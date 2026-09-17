#include "core/actor/native_actor_scheduler.hpp"
#include "core/actor/reflected_actor.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace obcx::tests::reflection {

struct SyncInput {
  int value = 0;
};

struct AsyncInput {
  std::string value;
};

struct Output {
  int value = 0;
};

struct ImmediateAsioInput {
  std::uint64_t sequence = 0;
};

struct ChatCommand final : command::RequestMessage<ChatCommand> {};
struct ToggleThinkCommand final : command::RequestMessage<ToggleThinkCommand> {
};

struct Outer {
  struct NestedMessage {
    int value = 0;
  };
};

inline void from_json(const common::json &document, SyncInput &message) {
  document.at("value").get_to(message.value);
}

inline void to_json(common::json &document, const SyncInput &message) {
  document = {{"value", message.value}};
}

inline void from_json(const common::json &document, AsyncInput &message) {
  document.at("value").get_to(message.value);
}

inline void to_json(common::json &document, const AsyncInput &message) {
  document = {{"value", message.value}};
}

inline void to_json(common::json &document, const Output &message) {
  document = {{"value", message.value}};
}

inline void from_json(const common::json &document,
                      ImmediateAsioInput &message) {
  document.at("sequence").get_to(message.sequence);
}

inline void to_json(common::json &document, const ImmediateAsioInput &message) {
  document = {{"sequence", message.sequence}};
}

inline void from_json(const common::json &document,
                      Outer::NestedMessage &message) {
  document.at("value").get_to(message.value);
}

inline void to_json(common::json &document,
                    const Outer::NestedMessage &message) {
  document = {{"value", message.value}};
}

class TestActor final : public core::ReflectedActor<TestActor> {
public:
  static constexpr std::string_view actor_name = "reflected_test";
  static constexpr std::string_view actor_version = "1.0";

  static constexpr auto command_contract() {
    return command::catalog(
        command::observe<ChatCommand>("chat", "Chat with the test actor",
                                      command::re2(R"(^(?:chat|ask)$)")),
        command::observe<ToggleThinkCommand>("toggle_think",
                                             "Toggle test thinking"));
  }

  auto handle(const SyncInput &input, const core::MessageEnvelope &envelope,
              core::ActorContext &) -> core::ActorResult {
    auto result = core::ActorResult::success();
    result.emit(Output{.value = input.value + 1}, envelope);
    return result;
  }

  auto handle(const AsyncInput &input, const core::MessageEnvelope &,
              core::ActorContext &context)
      -> core::ActorTask<core::ActorResult> {
    before_suspend = input.value;
    co_await context.yield();
    after_suspend = input.value;
    co_return core::ActorResult::success();
  }

