#include "network/detail/websocket_write_queue.hpp"
#include "network/websocket_client.hpp"

#include <algorithm>
#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
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

namespace obcx::network::test {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;
using detail::WebsocketWriteQueue;

class ManualWriter final : public std::enable_shared_from_this<ManualWriter> {
public:
  explicit ManualWriter(asio::any_io_executor executor)
      : executor_(std::move(executor)) {}

  auto write(const std::string &message) -> asio::awaitable<void> {
    auto completion = std::make_shared<Completion>(executor_, 1);
    {
      std::scoped_lock lock(mutex_);
      messages_.push_back(message);
      completions_.push_back(completion);
      ++active_;
      max_active_ = std::max(max_active_, active_);
    }
    changed_.notify_all();

    auto failure = co_await completion->async_receive(asio::use_awaitable);
    {
      std::scoped_lock lock(mutex_);
      --active_;
    }
    changed_.notify_all();
    if (failure) {
      std::rethrow_exception(failure);
    }
  }

  auto wait_for_started(const std::size_t count,
                        const std::chrono::milliseconds watchdog = 2s) -> bool {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, watchdog,
                             [&] { return messages_.size() >= count; });
  }

  void succeed(const std::size_t index) { complete(index, {}); }

  void fail(const std::size_t index, std::string message) {
    complete(index,
             std::make_exception_ptr(std::runtime_error{std::move(message)}));
  }

  void cancel_active() {
    std::vector<std::shared_ptr<Completion>> completions;
    {
      std::scoped_lock lock(mutex_);
      completions = completions_;
    }
    for (const auto &completion : completions) {
      static_cast<void>(completion->try_send(
          boost::system::error_code{},
          std::make_exception_ptr(
              boost::system::system_error{asio::error::operation_aborted})));
    }
  }

  [[nodiscard]] auto messages() const -> std::vector<std::string> {
    std::scoped_lock lock(mutex_);
    return messages_;
  }

  [[nodiscard]] auto max_active() const -> std::size_t {
    std::scoped_lock lock(mutex_);
    return max_active_;
  }

private:
  using Completion = asio::experimental::concurrent_channel<void(
      boost::system::error_code, std::exception_ptr)>;

  void complete(const std::size_t index, std::exception_ptr failure) {
    std::shared_ptr<Completion> completion;
    {
      std::scoped_lock lock(mutex_);
      ASSERT_LT(index, completions_.size());
      completion = completions_[index];
    }
    ASSERT_TRUE(
        completion->try_send(boost::system::error_code{}, std::move(failure)));
  }

  asio::any_io_executor executor_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<std::string> messages_;
  std::vector<std::shared_ptr<Completion>> completions_;
  std::size_t active_{0};
  std::size_t max_active_{0};
};

class WebsocketWriteQueueTest : public ::testing::Test {
protected:
  void SetUp() override {
    work_.emplace(asio::make_work_guard(io_));
    writer_ = std::make_shared<ManualWriter>(io_.get_executor());
  }

  void start(const std::size_t capacity) {
    queue_ = std::make_shared<WebsocketWriteQueue>(
        io_.get_executor(), capacity,
        [writer = writer_](const std::string &message) {
          return writer->write(message);
        },
        [writer = writer_] { writer->cancel_active(); });
    asio::co_spawn(
        io_,
        [queue = queue_]() -> asio::awaitable<void> { co_await queue->run(); },
        asio::detached);
    thread_ = std::jthread([this] { io_.run(); });
  }

