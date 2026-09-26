#include "onebot11/network/websocket/detail/action_request_tracker.hpp"

#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace obcx::network::detail::test {
namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;

class ManualDeadline final : public ActionDeadline {
public:
  explicit ManualDeadline(std::function<void()> expired)
      : expired_(std::move(expired)) {}

  void cancel() override { cancelled_.store(true, std::memory_order_release); }

  auto fire() -> bool {
    if (cancelled_.load(std::memory_order_acquire)) {
      return false;
    }
    bool expected = false;
    if (!fired_.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
      return false;
    }
    expired_();
    return true;
  }

  [[nodiscard]] auto cancelled() const -> bool {
    return cancelled_.load(std::memory_order_acquire);
  }

private:
  std::function<void()> expired_;
  std::atomic_bool cancelled_{false};
  std::atomic_bool fired_{false};
};

class ManualDeadlineFactory final {
public:
  auto make(asio::any_io_executor, std::chrono::milliseconds,
            std::function<void()> expired) -> std::shared_ptr<ActionDeadline> {
    auto deadline = std::make_shared<ManualDeadline>(std::move(expired));
    {
      std::scoped_lock lock(mutex_);
      deadlines_.push_back(deadline);
    }
    changed_.notify_all();
    return deadline;
  }

  auto wait_for(const std::size_t index,
                const std::chrono::milliseconds watchdog = 2s)
      -> std::shared_ptr<ManualDeadline> {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, watchdog,
                           [&] { return deadlines_.size() > index; })) {
      return {};
    }
    return deadlines_[index];
  }

private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<std::shared_ptr<ManualDeadline>> deadlines_;
};

class ActionRequestTrackerTest : public ::testing::Test {
protected:
  void SetUp() override {
    work_.emplace(asio::make_work_guard(io_));
    deadlines_ = std::make_shared<ManualDeadlineFactory>();
    tracker_ = std::make_shared<ActionRequestTracker>(
        io_.get_executor(),
        [factory = deadlines_](asio::any_io_executor executor,
                               const std::chrono::milliseconds timeout,
                               std::function<void()> expired) {
          return factory->make(std::move(executor), timeout,
                               std::move(expired));
        });
    thread_ = std::jthread([this] { io_.run(); });
  }

  void TearDown() override {
    tracker_.reset();
    work_.reset();
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  auto wait(const std::shared_ptr<PendingAction> &request)
      -> std::future<ActionTerminalOutcome> {
    return asio::co_spawn(io_, request->wait(), asio::use_future);
  }

  asio::io_context io_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_;
  std::shared_ptr<ManualDeadlineFactory> deadlines_;
  std::shared_ptr<ActionRequestTracker> tracker_;
  std::jthread thread_;
};

TEST_F(ActionRequestTrackerTest, MatchingResponseWinsAndDisarmsDeadline) {
  auto request = tracker_->start(101, 30s);
  auto deadline = deadlines_->wait_for(0);
  ASSERT_TRUE(deadline);
  auto completed = wait(request);

  EXPECT_TRUE(tracker_->respond(101, "bounded-response"));
  const auto outcome = completed.get();

  EXPECT_EQ(outcome.status, ActionTerminalStatus::Response);
  EXPECT_EQ(outcome.response, "bounded-response");
  EXPECT_TRUE(deadline->cancelled());
  EXPECT_FALSE(deadline->fire());
  EXPECT_EQ(tracker_->pending_count(), 0U);
}

TEST_F(ActionRequestTrackerTest, ExplicitDeadlineWinsAndLateResponseIsIgnored) {
  auto request = tracker_->start(202, 30s);
  auto deadline = deadlines_->wait_for(0);
  ASSERT_TRUE(deadline);
  auto completed = wait(request);

  ASSERT_TRUE(deadline->fire());
  const auto outcome = completed.get();

  EXPECT_EQ(outcome.status, ActionTerminalStatus::Timeout);
  EXPECT_FALSE(tracker_->respond(202, "late-response"));
  EXPECT_EQ(tracker_->pending_count(), 0U);
}

TEST_F(ActionRequestTrackerTest, ResponseTimeoutBoundaryCompletesOnlyOnce) {
  auto response_first = tracker_->start(301, 30s);
  auto first_deadline = deadlines_->wait_for(0);
  ASSERT_TRUE(first_deadline);
  auto first_result = wait(response_first);
  EXPECT_TRUE(tracker_->respond(301, "response-first"));
  EXPECT_FALSE(first_deadline->fire());
  EXPECT_EQ(first_result.get().status, ActionTerminalStatus::Response);

  auto timeout_first = tracker_->start(302, 30s);
  auto second_deadline = deadlines_->wait_for(1);
  ASSERT_TRUE(second_deadline);
  auto second_result = wait(timeout_first);
  EXPECT_TRUE(second_deadline->fire());
  EXPECT_FALSE(tracker_->respond(302, "response-second"));
  EXPECT_EQ(second_result.get().status, ActionTerminalStatus::Timeout);
  EXPECT_EQ(tracker_->pending_count(), 0U);
}

TEST_F(ActionRequestTrackerTest,
       TransportFailurePreservesExceptionAndCleansUp) {
  auto request = tracker_->start(401, 30s);
  auto deadline = deadlines_->wait_for(0);
  ASSERT_TRUE(deadline);
  auto completed = wait(request);

  EXPECT_TRUE(tracker_->fail(
      401, std::make_exception_ptr(std::runtime_error{"controlled failure"})));
  auto outcome = completed.get();

  EXPECT_EQ(outcome.status, ActionTerminalStatus::TransportFailure);
  ASSERT_TRUE(outcome.failure);
  EXPECT_THROW(std::rethrow_exception(outcome.failure), std::runtime_error);
  EXPECT_TRUE(deadline->cancelled());
  EXPECT_EQ(tracker_->pending_count(), 0U);
}

TEST_F(ActionRequestTrackerTest, CancelAllCompletesEveryPendingAction) {
  auto first = tracker_->start(501, 30s);
  auto second = tracker_->start(502, 30s);
  ASSERT_TRUE(deadlines_->wait_for(0));
  ASSERT_TRUE(deadlines_->wait_for(1));
  auto first_result = wait(first);
  auto second_result = wait(second);

  tracker_->cancel_all();

  EXPECT_EQ(first_result.get().status, ActionTerminalStatus::Cancelled);
  EXPECT_EQ(second_result.get().status, ActionTerminalStatus::Cancelled);
  EXPECT_EQ(tracker_->pending_count(), 0U);
  EXPECT_FALSE(tracker_->cancel(501));
}

TEST_F(ActionRequestTrackerTest, DestructionCancelsPendingAction) {
  auto request = tracker_->start(601, 30s);
  ASSERT_TRUE(deadlines_->wait_for(0));
  auto completed = wait(request);

  tracker_.reset();

  EXPECT_EQ(completed.get().status, ActionTerminalStatus::Cancelled);
}

TEST_F(ActionRequestTrackerTest, DuplicateEchoIsRejectedWithoutLosingOriginal) {
  auto original = tracker_->start(701, 30s);
  ASSERT_TRUE(deadlines_->wait_for(0));

  EXPECT_THROW(static_cast<void>(tracker_->start(701, 30s)),
               std::invalid_argument);
  EXPECT_EQ(tracker_->pending_count(), 1U);

  auto completed = wait(original);
  EXPECT_TRUE(tracker_->respond(701, "original"));
  EXPECT_EQ(completed.get().status, ActionTerminalStatus::Response);
}

} // namespace
} // namespace obcx::network::detail::test
