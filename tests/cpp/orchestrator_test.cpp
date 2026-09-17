#include "core/runtime/orchestrator.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/system_executor.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace asio = boost::asio;

namespace obcx::core {
namespace {

template <typename T>
auto run_awaitable(asio::io_context &ioc, asio::awaitable<T> awaitable) -> T {
  std::optional<T> result;
  std::exception_ptr exception;

  asio::co_spawn(
      ioc,
      [&]() -> asio::awaitable<void> {
        try {
          result = co_await std::move(awaitable);
        } catch (...) {
          exception = std::current_exception();
        }
      },
      asio::detached);

  ioc.run();
  ioc.restart();

  if (exception) {
    std::rethrow_exception(exception);
  }

  return std::move(*result);
}

auto run_pair(asio::io_context &ioc, asio::awaitable<OrchestratorResult> first,
              asio::awaitable<OrchestratorResult> second)
    -> std::pair<OrchestratorResult, OrchestratorResult> {
  std::optional<OrchestratorResult> first_result;
  std::optional<OrchestratorResult> second_result;
  std::exception_ptr exception;

  asio::co_spawn(
      ioc,
      [&]() mutable -> asio::awaitable<void> {
        try {
          first_result = co_await std::move(first);
        } catch (...) {
          exception = std::current_exception();
        }
      },
      asio::detached);

  asio::co_spawn(
      ioc,
      [&]() mutable -> asio::awaitable<void> {
        try {
          second_result = co_await std::move(second);
        } catch (...) {
          exception = std::current_exception();
        }
      },
      asio::detached);

  ioc.run();
  ioc.restart();

  if (exception) {
    std::rethrow_exception(exception);
  }

  return {*first_result, *second_result};
}

template <typename T>
auto run_until_result(asio::io_context &ioc, asio::awaitable<T> awaitable)
    -> T {
  std::optional<T> result;
  std::exception_ptr exception;

  asio::co_spawn(
      ioc,
      [&]() -> asio::awaitable<void> {
        try {
          result = co_await std::move(awaitable);
        } catch (...) {
          exception = std::current_exception();
        }
      },
      asio::detached);

  while (!result.has_value() && !exception) {
    ioc.run_one();
  }
  ioc.restart();

  if (exception) {
    std::rethrow_exception(exception);
  }

  return std::move(*result);
}

class RecordingActor final : public IActorV2 {
public:
  RecordingActor(std::string name, std::string output_type,
                 std::vector<std::string> &calls)
      : name_(std::move(name)), output_type_(std::move(output_type)),
        calls_(calls) {}

  [[nodiscard]] auto get_name() const -> std::string override { return name_; }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    calls_.push_back(context.actor_id() + ":" + message.type);

    ActorResult result = ActorResult::success();
    if (!output_type_.empty()) {
      MessageEnvelope emitted;
      emitted.type = output_type_;
      emitted.correlation_id = message.correlation_id;
      emitted.causation_id = message.id;
      result.emit(std::move(emitted));
    }

    co_return result;
  }

private:
  std::string name_;
  std::string output_type_;
  std::vector<std::string> &calls_;
};

class FanOutActor final : public IActorV2 {
public:
  explicit FanOutActor(std::vector<std::string> &calls) : calls_(calls) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "fan_out";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    (void)context;
    calls_.push_back("fan_out:" + message.type);
    ActorResult result = ActorResult::success();
    for (const auto suffix : {"left", "right"}) {
      MessageEnvelope emitted;
      emitted.id = message.id + ":" + suffix;
      emitted.type = "FanOutBranch";
      emitted.correlation_id = message.correlation_id;
      emitted.causation_id = message.id;
      result.emit(std::move(emitted));
    }
    co_return result;
  }

private:
  std::vector<std::string> &calls_;
};

struct RuntimeServiceMarker {
  std::string label;
};

class RuntimeServiceAwareActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "service_aware";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    ActorResult result = ActorResult::success();
    MessageEnvelope emitted;
    emitted.type = "RuntimeServiceSeen";
    emitted.correlation_id = message.correlation_id;
    emitted.causation_id = message.id;

    if (const auto marker = context.get_service<RuntimeServiceMarker>()) {
      emitted.payload["label"] = marker->label;
    }

    result.emit(std::move(emitted));
    co_return result;
  }
};

class DbBindingAwareActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "db_aware";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    ActorResult result = ActorResult::success();
    MessageEnvelope emitted;
    emitted.type = "ActorDbBindingSeen";
    emitted.correlation_id = message.correlation_id;
    emitted.causation_id = message.id;
    emitted.payload["db"] = context.db_instance();
    emitted.payload["db_namespace"] = context.db_namespace();
    result.emit(std::move(emitted));
    co_return result;
  }
};

class FailingActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "failing";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    (void)message;
    (void)context;
    co_return ActorResult::failed("boom", "actor failed", false);
  }
};

class UnknownThrowingActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "unknown_throwing";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    (void)context;
    if (!message.type.empty()) {
      throw 42;
    }
    co_return ActorResult::success();
  }
};

class FailureSinkActor final : public IActorV2 {
public:
  explicit FailureSinkActor(
      std::shared_ptr<std::vector<MessageEnvelope>> messages)
      : messages_(std::move(messages)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "failure_sink";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    (void)context;
    messages_->push_back(message);
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<std::vector<MessageEnvelope>> messages_;
};

struct ConcurrentProbe {
  std::mutex mutex;
  int active = 0;
  int max_active = 0;
  std::vector<std::string> events;
};

class DelayedActor final : public IActorV2 {
public:
  explicit DelayedActor(std::shared_ptr<ConcurrentProbe> probe)
      : probe_(std::move(probe)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "message_store";
  }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {

    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("start:" + message.id);
      probe_->active++;
      probe_->max_active = std::max(probe_->max_active, probe_->active);
    }

    auto executor = asio::system_executor{};
    co_await context.await_asio(
        executor, [executor]() -> asio::awaitable<void> {
          asio::steady_timer timer(executor);
          timer.expires_after(std::chrono::milliseconds(5));
          co_await timer.async_wait(asio::use_awaitable);
        });

    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("finish:" + message.id);
      probe_->active--;
    }

    ActorResult result = ActorResult::success();
    MessageEnvelope emitted;
    emitted.type = "obcx::message_store::events::MessageStored";
    emitted.correlation_id = message.correlation_id;
    emitted.causation_id = message.id;
    result.emit(std::move(emitted));
    co_return result;
  }

private:
  std::shared_ptr<ConcurrentProbe> probe_;
};

class DelayedNamedActor final : public IActorV2 {
public:
  DelayedNamedActor(std::string name, std::string output_type,
                    std::shared_ptr<ConcurrentProbe> probe)
      : name_(std::move(name)), output_type_(std::move(output_type)),
        probe_(std::move(probe)) {}

  [[nodiscard]] auto get_name() const -> std::string override { return name_; }

  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {

    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("start:" + name_ + ":" + message.id);
    }
    auto executor = asio::system_executor{};
    co_await context.await_asio(
        executor, [executor]() -> asio::awaitable<void> {
          asio::steady_timer timer(executor);
          timer.expires_after(std::chrono::milliseconds(25));
          co_await timer.async_wait(asio::use_awaitable);
        });
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("finish:" + name_ + ":" + message.id);
    }

    ActorResult result = ActorResult::success();
    MessageEnvelope emitted;
    emitted.type = output_type_;
    emitted.correlation_id = message.correlation_id;
    emitted.causation_id = message.id;
    result.emit(std::move(emitted));
    co_return result;
  }

private:
  std::string name_;
  std::string output_type_;
  std::shared_ptr<ConcurrentProbe> probe_;
};

auto pipeline_with_stages(std::vector<common::PipelineStageConfig> stages)
    -> common::PipelineConfig {
  common::PipelineConfig pipeline;
  pipeline.name = "message";
  pipeline.source = "obcx::core::events::RawMessageEvent";
  pipeline.stages = std::move(stages);
  return pipeline;
}

auto stage(std::string name, std::string actor, std::string input,
           std::string output, std::string mode,
           std::vector<std::string> after = {}) -> common::PipelineStageConfig {
  common::PipelineStageConfig config;
  config.name = std::move(name);
  config.actor = std::move(actor);
  config.input = std::move(input);
  config.outputs = {std::move(output)};
  config.mode = std::move(mode);
  config.after = std::move(after);
  return config;
}

auto actor_config(std::string name, std::string partition)
    -> common::ActorConfig {
  common::ActorConfig config;
  config.name = std::move(name);
  config.enabled = true;
  config.partition = std::move(partition);
  return config;
}

auto raw_message(std::string id, std::string group_id) -> MessageEnvelope {
  MessageEnvelope raw;
  raw.id = std::move(id);
  raw.type = "obcx::core::events::RawMessageEvent";
  raw.source_platform = "qq";
  raw.source_bot = "qq-main";
  raw.conversation_id = "group:" + group_id;
  raw.correlation_id = "corr-" + raw.id;
  raw.payload["conversation_id"] = raw.conversation_id;
  raw.payload["group_id"] = std::move(group_id);
  return raw;
}