  void TearDown() override {
    if (queue_) {
      queue_->stop();
    }
    work_.reset();
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  auto send(std::string message) -> std::future<void> {
    return asio::co_spawn(io_, queue_->send(std::move(message)),
                          asio::use_future);
  }

  asio::io_context io_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_;
  std::shared_ptr<ManualWriter> writer_;
  std::shared_ptr<WebsocketWriteQueue> queue_;
  std::jthread thread_;
};

TEST_F(WebsocketWriteQueueTest, SerializesConcurrentWritesInFifoOrder) {
  start(4);
  auto first = send("first");
  auto second = send("second");
  auto third = send("third");

  ASSERT_TRUE(writer_->wait_for_started(1));
  EXPECT_EQ(writer_->max_active(), 1U);
  EXPECT_EQ(first.wait_for(0s), std::future_status::timeout);
  EXPECT_EQ(second.wait_for(0s), std::future_status::timeout);
  EXPECT_EQ(third.wait_for(0s), std::future_status::timeout);

  writer_->succeed(0);
  ASSERT_TRUE(writer_->wait_for_started(2));
  writer_->succeed(1);
  ASSERT_TRUE(writer_->wait_for_started(3));
  writer_->succeed(2);

  EXPECT_NO_THROW(first.get());
  EXPECT_NO_THROW(second.get());
  EXPECT_NO_THROW(third.get());
  EXPECT_EQ(writer_->messages(),
            (std::vector<std::string>{"first", "second", "third"}));
  EXPECT_EQ(writer_->max_active(), 1U);
}

TEST_F(WebsocketWriteQueueTest, SaturationBackpressuresWithoutDropping) {
  start(1);
  auto first = send("first");
  auto queued = send("queued");
  auto backpressured = send("backpressured");

  ASSERT_TRUE(writer_->wait_for_started(1));
  EXPECT_EQ(queued.wait_for(0s), std::future_status::timeout);
  EXPECT_EQ(backpressured.wait_for(0s), std::future_status::timeout);

  writer_->succeed(0);
  ASSERT_TRUE(writer_->wait_for_started(2));
  EXPECT_EQ(writer_->messages().back(), "queued");
  writer_->succeed(1);
  ASSERT_TRUE(writer_->wait_for_started(3));
  EXPECT_EQ(writer_->messages().back(), "backpressured");
  writer_->succeed(2);

  EXPECT_NO_THROW(first.get());
  EXPECT_NO_THROW(queued.get());
  EXPECT_NO_THROW(backpressured.get());
}

TEST_F(WebsocketWriteQueueTest,
       WriteFailureRetiresQueuedAndBackpressuredCalls) {
  start(1);
  auto active = send("active");
  auto queued = send("queued");
  auto backpressured = send("backpressured");

  ASSERT_TRUE(writer_->wait_for_started(1));
  writer_->fail(0, "controlled write failure");

  EXPECT_THROW(active.get(), std::runtime_error);
  EXPECT_THROW(queued.get(), std::runtime_error);
  EXPECT_THROW(backpressured.get(), std::runtime_error);
  EXPECT_TRUE(queue_->stopped());
  EXPECT_EQ(writer_->messages(), (std::vector<std::string>{"active"}));
}

TEST_F(WebsocketWriteQueueTest, StopCancelsEveryWaiterExactlyOnce) {
  start(1);
  auto active = send("active");
  auto queued = send("queued");
  auto backpressured = send("backpressured");

  ASSERT_TRUE(writer_->wait_for_started(1));
  queue_->stop();

  EXPECT_THROW(active.get(), boost::system::system_error);
  EXPECT_THROW(queued.get(), boost::system::system_error);
  EXPECT_THROW(backpressured.get(), boost::system::system_error);
  EXPECT_TRUE(queue_->stopped());
}

class LoopbackWebSocketServer final {
public:
  LoopbackWebSocketServer()
      : acceptor_(io_, {asio::ip::make_address("127.0.0.1"), 0}),
        received_(received_promise_.get_future()) {}

  void start() {
    asio::co_spawn(
        io_,
        [this]() -> asio::awaitable<void> {
          auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
          beast::websocket::stream<tcp::socket> stream{std::move(socket)};
          co_await stream.async_accept(asio::use_awaitable);
          beast::flat_buffer buffer;
          co_await stream.async_read(buffer, asio::use_awaitable);
          received_promise_.set_value(beast::buffers_to_string(buffer.data()));
          beast::error_code ignored;
          stream.close(beast::websocket::close_code::normal, ignored);
        },
        [this](std::exception_ptr failure) {
          if (failure) {
            try {
              received_promise_.set_exception(failure);
            } catch (...) {
            }
          }
        });
    thread_ = std::jthread([this] { io_.run(); });
  }

  ~LoopbackWebSocketServer() {
    io_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return acceptor_.local_endpoint().port();
  }

  auto received() -> std::future<std::string> && {
    return std::move(received_);
  }

private:
  asio::io_context io_;
  tcp::acceptor acceptor_;
  std::promise<std::string> received_promise_;
  std::future<std::string> received_;
  std::jthread thread_;
};

TEST(WebsocketLoopbackTest, HandshakeAndWriteUseExplicitCompletionSignals) {
  LoopbackWebSocketServer server;
  server.start();
  auto received = std::move(server).received();

  asio::io_context io;
  auto work = asio::make_work_guard(io);
  auto client = std::make_shared<WebsocketClient>(io);
  std::promise<void> connected_promise;
  auto connected = connected_promise.get_future();
  std::atomic_bool connection_reported{false};

  asio::co_spawn(io,
                 client->run("127.0.0.1", std::to_string(server.port()), "",
                             [&](const beast::error_code &error,
                                 const std::string &message) {
                               if (!error && message.empty() &&
                                   !connection_reported.exchange(true)) {
                                 connected_promise.set_value();
                               }
                             }),
                 asio::detached);
  std::jthread io_thread([&] { io.run(); });

  ASSERT_EQ(connected.wait_for(3s), std::future_status::ready);
  auto sent = asio::co_spawn(io, client->send("deterministic-loopback"),
                             asio::use_future);
  ASSERT_EQ(sent.wait_for(3s), std::future_status::ready);
  EXPECT_NO_THROW(sent.get());
  ASSERT_EQ(received.wait_for(3s), std::future_status::ready);
  EXPECT_EQ(received.get(), "deterministic-loopback");

  work.reset();
  io.stop();
  io_thread.join();
}

} // namespace
} // namespace obcx::network::test
