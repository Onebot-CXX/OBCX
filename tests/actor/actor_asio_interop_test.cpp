#include "core/actor/native_actor_scheduler.hpp"
#include "core/bot/messaging_client.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancellation_state.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/scope/scope_exit.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace obcx::core {
namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;

struct AsioValueProbe {
  std::mutex mutex;
  int value = 0;
  std::thread::id before_thread;
  std::thread::id after_thread;
};

class ImmediateAsioValueActor final : public IActorV2 {
public:
  ImmediateAsioValueActor(asio::any_io_executor executor,
                          std::shared_ptr<AsioValueProbe> probe)
      : executor_(std::move(executor)), probe_(std::move(probe)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-value";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->before_thread = std::this_thread::get_id();
    }
    const auto value = co_await context.await_asio(
        executor_, []() -> asio::awaitable<int> { co_return 42; });
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->value = value;
      probe_->after_thread = std::this_thread::get_id();
    }
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
  std::shared_ptr<AsioValueProbe> probe_;
};

class CancellableAsioActor final : public IActorV2 {
public:
  CancellableAsioActor(asio::any_io_executor executor,
                       std::shared_ptr<std::atomic_bool> cancelled)
      : executor_(std::move(executor)), cancelled_(std::move(cancelled)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-cancellable";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    co_await context.await_asio(
        executor_,
        [executor = executor_,
         cancelled = cancelled_]() -> asio::awaitable<void> {
          asio::steady_timer timer(executor);
          timer.expires_after(30s);
          try {
            co_await timer.async_wait(asio::use_awaitable);
          } catch (const boost::system::system_error &error) {
            if (error.code() == asio::error::operation_aborted) {
              cancelled->store(true, std::memory_order_release);
            }
            throw;
          }
        });
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
  std::shared_ptr<std::atomic_bool> cancelled_;
};

class DelayedVoidAsioActor final : public IActorV2 {
public:
  explicit DelayedVoidAsioActor(asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-delayed-void";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    co_await context.await_asio(
        executor_, [executor = executor_]() -> asio::awaitable<void> {
          asio::steady_timer timer(executor);
          timer.expires_after(200ms);
          co_await timer.async_wait(asio::use_awaitable);
        });
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
};

class ImmediateActor final : public IActorV2 {
public:
  [[nodiscard]] auto get_name() const -> std::string override {
    return "immediate";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &)
      -> ActorTask<ActorResult> override {
    co_return ActorResult::success();
  }
};

struct BlockingActorProbe {
  std::mutex mutex;
  std::condition_variable changed;
  std::thread::id actor_before;
  std::thread::id actor_after;
  std::thread::id blocking_thread;
  std::vector<std::string> events;
  bool blocking_started = false;
  bool blocking_released = false;
};

class BlockingActor final : public IActorV2 {
public:
  explicit BlockingActor(std::shared_ptr<BlockingActorProbe> probe,
                         std::shared_ptr<std::atomic_size_t> destructions = {})
      : probe_(std::move(probe)), destructions_(std::move(destructions)) {}

  ~BlockingActor() override {
    if (destructions_) {
      destructions_->fetch_add(1, std::memory_order_release);
    }
  }

  [[nodiscard]] auto get_name() const -> std::string override {
    return "blocking";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &message, ActorContext &context)
      -> ActorTask<ActorResult> override {
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("start:" + message.id);
    }
    if (message.payload.value("block", false)) {
      {
        std::scoped_lock lock(probe_->mutex);
        probe_->actor_before = std::this_thread::get_id();
      }
      const auto value = co_await context.run_blocking([probe = probe_] {
        std::unique_lock lock(probe->mutex);
        probe->blocking_thread = std::this_thread::get_id();
        probe->blocking_started = true;
        probe->changed.notify_all();
        probe->changed.wait(lock,
                            [&probe] { return probe->blocking_released; });
        return 42;
      });
      {
        std::scoped_lock lock(probe_->mutex);
        probe_->actor_after = std::this_thread::get_id();
        probe_->events.push_back("value:" + std::to_string(value));
      }
    }
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->events.push_back("finish:" + message.id);
      probe_->changed.notify_all();
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<BlockingActorProbe> probe_;
  std::shared_ptr<std::atomic_size_t> destructions_;
};