TEST(OrchestratorTest, ProcessesMessageThroughTwoStagePipeline) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "bridge", "bridge::events::MessageForwarded", calls));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
      stage("forward", "bridge", "obcx::message_store::events::MessageStored",
            "bridge::events::MessageForwarded", "await", {"persist"}),
  })});

  MessageEnvelope raw;
  raw.id = "raw-1";
  raw.type = "obcx::core::events::RawMessageEvent";
  raw.correlation_id = "corr-1";

  asio::io_context ioc;
  const auto result = run_awaitable(ioc, orchestrator.process(raw));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent",
                       "bridge:obcx::message_store::events::MessageStored",
                   }));
  ASSERT_EQ(result.emitted.size(), 2);
  EXPECT_EQ(result.emitted[0].type,
            "obcx::message_store::events::MessageStored");
  EXPECT_EQ(result.emitted[1].type, "bridge::events::MessageForwarded");
}

TEST(OrchestratorTest, ProcessAwaitableSurvivesOrchestratorDestruction) {
  std::vector<std::string> calls;
  std::optional<asio::awaitable<OrchestratorResult>> pending;
  {
    Orchestrator orchestrator;
    orchestrator.register_actor(std::make_shared<RecordingActor>(
        "message_store", "obcx::message_store::events::MessageStored", calls));
    orchestrator.configure_pipelines({pipeline_with_stages({
        stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
              "obcx::message_store::events::MessageStored", "await"),
    })});
    pending.emplace(
        orchestrator.process(raw_message("raw-process-lifetime", "g")));
  }

  asio::io_context ioc;
  const auto result = run_awaitable(ioc, std::move(*pending));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent"}));
}

TEST(OrchestratorTest, InjectsRuntimeServicesIntoActorContexts) {
  Orchestrator orchestrator;
  auto marker = std::make_shared<RuntimeServiceMarker>();
  marker->label = "db-main";
  orchestrator.register_service<RuntimeServiceMarker>(marker);
  orchestrator.register_actor(std::make_shared<RuntimeServiceAwareActor>());
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("inspect", "service_aware", "obcx::core::events::RawMessageEvent",
            "RuntimeServiceSeen", "await"),
  })});

  asio::io_context ioc;
  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("raw-svc", "g")));

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted[0].type, "RuntimeServiceSeen");
  EXPECT_EQ(result.emitted[0].payload["label"], "db-main");
}

TEST(OrchestratorTest, InjectsActorDbBindingIntoActorContexts) {
  common::ActorConfig config;
  config.name = "db_aware";
  config.enabled = true;
  config.db = "main";
  config.db_namespace = "message_store";

  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<DbBindingAwareActor>());
  orchestrator.configure_actors({config});
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("inspect", "db_aware", "obcx::core::events::RawMessageEvent",
            "ActorDbBindingSeen", "await"),
  })});

  asio::io_context ioc;
  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("raw-db", "g")));

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted[0].payload["db"], "main");
  EXPECT_EQ(result.emitted[0].payload["db_namespace"], "message_store");
}

TEST(OrchestratorTest, HonorsAfterDependenciesAndDefersTerminalAsyncStages) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(
      std::make_shared<RecordingActor>("audit", "AuditRecorded", calls));
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "bridge", "bridge::events::MessageForwarded", calls));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("audit_raw", "audit", "obcx::core::events::RawMessageEvent",
            "AuditRecorded", "async"),
      stage("forward", "bridge", "obcx::message_store::events::MessageStored",
            "bridge::events::MessageForwarded", "await", {"persist"}),
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  MessageEnvelope raw;
  raw.id = "raw-2";
  raw.type = "obcx::core::events::RawMessageEvent";

  asio::io_context ioc;
  const auto result = run_awaitable(ioc, orchestrator.process(raw));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent",
                       "bridge:obcx::message_store::events::MessageStored",
                       "audit:obcx::core::events::RawMessageEvent",
                   }));
  ASSERT_EQ(result.stages.size(), 3);
  EXPECT_EQ(result.stages[0].name, "persist");
  EXPECT_EQ(result.stages[1].name, "forward");
  EXPECT_EQ(result.stages[2].name, "audit_raw");
  EXPECT_TRUE(result.stages[2].terminal_async);
}

TEST(OrchestratorTest, RoutesEmittedMessagesBackThroughMatchingPipelines) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.register_actor(
      std::make_shared<RecordingActor>("audit", "AuditStored", calls));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("persist", "message_store",
                "obcx::core::events::RawMessageEvent",
                "obcx::message_store::events::MessageStored", "await"),
      }),
      common::PipelineConfig{
          .name = "stored_audit",
          .source = "obcx::message_store::events::MessageStored",
          .stages = {stage("audit_stored", "audit",
                           "obcx::message_store::events::MessageStored",
                           "AuditStored", "await")},
      },
  });

  asio::io_context ioc;
  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("raw-route", "g")));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent",
                       "audit:obcx::message_store::events::MessageStored",
                   }));
  ASSERT_EQ(result.emitted.size(), 2);
  EXPECT_EQ(result.emitted[0].type,
            "obcx::message_store::events::MessageStored");
  EXPECT_EQ(result.emitted[1].type, "AuditStored");
}