  auto handle(const ChatCommand &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult {
    return core::ActorResult::success();
  }

  auto handle(const ToggleThinkCommand &, const core::MessageEnvelope &,
              core::ActorContext &) -> core::ActorResult {
    return core::ActorResult::success();
  }

  std::string before_suspend;
  std::string after_suspend;
};

class ImmediateAsioActor final
    : public core::ReflectedActor<ImmediateAsioActor> {
public:
  static constexpr std::string_view actor_name = "reflected-immediate-asio";
  static constexpr std::string_view actor_version = "test";

  explicit ImmediateAsioActor(boost::asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  auto handle(const ImmediateAsioInput &input, const core::MessageEnvelope &,
              core::ActorContext &context)
      -> core::ActorTask<core::ActorResult> {
    const auto observed = co_await context.await_asio(
        executor_,
        [sequence = input.sequence]() -> boost::asio::awaitable<std::uint64_t> {
          co_return sequence;
        });
    if (observed != input.sequence) {
      co_return core::ActorResult::failed(
          "sequence_mismatch", "immediate Asio result changed", false);
    }
    co_return core::ActorResult::success();
  }

private:
  boost::asio::any_io_executor executor_;
};

} // namespace obcx::tests::reflection

namespace obcx::core {
namespace {

using obcx::tests::reflection::AsyncInput;
using obcx::tests::reflection::ChatCommand;
using obcx::tests::reflection::ImmediateAsioActor;
using obcx::tests::reflection::ImmediateAsioInput;
using obcx::tests::reflection::Outer;
using obcx::tests::reflection::Output;
using obcx::tests::reflection::SyncInput;
using obcx::tests::reflection::TestActor;
using obcx::tests::reflection::ToggleThinkCommand;
using namespace std::chrono_literals;

auto run_to_completion(ActorTask<ActorResult> task) -> ActorResult {
  task.attach_runtime(ActorTaskRuntimeContext{});
  while (!task.done()) {
    task.resume();
  }
  return task.take_result();
}

TEST(ReflectedActorTest, DerivesNestedAndDealiasedCanonicalNames) {
  using Alias = Outer::NestedMessage;
  EXPECT_EQ(canonical_message_type_name<Alias>(),
            "obcx::tests::reflection::Outer::NestedMessage");
  EXPECT_EQ(canonical_message_type_name<const SyncInput &>(),
            "obcx::tests::reflection::SyncInput");
}

TEST(ReflectedActorTest, GeneratesSortedUniqueInputContract) {
  const auto contract = common::json::parse(TestActor::input_contract_json());
  EXPECT_EQ(contract["schema_version"], 2);
  EXPECT_EQ(contract["actor"], "reflected_test");
  EXPECT_EQ(contract["accepted_inputs"],
            (common::json{
                "obcx::tests::reflection::AsyncInput",
                "obcx::tests::reflection::ChatCommand",
                "obcx::tests::reflection::SyncInput",
                "obcx::tests::reflection::ToggleThinkCommand",
            }));
  EXPECT_EQ(
      contract["commands"],
      (common::json{
          {{"name", "chat"},
           {"description", "Chat with the test actor"},
           {"request_type", canonical_message_type_name<ChatCommand>()},
           {"matcher",
            {{"kind", "re2"},
             {"pattern", R"(^(?:chat|ask)$)"},
             {"mode", "full"}}}},
          {{"name", "toggle_think"},
           {"description", "Toggle test thinking"},
           {"request_type", canonical_message_type_name<ToggleThinkCommand>()}},
      }));
  EXPECT_FALSE(contract.contains("outputs"));
}

TEST(ReflectedActorTest, SerializesCommandRequestAndCompletionMessages) {
  ChatCommand request;
  request.invocation = command::CommandInvocation{
      .transaction_id = "generation-7:command-1",
      .name = "chat",
      .arguments = "hello",
      .source_message_id = "raw-1",
      .source_platform = "telegram",
      .source_bot = "bot",
      .conversation_id = "chat:1",
      .sender = "42",
      .source_event = {{"message_id", "raw-1"}},
  };
  const auto document = common::json(request);
  const auto decoded = document.get<ChatCommand>();
  EXPECT_EQ(decoded.invocation.transaction_id, "generation-7:command-1");
  EXPECT_EQ(decoded.invocation.arguments, "hello");
  EXPECT_EQ(decoded.invocation.source_event["message_id"], "raw-1");

  const auto completion_document = common::json(command::CommandCompleted{
      .transaction_id = request.invocation.transaction_id,
      .propagation = command::Propagation::Continue,
  });
  const auto completion = completion_document.get<command::CommandCompleted>();
  EXPECT_EQ(completion.transaction_id, request.invocation.transaction_id);
  EXPECT_EQ(completion.propagation, command::Propagation::Continue);
}

TEST(ReflectedActorTest, DispatchesExactTypeThroughAdlJsonAndNormalizesSync) {
  TestActor actor;
  ActorContext context("reflected_test");
  MessageEnvelope envelope;
  envelope.id = "sync";
  envelope.type = canonical_message_type_name<SyncInput>();
  envelope.payload = {{"value", 41}};

  const auto result =
      run_to_completion(actor.handle_message(envelope, context));

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().type, canonical_message_type_name<Output>());
  EXPECT_EQ(result.emitted.front().payload, (common::json{{"value", 42}}));
}

TEST(ReflectedActorTest, ReportsUnsupportedAndInvalidPayloadWithoutContents) {
  TestActor actor;
  ActorContext context("reflected_test");
  MessageEnvelope unsupported;
  unsupported.id = "unsupported";
  unsupported.type = "other::Secret";
  unsupported.payload = {{"secret", "do-not-log"}};

  const auto unsupported_result =
      run_to_completion(actor.handle_message(unsupported, context));
  ASSERT_FALSE(unsupported_result.ok());
  EXPECT_EQ(unsupported_result.failure->code, "unsupported_message_type");
  EXPECT_EQ(unsupported_result.failure->message.find("do-not-log"),
            std::string::npos);

  MessageEnvelope invalid;
  invalid.id = "invalid";
  invalid.type = canonical_message_type_name<SyncInput>();
  invalid.payload = {{"value", "still-secret"}};
  const auto invalid_result =
      run_to_completion(actor.handle_message(invalid, context));
  ASSERT_FALSE(invalid_result.ok());
  EXPECT_EQ(invalid_result.failure->code, "invalid_message_payload");
  EXPECT_EQ(invalid_result.failure->message.find("still-secret"),
            std::string::npos);
}

TEST(ReflectedActorTest, KeepsDecodedAsyncInputAliveAcrossSuspension) {
  TestActor actor;
  ActorContext context("reflected_test");
  ActorTask<ActorResult> task;
  {
    MessageEnvelope envelope;
    envelope.id = "async";
    envelope.type = canonical_message_type_name<AsyncInput>();
    envelope.payload = {{"value", "retained"}};
    task = actor.handle_message(envelope, context);
    task.attach_runtime(ActorTaskRuntimeContext{});
    task.resume();
    ASSERT_FALSE(task.done());
    EXPECT_EQ(task.suspension(), ActorTaskSuspension::Yielded);
    EXPECT_EQ(actor.before_suspend, "retained");
  }

  task.resume();
  ASSERT_TRUE(task.done());
  EXPECT_TRUE(task.take_result().ok());
  EXPECT_EQ(actor.after_suspend, "retained");
}

TEST(ReflectedActorTest, TypedEmitInheritsAndOverridesRoutingMetadata) {
  MessageEnvelope parent;
  parent.id = "parent";
  parent.source_platform = "qq";
  parent.source_bot = "bot";
  parent.conversation_id = "group:1";
  parent.headers = {{"inherited", "yes"}, {"replace", "old"}};

  auto result = ActorResult::success();
  result.emit(Output{.value = 7}, parent,
              ActorEmitOptions{
                  .id = "custom",
                  .source_platform = "telegram",
                  .headers = {{"replace", "new"}, {"extra", "value"}},
              });
  ASSERT_EQ(result.emitted.size(), 1);
  const auto &emitted = result.emitted.front();
  EXPECT_EQ(emitted.id, "custom");
  EXPECT_EQ(emitted.type, canonical_message_type_name<Output>());
  EXPECT_EQ(emitted.source_platform, "telegram");
  EXPECT_EQ(emitted.source_bot, "bot");
  EXPECT_EQ(emitted.conversation_id, "group:1");
  EXPECT_EQ(emitted.correlation_id, "parent");
  EXPECT_EQ(emitted.causation_id, "parent");
  EXPECT_EQ(emitted.headers.at("inherited"), "yes");
  EXPECT_EQ(emitted.headers.at("replace"), "new");
  EXPECT_EQ(emitted.headers.at("extra"), "value");
  EXPECT_EQ(emitted.payload, (common::json{{"value", 7}}));

  MessageEnvelope low_level;
  low_level.id = "low";
  low_level.type = "dynamic::Envelope";
  result.emit(low_level);
  ASSERT_EQ(result.emitted.size(), 2);
  EXPECT_EQ(result.emitted.back().type, "dynamic::Envelope");
}

TEST(ReflectedActorTest, TypedEmitGeneratesUniqueDefaultIds) {
  MessageEnvelope parent;
  parent.id = "parent";

  auto result = ActorResult::success();
  result.emit(Output{.value = 1}, parent);
  result.emit(Output{.value = 2}, parent);

  ASSERT_EQ(result.emitted.size(), 2);
  EXPECT_EQ(result.emitted[0].id,
            "parent:emit:0:obcx::tests::reflection::Output");
  EXPECT_EQ(result.emitted[1].id,
            "parent:emit:1:obcx::tests::reflection::Output");
  EXPECT_NE(result.emitted[0].id, result.emitted[1].id);
}

TEST(ReflectedActorTest,
     ImmediateAsioCompletionCannotOutrunNestedEpochPropagation) {
  boost::asio::io_context io;
  auto work = boost::asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(
      std::make_shared<ImmediateAsioActor>(io.get_executor()));

  constexpr std::uint64_t iterations = 25'000;
  bool completed_all = true;
  for (std::uint64_t sequence = 0; sequence < iterations; ++sequence) {
    auto completion = std::make_shared<std::promise<ActorResult>>();
    auto result = completion->get_future();
    MessageEnvelope message;
    message.id = "immediate-asio-" + std::to_string(sequence);
    message.type = canonical_message_type_name<ImmediateAsioInput>();
    message.payload = {{"sequence", sequence}};

    if (!scheduler.enqueue(
            ActorInvocation{.actor_id = "reflected-immediate-asio",
                            .partition_key = "same",
                            .message = std::move(message)},
            [completion](ActorResult actor_result) {
              completion->set_value(std::move(actor_result));
            })) {
      ADD_FAILURE() << "scheduler rejected sequence " << sequence;
      completed_all = false;
      break;
    }
    if (result.wait_for(2s) != std::future_status::ready) {
      ADD_FAILURE() << "completion timed out at sequence " << sequence;
      completed_all = false;
      break;
    }
    const auto actor_result = result.get();
    if (!actor_result.ok()) {
      ADD_FAILURE() << "actor failed at sequence " << sequence << ": "
                    << actor_result.failure->code;
      completed_all = false;
      break;
    }
  }

  scheduler.shutdown(completed_all ? ActorExecutorShutdownMode::Drain
                                   : ActorExecutorShutdownMode::Cancel);
  work.reset();
  io.stop();
  io_thread.join();
  EXPECT_TRUE(completed_all);
}

} // namespace
} // namespace obcx::core