class BlockingExceptionActor final : public IActorV2 {
public:
  explicit BlockingExceptionActor(std::shared_ptr<std::string> observed)
      : observed_(std::move(observed)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "blocking-exception";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    try {
      co_await context.run_blocking(
          []() -> void { throw std::runtime_error("blocking actor failure"); });
    } catch (const std::runtime_error &error) {
      *observed_ = error.what();
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<std::string> observed_;
};

struct BlockingImmediateProbe {
  std::mutex mutex;
  std::thread::id actor_before;
  std::thread::id actor_after;
  std::vector<std::thread::id> blocking_threads;
  int value = 0;
  int void_calls = 0;
};

class BlockingImmediateActor final : public IActorV2 {
public:
  explicit BlockingImmediateActor(std::shared_ptr<BlockingImmediateProbe> probe)
      : probe_(std::move(probe)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "blocking-immediate";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->actor_before = std::this_thread::get_id();
    }
    co_await context.run_blocking([probe = probe_] {
      std::scoped_lock lock(probe->mutex);
      probe->blocking_threads.push_back(std::this_thread::get_id());
      ++probe->void_calls;
    });
    const auto value = co_await context.run_blocking([probe = probe_] {
      std::scoped_lock lock(probe->mutex);
      probe->blocking_threads.push_back(std::this_thread::get_id());
      return 42;
    });
    {
      std::scoped_lock lock(probe_->mutex);
      probe_->value = value;
      probe_->actor_after = std::this_thread::get_id();
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<BlockingImmediateProbe> probe_;
};

struct MissingBlockingServiceProbe {
  std::atomic_bool unavailable = false;
  std::atomic_bool callable_ran = false;
};

class MissingBlockingServiceActor final : public IActorV2 {
public:
  explicit MissingBlockingServiceActor(
      std::shared_ptr<MissingBlockingServiceProbe> probe)
      : probe_(std::move(probe)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "missing-blocking-service";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    try {
      co_await context.run_blocking([probe = probe_] {
        probe->callable_ran.store(true, std::memory_order_release);
      });
    } catch (const BlockingExecutorUnavailable &) {
      probe_->unavailable.store(true, std::memory_order_release);
    }
    co_return ActorResult::success();
  }

private:
  std::shared_ptr<MissingBlockingServiceProbe> probe_;
};

class AsioExceptionActor final : public IActorV2 {
public:
  AsioExceptionActor(asio::any_io_executor executor,
                     std::shared_ptr<std::string> observed)
      : executor_(std::move(executor)), observed_(std::move(observed)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-exception";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    try {
      (void)co_await context.await_asio(
          executor_, []() -> asio::awaitable<int> {
            throw std::runtime_error("synthetic Asio failure");
            co_return 0;
          });
    } catch (const std::runtime_error &error) {
      *observed_ = error.what();
    }
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
  std::shared_ptr<std::string> observed_;
};

class NonCancellableAsioActor final : public IActorV2 {
public:
  NonCancellableAsioActor(asio::any_io_executor executor,
                          std::shared_ptr<std::atomic_bool> completed)
      : executor_(std::move(executor)), completed_(std::move(completed)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-noncancellable";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    co_await context.await_asio(
        executor_,
        [executor = executor_,
         completed = completed_]() -> asio::awaitable<void> {
          co_await asio::this_coro::reset_cancellation_state(
              asio::disable_cancellation());
          asio::steady_timer timer(executor);
          timer.expires_after(30ms);
          co_await timer.async_wait(asio::use_awaitable);
          completed->store(true, std::memory_order_release);
        });
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
  std::shared_ptr<std::atomic_bool> completed_;
};

class TimedAsioActor final : public IActorV2 {
public:
  TimedAsioActor(asio::any_io_executor executor,
                 std::chrono::microseconds delay)
      : executor_(std::move(executor)), delay_(delay) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-timed";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    const auto value = co_await context.await_asio(
        executor_,
        [executor = executor_, delay = delay_]() -> asio::awaitable<int> {
          asio::steady_timer timer(executor);
          timer.expires_after(delay);
          co_await timer.async_wait(asio::use_awaitable);
          co_return 7;
        });
    if (value != 7) {
      co_return ActorResult::failed("unexpected_value",
                                    "timed Asio result changed", false);
    }
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
  std::chrono::microseconds delay_;
};

class DirectTokenAsioActor final : public IActorV2 {
public:
  DirectTokenAsioActor(asio::any_io_executor executor,
                       std::shared_ptr<std::atomic_bool> completed)
      : executor_(std::move(executor)), completed_(std::move(completed)) {}

  [[nodiscard]] auto get_name() const -> std::string override {
    return "asio-direct-token";
  }
  [[nodiscard]] auto get_version() const -> std::string override {
    return "test";
  }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    asio::steady_timer timer(executor_);
    timer.expires_after(5ms);
    co_await timer.async_wait(context.asio_token(executor_));
    completed_->store(true, std::memory_order_release);
    co_return ActorResult::success();
  }

private:
  asio::any_io_executor executor_;
  std::shared_ptr<std::atomic_bool> completed_;
};

auto unbound_generic_asio_task(ActorContext &context,
                               asio::any_io_executor executor)
    -> ActorTask<ActorResult> {
  (void)co_await context.await_asio(
      executor, []() -> asio::awaitable<int> { co_return 42; });
  co_return ActorResult::success();
}

auto unbound_direct_asio_task(ActorContext &context,
                              asio::any_io_executor executor)
    -> ActorTask<ActorResult> {
  asio::steady_timer timer(executor);
  timer.expires_after(1ms);
  co_await timer.async_wait(context.asio_token(executor));
  co_return ActorResult::success();
}

struct GatewayLifetimeProbe {
  std::shared_ptr<asio::steady_timer> timer;
  std::atomic_bool entered{false};
  std::atomic_bool finished{false};
  std::atomic_bool request_valid{false};
  std::atomic_bool actor_continued{false};
  std::atomic_uint actor_destructions{0};
};

class LifetimeGateway final : public bot::BotOperationGateway {
public:
  LifetimeGateway(std::shared_ptr<GatewayLifetimeProbe> probe,
                  bool ignore_cancel)
      : probe_(std::move(probe)), ignore_cancel_(ignore_cancel) {}