TEST(OrchestratorTest, TerminalAsyncStagesDoNotBlockProcessCompletion) {
  std::vector<std::string> calls;
  auto probe = std::make_shared<ConcurrentProbe>();
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.register_actor(
      std::make_shared<DelayedNamedActor>("audit", "AuditRecorded", probe));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
      stage("audit_raw", "audit", "obcx::core::events::RawMessageEvent",
            "AuditRecorded", "async"),
  })});

  asio::io_context ioc;
  const auto result = run_until_result(
      ioc, orchestrator.process(raw_message("raw-async", "g")));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent"}));
  ASSERT_EQ(result.stages.size(), 2);
  EXPECT_EQ(result.stages[1].name, "audit_raw");
  EXPECT_TRUE(result.stages[1].terminal_async);
  EXPECT_TRUE(probe->events.empty());
  EXPECT_EQ(orchestrator.pending_terminal_tasks(), 1);

  ioc.run();
  ioc.restart();
  EXPECT_EQ(orchestrator.pending_terminal_tasks(), 0);
  EXPECT_EQ(probe->events, (std::vector<std::string>{
                               "start:audit:raw-async",
                               "finish:audit:raw-async",
                           }));
}

TEST(OrchestratorTest, TerminalAsyncStageSurvivesOrchestratorDestruction) {
  std::vector<std::string> calls;
  auto probe = std::make_shared<ConcurrentProbe>();
  asio::io_context ioc;

  {
    Orchestrator orchestrator;
    orchestrator.register_actor(std::make_shared<RecordingActor>(
        "message_store", "obcx::message_store::events::MessageStored", calls));
    orchestrator.register_actor(
        std::make_shared<DelayedNamedActor>("audit", "AuditRecorded", probe));
    orchestrator.configure_pipelines({pipeline_with_stages({
        stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
              "obcx::message_store::events::MessageStored", "await"),
        stage("audit_raw", "audit", "obcx::core::events::RawMessageEvent",
              "AuditRecorded", "async"),
    })});

    const auto result = run_until_result(
        ioc, orchestrator.process(raw_message("raw-lifetime", "g")));
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(probe->events.empty());
  }

  ioc.run();
  ioc.restart();
  EXPECT_EQ(probe->events, (std::vector<std::string>{
                               "start:audit:raw-lifetime",
                               "finish:audit:raw-lifetime",
                           }));
}

TEST(OrchestratorTest, RuntimeShutdownCancelsTrackedTerminalStage) {
  asio::io_context ioc;
  NativeActorSchedulerOptions options;
  options.worker_count = 1;
  auto probe = std::make_shared<ConcurrentProbe>();
  Orchestrator orchestrator(options);
  orchestrator.register_actor(
      std::make_shared<DelayedNamedActor>("audit", "AuditRecorded", probe));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("audit_raw", "audit", "obcx::core::events::RawMessageEvent",
            "AuditRecorded", "async"),
  })});

  const auto result = run_until_result(
      ioc, orchestrator.process(raw_message("raw-shutdown", "g")));
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(orchestrator.pending_terminal_tasks(), 1);
  EXPECT_TRUE(probe->events.empty());

  orchestrator.shutdown();
  ioc.run();
  ioc.restart();
  EXPECT_EQ(orchestrator.pending_terminal_tasks(), 0);
  EXPECT_TRUE(probe->events.empty());
}

