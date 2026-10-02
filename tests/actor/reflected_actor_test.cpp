#include "core/actor/native_actor_scheduler.hpp"
#include "support/reflected_test_actor.hpp"

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
using obcx::tests::reflection::ImmediateAsioActor;
using obcx::tests::reflection::ImmediateAsioInput;
using obcx::tests::reflection::Outer;
using obcx::tests::reflection::Output;
using obcx::tests::reflection::SyncInput;
using obcx::tests::reflection::TestActor;
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