  auto supported_actions(const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedActions> override {
    return bot::BotOperationResult<bot::SupportedActions>::success(
        {.installation = installation,
         .actions = {bot::SendGroupMessageRequest::action}});
  }

  auto invoke(bot::OperationEnvelope envelope)
      -> asio::awaitable<bot::OperationReply> override {
    if (ignore_cancel_) {
      co_await asio::this_coro::reset_cancellation_state(
          asio::disable_cancellation());
    }
    probe_->entered.store(true, std::memory_order_release);
    boost::system::error_code error;
    co_await probe_->timer->async_wait(
        asio::redirect_error(asio::use_awaitable, error));
    // A cancellation-resistant provider may still complete after actor
    // shutdown. Its owned request must remain intact, but it must not republish
    // the actor.
    envelope.validate();
    const auto request = envelope.payload.get<bot::SendGroupMessageRequest>();
    probe_->request_valid.store(
        request.target.installation == envelope.installation &&
            request.message.front().data.at("text") == "owned request",
        std::memory_order_release);
    probe_->finished.store(true, std::memory_order_release);
    co_return bot::OperationReply::success(bot::Json(bot::SendMessageResult{
        .messages = {{.group = request.target, .native_message_id = "42"}}}));
  }

private:
  std::shared_ptr<GatewayLifetimeProbe> probe_;
  bool ignore_cancel_;
};

class GatewayAsioActor final : public IActorV2 {
public:
  GatewayAsioActor(asio::any_io_executor executor,
                   std::shared_ptr<bot::BotOperationGateway> gateway,
                   std::shared_ptr<GatewayLifetimeProbe> probe)
      : executor_(std::move(executor)), gateway_(std::move(gateway)),
        probe_(std::move(probe)) {}
  ~GatewayAsioActor() override { ++probe_->actor_destructions; }
  auto get_name() const -> std::string override { return "gateway-asio"; }
  auto get_version() const -> std::string override { return "test"; }