TEST(OrchestratorTest, RoutesTerminalAsyncFailuresToActorFailedPipeline) {
  auto failures = std::make_shared<std::vector<MessageEnvelope>>();
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<FailingActor>());
  orchestrator.register_actor(std::make_shared<FailureSinkActor>(failures));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("fail_async", "failing", "obcx::core::events::RawMessageEvent",
                "ActorFailed", "async"),
      }),
      common::PipelineConfig{
          .name = "failure_handler",
          .source = "ActorFailed",
          .stages = {stage("record_failure", "failure_sink", "ActorFailed",
                           "FailureRecorded", "await")},
      },
  });

  asio::io_context ioc;
  const auto result = run_until_result(
      ioc, orchestrator.process(raw_message("raw-async-failure", "group-7")));

  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(failures->empty());

  ioc.run();
  ioc.restart();

  ASSERT_EQ(failures->size(), 1);
  const auto &failure = failures->front();
  EXPECT_EQ(failure.type, "ActorFailed");
  EXPECT_EQ(failure.source_platform, "qq");
  EXPECT_EQ(failure.source_bot, "qq-main");
  EXPECT_EQ(failure.conversation_id, "group:group-7");
  EXPECT_EQ(failure.correlation_id, "corr-raw-async-failure");
  EXPECT_EQ(failure.causation_id, "raw-async-failure");
  EXPECT_EQ(failure.payload["pipeline"], "message");
  EXPECT_EQ(failure.payload["stage"], "fail_async");
  EXPECT_EQ(failure.payload["actor"], "failing");
  EXPECT_EQ(failure.payload["code"], "boom");
  EXPECT_EQ(failure.payload["message"], "actor failed");
  EXPECT_EQ(failure.payload["retryable"], false);
}

TEST(OrchestratorTest, RoutesUnknownTerminalActorExceptions) {
  auto failures = std::make_shared<std::vector<MessageEnvelope>>();
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<UnknownThrowingActor>());
  orchestrator.register_actor(std::make_shared<FailureSinkActor>(failures));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("throw_async", "unknown_throwing",
                "obcx::core::events::RawMessageEvent", "ActorFailed", "async"),
      }),
      common::PipelineConfig{
          .name = "failure_handler",
          .source = "ActorFailed",
          .stages = {stage("record_failure", "failure_sink", "ActorFailed",
                           "FailureRecorded", "await")},
      },
  });

  asio::io_context ioc;
  const auto result = run_until_result(
      ioc, orchestrator.process(raw_message("raw-unknown-throw", "group-8")));
  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(failures->empty());

  ioc.run();
  ioc.restart();

  ASSERT_EQ(failures->size(), 1);
  const auto &failure = failures->front();
  EXPECT_EQ(failure.payload["pipeline"], "message");
  EXPECT_EQ(failure.payload["stage"], "throw_async");
  EXPECT_EQ(failure.payload["actor"], "unknown_throwing");
  EXPECT_EQ(failure.payload["code"], "actor_exception");
  EXPECT_EQ(failure.payload["message"], "unknown actor exception");
  EXPECT_EQ(failure.payload["retryable"], true);
}

TEST(OrchestratorTest, ResolvesDefaultAndConfiguredPartitionKeys) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  asio::io_context ioc;
  const auto default_result =
      run_awaitable(ioc, orchestrator.process(raw_message("raw-3", "group-1")));
  ASSERT_EQ(default_result.stages.size(), 1);
  EXPECT_EQ(default_result.stages[0].partition_key, "global");

  orchestrator.configure_actors(
      {actor_config("message_store", "source_platform:group_id")});

  const auto configured_result =
      run_awaitable(ioc, orchestrator.process(raw_message("raw-4", "group-7")));
  ASSERT_EQ(configured_result.stages.size(), 1);
  EXPECT_EQ(configured_result.stages[0].partition_key, "qq:group-7");
}

TEST(OrchestratorTest, ProcessUsesAnImmutableRoutingSnapshot) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  auto pending = orchestrator.process(raw_message("snapshot-old", "group-1"));
  orchestrator.configure_pipelines({});

  asio::io_context ioc;
  const auto old_snapshot = run_awaitable(ioc, std::move(pending));
  EXPECT_EQ(old_snapshot.stages.size(), 1);
  const auto new_snapshot = run_awaitable(
      ioc, orchestrator.process(raw_message("snapshot-new", "group-1")));
  EXPECT_TRUE(new_snapshot.stages.empty());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent"}));
}

TEST(OrchestratorTest, SerializesSameActorPartitionMailbox) {
  auto probe = std::make_shared<ConcurrentProbe>();
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<DelayedActor>(probe));
  orchestrator.configure_actors(
      {actor_config("message_store", "source_platform:group_id")});
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  asio::io_context ioc;
  auto [first, second] =
      run_pair(ioc, orchestrator.process(raw_message("raw-5", "group-1")),
               orchestrator.process(raw_message("raw-6", "group-1")));

  EXPECT_TRUE(first.ok());
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(probe->max_active, 1);
  EXPECT_EQ(probe->events, (std::vector<std::string>{
                               "start:raw-5",
                               "finish:raw-5",
                               "start:raw-6",
                               "finish:raw-6",
                           }));
}

TEST(OrchestratorTest, AllowsDifferentActorPartitionsToRunConcurrently) {
  auto probe = std::make_shared<ConcurrentProbe>();
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<DelayedActor>(probe));
  orchestrator.configure_actors(
      {actor_config("message_store", "source_platform:group_id")});
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  asio::io_context ioc;
  auto [first, second] =
      run_pair(ioc, orchestrator.process(raw_message("raw-7", "group-1")),
               orchestrator.process(raw_message("raw-8", "group-2")));

  EXPECT_TRUE(first.ok());
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(probe->max_active, 2);
}

TEST(OrchestratorTest, SurfacesSchedulerBackpressureAsActorFailure) {
  auto probe = std::make_shared<ConcurrentProbe>();
  NativeActorSchedulerOptions options;
  options.max_pending_tasks = 1;
  Orchestrator orchestrator(options);
  orchestrator.register_actor(std::make_shared<DelayedActor>(probe));
  orchestrator.configure_actors(
      {actor_config("message_store", "source_platform:group_id")});
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  asio::io_context ioc;
  auto [first, second] =
      run_pair(ioc, orchestrator.process(raw_message("raw-bp-1", "group-1")),
               orchestrator.process(raw_message("raw-bp-2", "group-2")));

  EXPECT_TRUE(first.ok());
  ASSERT_FALSE(second.ok());
  ASSERT_EQ(second.failures.size(), 1);
  EXPECT_EQ(second.failures[0].failure.code, "scheduler_backpressure");
  ASSERT_EQ(second.emitted.size(), 1);
  EXPECT_EQ(second.emitted[0].type, "ActorFailed");
}

TEST(OrchestratorTest, EmitsFailureEnvelopeWhenActorFails) {
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<FailingActor>());
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("fail", "failing", "obcx::core::events::RawMessageEvent",
            "ActorFailed", "await"),
  })});

  asio::io_context ioc;
  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("raw-9", "group-1")));

  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.failures.size(), 1);
  EXPECT_EQ(result.failures[0].failure.code, "boom");
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted[0].type, "ActorFailed");
  EXPECT_EQ(result.emitted[0].payload["pipeline"], "message");
  EXPECT_EQ(result.emitted[0].payload["stage"], "fail");
  EXPECT_EQ(result.emitted[0].payload["actor"], "failing");
  EXPECT_EQ(result.emitted[0].payload["code"], "boom");
  EXPECT_EQ(result.emitted[0].payload["message"], "actor failed");
  EXPECT_EQ(result.emitted[0].payload["retryable"], false);
}

TEST(OrchestratorTest, RoutesAwaitedFailuresToActorFailedPipeline) {
  auto failures = std::make_shared<std::vector<MessageEnvelope>>();
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<FailingActor>());
  orchestrator.register_actor(std::make_shared<FailureSinkActor>(failures));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("fail", "failing", "obcx::core::events::RawMessageEvent",
                "ActorFailed", "await"),
      }),
      common::PipelineConfig{
          .name = "failure_handler",
          .source = "ActorFailed",
          .stages = {stage("record_failure", "failure_sink", "ActorFailed",
                           "FailureRecorded", "await")},
      },
  });

  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, orchestrator.process(raw_message("raw-awaited-failure", "group-9")));

  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.failures.size(), 1);
  ASSERT_EQ(failures->size(), 1);
  EXPECT_EQ(failures->front().type, "ActorFailed");
  EXPECT_EQ(failures->front().payload["pipeline"], "message");
  EXPECT_EQ(failures->front().payload["stage"], "fail");
  EXPECT_EQ(failures->front().payload["code"], "boom");
}

TEST(OrchestratorTest, PreservesPipelineOrderAndEmittedMessageRecursion) {
  asio::io_context ioc;
  NativeActorSchedulerOptions options;
  options.worker_count = 2;
  Orchestrator orchestrator(options);

  std::vector<std::string> calls;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "message_store", "obcx::message_store::events::MessageStored", calls));
  orchestrator.register_actor(
      std::make_shared<RecordingActor>("audit", "AuditStored", calls));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("persist", "message_store",
                "obcx::core::events::RawMessageEvent",
                "obcx::message_store::events::MessageStored", "await"),
      }),
      common::PipelineConfig{
          .name = "stored_audit",
          .source = "obcx::message_store::events::MessageStored",
          .stages = {stage("audit_stored", "audit",
                           "obcx::message_store::events::MessageStored",
                           "AuditStored", "await")},
      },
  });

  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("dual", "g")));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "message_store:obcx::core::events::RawMessageEvent",
                       "audit:obcx::message_store::events::MessageStored",
                   }));
  ASSERT_EQ(result.emitted.size(), 2);
  EXPECT_EQ(result.emitted[0].type,
            "obcx::message_store::events::MessageStored");
  EXPECT_EQ(result.emitted[1].type, "AuditStored");
  orchestrator.shutdown();
}