  auto handle_message(const MessageEnvelope &, ActorContext &context)
      -> ActorTask<ActorResult> override {
    const auto result = co_await context.await_asio(
        executor_,
        [gateway = gateway_]()
            -> asio::awaitable<
                bot::BotOperationResult<bot::SendMessageResult>> {
          bot::MessagingClient client{*gateway};
          co_return co_await client.execute(bot::SendGroupMessageRequest{
              .target = {.installation = {.installation_id = "fixture",
                                          .surface =
                                              bot::SurfaceId{"test.echo"}},
                         .native_group_id = "group-1"},
              .message = {
                  {.type = "text", .data = {{"text", "owned request"}}}}});
        });
    probe_->actor_continued.store(true, std::memory_order_release);
    co_return result.ok()
        ? ActorResult::success()
        : ActorResult::failed("gateway_failed", "typed operation failed",
                              false);
  }

private:
  asio::any_io_executor executor_;
  std::shared_ptr<bot::BotOperationGateway> gateway_;
  std::shared_ptr<GatewayLifetimeProbe> probe_;
};

template <typename Predicate>
auto wait_until(Predicate predicate,
                const std::chrono::steady_clock::duration timeout = 2s)
    -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

TEST(ActorAsioInteropTest,
     ImmediateValueReturnsBySchedulerInsteadOfResumingOnIoThread) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::promise<std::thread::id> io_thread_id_promise;
  auto io_thread_id = io_thread_id_promise.get_future();
  std::thread io_thread([&] {
    io_thread_id_promise.set_value(std::this_thread::get_id());
    io.run();
  });

  auto probe = std::make_shared<AsioValueProbe>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(
      std::make_shared<ImmediateAsioValueActor>(io.get_executor(), probe));

  MessageEnvelope message;
  message.id = "immediate-value";
  message.type = "AsioInteropTest";
  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  ASSERT_TRUE(scheduler.enqueue(ActorInvocation{.actor_id = "asio-value",
                                                .partition_key = "same",
                                                .message = std::move(message)},
                                [&completion](ActorResult actor_result) {
                                  completion.set_value(std::move(actor_result));
                                }));

  ASSERT_TRUE(result.get().ok());
  scheduler.shutdown();
  work.reset();
  io.stop();
  io_thread.join();