TEST(OrchestratorTest, RoutesActorFailureThroughNativeScheduler) {
  asio::io_context ioc;
  NativeActorSchedulerOptions options;
  options.worker_count = 2;
  Orchestrator orchestrator(options);
  orchestrator.register_actor(std::make_shared<FailingActor>());
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("fail", "failing", "obcx::core::events::RawMessageEvent",
            "ActorFailed", "await"),
  })});

  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("dual-fail", "g")));
  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.failures.size(), 1);
  EXPECT_EQ(result.failures.front().failure.code, "boom");
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().type, "ActorFailed");
  orchestrator.shutdown();
}

TEST(OrchestratorTest, PreservesBackpressureFailureRouting) {
  asio::io_context ioc;
  NativeActorSchedulerOptions options;
  options.worker_count = 2;
  options.max_pending_tasks = 1;
  Orchestrator orchestrator(options);
  auto probe = std::make_shared<ConcurrentProbe>();
  orchestrator.register_actor(std::make_shared<DelayedActor>(probe));
  orchestrator.configure_actors(
      {actor_config("message_store", "source_platform:group_id")});
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
            "obcx::message_store::events::MessageStored", "await"),
  })});

  auto [first, second] =
      run_pair(ioc, orchestrator.process(raw_message("bp-one", "one")),
               orchestrator.process(raw_message("bp-two", "two")));
  EXPECT_TRUE(first.ok());
  ASSERT_FALSE(second.ok());
  ASSERT_EQ(second.failures.size(), 1);
  EXPECT_EQ(second.failures.front().failure.code, "scheduler_backpressure");
  ASSERT_EQ(second.emitted.size(), 1);
  EXPECT_EQ(second.emitted.front().type, "ActorFailed");
  orchestrator.shutdown();
}

TEST(OrchestratorTest, NativeProcessSurvivesOrchestratorDestruction) {
  asio::io_context ioc;
  auto probe = std::make_shared<ConcurrentProbe>();
  std::optional<asio::awaitable<OrchestratorResult>> pending;
  {
    NativeActorSchedulerOptions options;
    options.worker_count = 2;
    Orchestrator orchestrator(options);
    orchestrator.register_actor(std::make_shared<DelayedActor>(probe));
    orchestrator.configure_pipelines({pipeline_with_stages({
        stage("persist", "message_store", "obcx::core::events::RawMessageEvent",
              "obcx::message_store::events::MessageStored", "await"),
    })});
    pending.emplace(orchestrator.process(raw_message("native-life", "g")));
  }

  const auto result = run_awaitable(ioc, std::move(*pending));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(probe->events, (std::vector<std::string>{"start:native-life",
                                                     "finish:native-life"}));
}

TEST(OrchestratorTest, RejectsAnActuallyRepeatedRouteAncestor) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<RecordingActor>(
      "loop", "obcx::core::events::RawMessageEvent", calls));
  orchestrator.configure_pipelines({pipeline_with_stages({
      stage("repeat", "loop", "obcx::core::events::RawMessageEvent",
            "obcx::core::events::RawMessageEvent", "await"),
  })});

  auto message = raw_message("cycle", "group");
  message.payload["secret"] = "must-not-appear";
  asio::io_context ioc;
  const auto result = run_awaitable(ioc, orchestrator.process(message));

  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.failures.size(), 1);
  EXPECT_EQ(result.failures.front().failure.code, "message_routing_cycle");
  EXPECT_NE(result.failures.front().failure.message.find(
                "message.repeat(obcx::core::events::RawMessageEvent)"),
            std::string::npos);
  EXPECT_EQ(result.failures.front().failure.message.find("must-not-appear"),
            std::string::npos);
  EXPECT_EQ(calls.size(), 1);
}

TEST(OrchestratorTest, AllowsNonRepeatingRouteBeyondDefaultHopCountWhenRaised) {
  constexpr size_t route_length = 40;
  std::vector<std::string> calls;
  std::vector<common::PipelineConfig> pipelines;
  Orchestrator orchestrator;
  orchestrator.set_routing_hop_limit(64);

  for (size_t index = 0; index < route_length; ++index) {
    const auto input = "Route" + std::to_string(index);
    const auto output = index + 1 == route_length
                            ? std::string{}
                            : "Route" + std::to_string(index + 1);
    const auto actor = "route_actor_" + std::to_string(index);
    orchestrator.register_actor(
        std::make_shared<RecordingActor>(actor, output, calls));
    pipelines.push_back(common::PipelineConfig{
        .name = "route_pipeline_" + std::to_string(index),
        .source = input,
        .stages = {stage("route_stage_" + std::to_string(index), actor, input,
                         output, "await")},
    });
  }
  orchestrator.configure_pipelines(std::move(pipelines));

  MessageEnvelope message;
  message.id = "long-route";
  message.type = "Route0";
  asio::io_context ioc;
  const auto result = run_awaitable(ioc, orchestrator.process(message));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls.size(), route_length);
  EXPECT_EQ(result.stages.size(), route_length);
}