  std::scoped_lock lock(probe->mutex);
  EXPECT_EQ(probe->value, 42);
  EXPECT_NE(probe->before_thread, std::thread::id{});
  EXPECT_NE(probe->after_thread, std::thread::id{});
  EXPECT_NE(probe->after_thread, io_thread_id.get());
}

TEST(ActorAsioInteropTest, AsioAwaitWithoutRuntimeFailsImmediately) {
  asio::io_context io;
  ActorContext context{"unbound"};

  auto generic = unbound_generic_asio_task(context, io.get_executor());
  generic.resume();
  ASSERT_TRUE(generic.done());
  EXPECT_THROW((void)generic.take_result(), std::logic_error);

  auto direct = unbound_direct_asio_task(context, io.get_executor());
  direct.resume();
  ASSERT_TRUE(direct.done());
  EXPECT_THROW((void)direct.take_result(), std::logic_error);
  EXPECT_EQ(io.poll(), 0);
}

TEST(ActorAsioInteropTest, SchedulerCancellationReachesAsioOperation) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  auto asio_cancelled = std::make_shared<std::atomic_bool>(false);
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::make_shared<CancellableAsioActor>(
      io.get_executor(), asio_cancelled));

  MessageEnvelope message;
  message.id = "cancel-asio";
  message.type = "AsioInteropTest";
  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  ASSERT_TRUE(scheduler.enqueue(ActorInvocation{.actor_id = "asio-cancellable",
                                                .partition_key = "same",
                                                .message = std::move(message)},
                                [&completion](ActorResult actor_result) {
                                  completion.set_value(std::move(actor_result));
                                }));
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
  ASSERT_TRUE(result.get().failure.has_value());
  EXPECT_TRUE(wait_until([&asio_cancelled] {
    return asio_cancelled->load(std::memory_order_acquire);
  }));

  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest, DelayedVoidOperationReleasesTheOnlyActorWorker) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(
      std::make_shared<DelayedVoidAsioActor>(io.get_executor()));
  scheduler.register_actor(std::make_shared<ImmediateActor>());

  std::promise<ActorResult> delayed_promise;
  auto delayed = delayed_promise.get_future();
  MessageEnvelope delayed_message;
  delayed_message.id = "delayed";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "asio-delayed-void",
                                        .partition_key = "delayed",
                                        .message = std::move(delayed_message)},
                        [&delayed_promise](ActorResult result) {
                          delayed_promise.set_value(std::move(result));
                        }));
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  std::promise<ActorResult> immediate_promise;
  auto immediate = immediate_promise.get_future();
  MessageEnvelope immediate_message;
  immediate_message.id = "immediate";
  ASSERT_TRUE(scheduler.enqueue(
      ActorInvocation{.actor_id = "immediate",
                      .partition_key = "immediate",
                      .message = std::move(immediate_message)},
      [&immediate_promise](ActorResult result) {
        immediate_promise.set_value(std::move(result));
      }));

  ASSERT_EQ(immediate.wait_for(100ms), std::future_status::ready);
  EXPECT_TRUE(immediate.get().ok());
  EXPECT_EQ(delayed.wait_for(20ms), std::future_status::timeout);
  EXPECT_TRUE(delayed.get().ok());
  scheduler.shutdown();

  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest, AsioExceptionIsRethrownInsideActorTask) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  auto observed = std::make_shared<std::string>();
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(
      std::make_shared<AsioExceptionActor>(io.get_executor(), observed));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  MessageEnvelope message;
  message.id = "exception";
  ASSERT_TRUE(scheduler.enqueue(ActorInvocation{.actor_id = "asio-exception",
                                                .partition_key = "same",
                                                .message = std::move(message)},
                                [&completion](ActorResult actor_result) {
                                  completion.set_value(std::move(actor_result));
                                }));

  EXPECT_TRUE(result.get().ok());
  EXPECT_EQ(*observed, "synthetic Asio failure");
  scheduler.shutdown();

  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest,
     BlockingAwaitReleasesWorkerButPreservesPartitionExclusivity) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });
  auto blocking_executor = std::make_shared<BlockingExecutor>(1);
  auto io_executor = std::make_shared<asio::any_io_executor>(io.get_executor());
  auto probe = std::make_shared<BlockingActorProbe>();

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_service<BlockingExecutor>(blocking_executor);
  scheduler.register_service<asio::any_io_executor>(io_executor);
  scheduler.register_actor(std::make_shared<BlockingActor>(probe));

  std::promise<ActorResult> blocked_promise;
  auto blocked = blocked_promise.get_future();
  MessageEnvelope blocked_message;
  blocked_message.id = "blocked";
  blocked_message.payload["block"] = true;
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "blocking",
                                        .partition_key = "same",
                                        .message = std::move(blocked_message)},
                        [&blocked_promise](ActorResult result) {
                          blocked_promise.set_value(std::move(result));
                        }));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->blocking_started; }));
  }
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  std::promise<ActorResult> same_promise;
  auto same = same_promise.get_future();
  MessageEnvelope same_message;
  same_message.id = "same";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "blocking",
                                        .partition_key = "same",
                                        .message = std::move(same_message)},
                        [&same_promise](ActorResult result) {
                          same_promise.set_value(std::move(result));
                        }));

  std::promise<ActorResult> other_promise;
  auto other = other_promise.get_future();
  MessageEnvelope other_message;
  other_message.id = "other";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "blocking",
                                        .partition_key = "other",
                                        .message = std::move(other_message)},
                        [&other_promise](ActorResult result) {
                          other_promise.set_value(std::move(result));
                        }));

  ASSERT_EQ(other.wait_for(200ms), std::future_status::ready);
  EXPECT_TRUE(other.get().ok());
  EXPECT_EQ(same.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(blocked.wait_for(20ms), std::future_status::timeout);

  {
    std::scoped_lock lock(probe->mutex);
    probe->blocking_released = true;
    probe->changed.notify_all();
  }
  EXPECT_TRUE(blocked.get().ok());
  EXPECT_TRUE(same.get().ok());
  scheduler.shutdown();
  blocking_executor->shutdown();
  work.reset();
  io.stop();
  io_thread.join();

  std::scoped_lock lock(probe->mutex);
  EXPECT_NE(probe->actor_before, std::thread::id{});
  EXPECT_NE(probe->actor_after, std::thread::id{});
  EXPECT_NE(probe->blocking_thread, std::thread::id{});
  EXPECT_NE(probe->actor_before, probe->blocking_thread);
  EXPECT_NE(probe->actor_after, probe->blocking_thread);
  const auto same_start =
      std::ranges::find(probe->events, std::string{"start:same"});
  const auto blocked_finish =
      std::ranges::find(probe->events, std::string{"finish:blocked"});
  ASSERT_NE(same_start, probe->events.end());
  ASSERT_NE(blocked_finish, probe->events.end());
  EXPECT_LT(blocked_finish, same_start);
}