TEST(OrchestratorTest, CompletesAtLimitAndFailsExplicitlyBeyondIt) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.set_routing_hop_limit(3);
  for (size_t index = 0; index < 4; ++index) {
    orchestrator.register_actor(std::make_shared<RecordingActor>(
        "limited_actor_" + std::to_string(index),
        index == 3 ? std::string{} : "Limited" + std::to_string(index + 1),
        calls));
  }
  orchestrator.configure_pipelines({
      common::PipelineConfig{
          .name = "limited_0",
          .source = "Limited0",
          .stages = {stage("stage_0", "limited_actor_0", "Limited0", "Limited1",
                           "await")}},
      common::PipelineConfig{
          .name = "limited_1",
          .source = "Limited1",
          .stages = {stage("stage_1", "limited_actor_1", "Limited1", "Limited2",
                           "await")}},
      common::PipelineConfig{
          .name = "limited_2",
          .source = "Limited2",
          .stages = {stage("stage_2", "limited_actor_2", "Limited2", "Limited3",
                           "await")}},
      common::PipelineConfig{.name = "limited_3",
                             .source = "Limited3",
                             .stages = {stage("stage_3", "limited_actor_3",
                                              "Limited3", "", "await")}},
  });

  MessageEnvelope at_limit;
  at_limit.id = "at-limit";
  at_limit.type = "Limited1";
  asio::io_context ioc;
  const auto accepted = run_awaitable(ioc, orchestrator.process(at_limit));
  EXPECT_TRUE(accepted.ok());
  EXPECT_EQ(accepted.stages.size(), 3);

  calls.clear();
  MessageEnvelope beyond_limit;
  beyond_limit.id = "beyond-limit";
  beyond_limit.type = "Limited0";
  const auto rejected = run_awaitable(ioc, orchestrator.process(beyond_limit));
  ASSERT_FALSE(rejected.ok());
  ASSERT_EQ(rejected.failures.size(), 1);
  EXPECT_EQ(rejected.failures.front().failure.code,
            "message_routing_hop_limit");
  EXPECT_EQ(rejected.stages.size(), 3);
}

TEST(OrchestratorTest, KeepsFanOutSiblingRouteContextsIndependent) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.register_actor(std::make_shared<FanOutActor>(calls));
  orchestrator.register_actor(
      std::make_shared<RecordingActor>("fan_in", "", calls));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("fan", "fan_out", "obcx::core::events::RawMessageEvent",
                "FanOutBranch", "await"),
      }),
      common::PipelineConfig{
          .name = "fan_branch",
          .source = "FanOutBranch",
          .stages = {stage("consume", "fan_in", "FanOutBranch", "", "await")},
      },
  });

  asio::io_context ioc;
  const auto result =
      run_awaitable(ioc, orchestrator.process(raw_message("fan-out", "group")));

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(calls, (std::vector<std::string>{
                       "fan_out:obcx::core::events::RawMessageEvent",
                       "fan_in:FanOutBranch",
                       "fan_in:FanOutBranch",
                   }));
}

TEST(OrchestratorTest, TerminalAsyncDescendantsRetainTheOriginatingRoute) {
  std::vector<std::string> calls;
  Orchestrator orchestrator;
  orchestrator.set_routing_hop_limit(1);
  orchestrator.register_actor(
      std::make_shared<RecordingActor>("terminal", "TerminalOutput", calls));
  orchestrator.register_actor(
      std::make_shared<RecordingActor>("terminal_sink", "", calls));
  orchestrator.configure_pipelines({
      pipeline_with_stages({
          stage("terminal", "terminal", "obcx::core::events::RawMessageEvent",
                "TerminalOutput", "async"),
      }),
      common::PipelineConfig{
          .name = "terminal_output",
          .source = "TerminalOutput",
          .stages = {stage("sink", "terminal_sink", "TerminalOutput", "",
                           "await")},
      },
  });

  asio::io_context ioc;
  const auto result = run_until_result(
      ioc, orchestrator.process(raw_message("terminal-route", "group")));
  EXPECT_TRUE(result.ok());
  ioc.run();
  ioc.restart();

  EXPECT_EQ(calls, (std::vector<std::string>{
                       "terminal:obcx::core::events::RawMessageEvent"}));
  EXPECT_EQ(orchestrator.pending_terminal_tasks(), 0);
}

} // namespace
} // namespace obcx::core