TEST(ActorAsioInteropTest, BlockingExceptionIsRethrownInsideActorTask) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });
  auto blocking_executor = std::make_shared<BlockingExecutor>(1);
  auto io_executor = std::make_shared<asio::any_io_executor>(io.get_executor());
  auto observed = std::make_shared<std::string>();

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_service<BlockingExecutor>(blocking_executor);
  scheduler.register_service<asio::any_io_executor>(io_executor);
  scheduler.register_actor(std::make_shared<BlockingExceptionActor>(observed));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  MessageEnvelope message;
  message.id = "blocking-exception";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "blocking-exception",
                                        .partition_key = "same",
                                        .message = std::move(message)},
                        [&completion](ActorResult actor_result) {
                          completion.set_value(std::move(actor_result));
                        }));

  EXPECT_TRUE(result.get().ok());
  EXPECT_EQ(*observed, "blocking actor failure");
  scheduler.shutdown();
  blocking_executor->shutdown();
  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest,
     ImmediateBlockingValueAndVoidResumeOnlyOnActorWorker) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });
  auto blocking_executor = std::make_shared<BlockingExecutor>(1);
  auto io_executor = std::make_shared<asio::any_io_executor>(io.get_executor());
  auto probe = std::make_shared<BlockingImmediateProbe>();

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_service<BlockingExecutor>(blocking_executor);
  scheduler.register_service<asio::any_io_executor>(io_executor);
  scheduler.register_actor(std::make_shared<BlockingImmediateActor>(probe));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  MessageEnvelope message;
  message.id = "blocking-immediate";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "blocking-immediate",
                                        .partition_key = "same",
                                        .message = std::move(message)},
                        [&completion](ActorResult actor_result) {
                          completion.set_value(std::move(actor_result));
                        }));

  EXPECT_TRUE(result.get().ok());
  scheduler.shutdown();
  blocking_executor->shutdown();
  work.reset();
  io.stop();
  io_thread.join();

  std::scoped_lock lock(probe->mutex);
  EXPECT_EQ(probe->void_calls, 1);
  EXPECT_EQ(probe->value, 42);
  ASSERT_EQ(probe->blocking_threads.size(), 2U);
  EXPECT_NE(probe->actor_before, std::thread::id{});
  EXPECT_NE(probe->actor_after, std::thread::id{});
  for (const auto blocking_thread : probe->blocking_threads) {
    EXPECT_NE(blocking_thread, probe->actor_before);
    EXPECT_NE(blocking_thread, probe->actor_after);
  }
}

TEST(ActorAsioInteropTest,
     MissingBlockingServiceFailsWithoutExecutingCallable) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });
  auto io_executor = std::make_shared<asio::any_io_executor>(io.get_executor());
  auto probe = std::make_shared<MissingBlockingServiceProbe>();

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_service<asio::any_io_executor>(io_executor);
  scheduler.register_actor(
      std::make_shared<MissingBlockingServiceActor>(probe));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  MessageEnvelope message;
  message.id = "missing-blocking-service";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "missing-blocking-service",
                                        .partition_key = "same",
                                        .message = std::move(message)},
                        [&completion](ActorResult actor_result) {
                          completion.set_value(std::move(actor_result));
                        }));

  EXPECT_TRUE(result.get().ok());
  EXPECT_TRUE(probe->unavailable.load(std::memory_order_acquire));
  EXPECT_FALSE(probe->callable_ran.load(std::memory_order_acquire));
  scheduler.shutdown();
  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest,
     CancelledBlockingWorkRetainsActorUntilCallableRetires) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });
  auto blocking_executor = std::make_shared<BlockingExecutor>(1);
  auto io_executor = std::make_shared<asio::any_io_executor>(io.get_executor());
  auto probe = std::make_shared<BlockingActorProbe>();
  auto destructions = std::make_shared<std::atomic_size_t>(0);

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_service<BlockingExecutor>(blocking_executor);
  scheduler.register_service<asio::any_io_executor>(io_executor);
  scheduler.register_actor(
      std::make_shared<BlockingActor>(probe, destructions));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  MessageEnvelope message;
  message.id = "cancelled-blocking";
  message.payload["block"] = true;
  ASSERT_TRUE(scheduler.enqueue(ActorInvocation{.actor_id = "blocking",
                                                .partition_key = "same",
                                                .message = std::move(message)},
                                [&completion](ActorResult actor_result) {
                                  completion.set_value(std::move(actor_result));
                                }));
  {
    std::unique_lock lock(probe->mutex);
    ASSERT_TRUE(probe->changed.wait_for(
        lock, 2s, [&probe] { return probe->blocking_started; }));
  }

  scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
  ASSERT_FALSE(result.get().ok());
  scheduler.release_actors();
  EXPECT_EQ(destructions->load(std::memory_order_acquire), 0);

  {
    std::scoped_lock lock(probe->mutex);
    probe->blocking_released = true;
    probe->changed.notify_all();
  }
  blocking_executor->shutdown();
  ASSERT_TRUE(wait_until([&destructions] {
    return destructions->load(std::memory_order_acquire) == 1;
  }));

  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest,
     NonCancellableLateCompletionCannotRepublishAbandonedTask) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  auto underlying_completed = std::make_shared<std::atomic_bool>(false);
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(std::make_shared<NonCancellableAsioActor>(
      io.get_executor(), underlying_completed));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  std::atomic_int completions = 0;
  MessageEnvelope message;
  message.id = "late";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "asio-noncancellable",
                                        .partition_key = "same",
                                        .message = std::move(message)},
                        [&completion, &completions](ActorResult actor_result) {
                          completions.fetch_add(1);
                          completion.set_value(std::move(actor_result));
                        }));
  ASSERT_TRUE(wait_until(
      [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

  scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
  ASSERT_TRUE(result.get().failure.has_value());
  ASSERT_TRUE(wait_until([&underlying_completed] {
    return underlying_completed->load(std::memory_order_acquire);
  }));
  EXPECT_EQ(completions.load(), 1);
  EXPECT_EQ(scheduler.metrics().pending, 0);

  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest,
     SeededCancellationVersusSuccessPublishesOneActorResult) {
  constexpr uint32_t seed = 0x4153494fU;
  std::minstd_rand random(seed);
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  for (size_t iteration = 0; iteration < 50; ++iteration) {
    SCOPED_TRACE(::testing::Message()
                 << "seed=" << seed << " iteration=" << iteration);
    NativeActorScheduler scheduler(
        NativeActorSchedulerOptions{.worker_count = 2});
    scheduler.register_actor(std::make_shared<TimedAsioActor>(
        io.get_executor(), std::chrono::milliseconds(5)));

    std::promise<ActorResult> completion;
    auto result = completion.get_future();
    std::atomic_int completions = 0;
    MessageEnvelope message;
    message.id = "asio-race-" + std::to_string(iteration);
    ASSERT_TRUE(scheduler.enqueue(
        ActorInvocation{.actor_id = "asio-timed",
                        .partition_key = "same",
                        .message = std::move(message)},
        [&completion, &completions](ActorResult actor_result) {
          completions.fetch_add(1);
          completion.set_value(std::move(actor_result));
        }));
    ASSERT_TRUE(wait_until(
        [&scheduler] { return scheduler.metrics().suspended_mailboxes == 1; }));

    std::this_thread::sleep_for(std::chrono::microseconds(random() % 10000));
    scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
    ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
    const auto actor_result = result.get();
    if (!actor_result.ok()) {
      ASSERT_TRUE(actor_result.failure.has_value());
      EXPECT_EQ(actor_result.failure->code, "scheduler_cancelled");
    }
    std::this_thread::sleep_for(1ms);
    EXPECT_EQ(completions.load(), 1);
    EXPECT_EQ(scheduler.metrics().pending, 0);
  }

  work.reset();
  io.stop();
  io_thread.join();
}

TEST(ActorAsioInteropTest,
     TypedGatewayCancellationRetainsRequestAndCannotRepublishActor) {
  for (const auto ignore_cancel : {false, true}) {
    SCOPED_TRACE(ignore_cancel ? "late completion" : "cancellable provider");
    asio::io_context io;
    auto work = asio::make_work_guard(io);
    auto probe = std::make_shared<GatewayLifetimeProbe>();
    probe->timer = std::make_shared<asio::steady_timer>(io);
    probe->timer->expires_after(30s);
    std::thread io_thread([&] { io.run(); });
    NativeActorScheduler scheduler(
        NativeActorSchedulerOptions{.worker_count = 1});
    boost::scope::scope_exit cleanup([&] {
      asio::post(io, [timer = probe->timer] { timer->cancel(); });
      scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
      scheduler.release_actors();
      work.reset();
      io.stop();
      io_thread.join();
    });
    auto gateway = std::make_shared<LifetimeGateway>(probe, ignore_cancel);
    const std::weak_ptr<bot::BotOperationGateway> weak_gateway = gateway;
    scheduler.register_actor(
        std::make_shared<GatewayAsioActor>(io.get_executor(), gateway, probe));
    gateway.reset();
    const auto completions = std::make_shared<std::atomic_uint>(0);
    const auto completion = std::make_shared<std::promise<ActorResult>>();
    auto result = completion->get_future();
    MessageEnvelope message;
    message.id = "gateway-lifetime";
    ASSERT_TRUE(
        scheduler.enqueue(ActorInvocation{.actor_id = "gateway-asio",
                                          .partition_key = "same",
                                          .message = std::move(message)},
                          [completions, completion](ActorResult value) {
                            if (completions->fetch_add(1) == 0) {
                              completion->set_value(std::move(value));
                            }
                          }));
    ASSERT_TRUE(wait_until([&] {
      return probe->entered.load(std::memory_order_acquire) &&
             scheduler.metrics().suspended_mailboxes == 1;
    }));
    scheduler.shutdown(ActorExecutorShutdownMode::Cancel);
    ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(result.get().ok());
    scheduler.release_actors();
    if (ignore_cancel) {
      EXPECT_EQ(probe->actor_destructions.load(), 0U);
      EXPECT_FALSE(weak_gateway.expired());
      asio::post(io, [timer = probe->timer] { timer->cancel(); });
    }
    ASSERT_TRUE(wait_until([&] {
      return probe->finished.load(std::memory_order_acquire) &&
             weak_gateway.expired();
    }));
    EXPECT_TRUE(probe->request_valid.load());
    EXPECT_FALSE(probe->actor_continued.load());
    EXPECT_EQ(probe->actor_destructions.load(), 1U);
    EXPECT_EQ(completions->load(), 1U);
    EXPECT_EQ(scheduler.metrics().pending, 0);
  }
}

TEST(ActorAsioInteropTest, DirectCompletionTokenMatchesGenericTimerBoundary) {
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  std::thread io_thread([&] { io.run(); });

  auto direct_completed = std::make_shared<std::atomic_bool>(false);
  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 1});
  scheduler.register_actor(std::make_shared<DirectTokenAsioActor>(
      io.get_executor(), direct_completed));
  scheduler.register_actor(std::make_shared<TimedAsioActor>(
      io.get_executor(), std::chrono::milliseconds(5)));

  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  MessageEnvelope message;
  message.id = "direct-token";
  ASSERT_TRUE(scheduler.enqueue(ActorInvocation{.actor_id = "asio-direct-token",
                                                .partition_key = "same",
                                                .message = std::move(message)},
                                [&completion](ActorResult actor_result) {
                                  completion.set_value(std::move(actor_result));
                                }));

  std::promise<ActorResult> generic_completion;
  auto generic_result = generic_completion.get_future();
  MessageEnvelope generic_message;
  generic_message.id = "generic-token";
  ASSERT_TRUE(
      scheduler.enqueue(ActorInvocation{.actor_id = "asio-timed",
                                        .partition_key = "generic",
                                        .message = std::move(generic_message)},
                        [&generic_completion](ActorResult actor_result) {
                          generic_completion.set_value(std::move(actor_result));
                        }));

  ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
  ASSERT_EQ(generic_result.wait_for(2s), std::future_status::ready);
  const auto direct_result = result.get();
  const auto nested_result = generic_result.get();
  EXPECT_EQ(direct_result.ok(), nested_result.ok());
  EXPECT_TRUE(direct_result.ok());
  EXPECT_TRUE(direct_completed->load(std::memory_order_acquire));
  scheduler.shutdown();
  EXPECT_EQ(scheduler.metrics().pending, 0);

  work.reset();
  io.stop();
  io_thread.join();
}

} // namespace
} // namespace obcx::core
