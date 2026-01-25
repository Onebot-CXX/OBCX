/**
 * @file websocket_queue_test.cpp
 * @brief 测试WebSocket写入队列机制
 *
 * 本测试验证在弱网环境下，WebSocket客户端的写入队列机制
 * 能够正确处理并发写入请求，避免Beast内部的竞争问题。
 */

#include "common/logger.hpp"
#include "network/websocket_client.hpp"

#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <queue>
#include <thread>

// NOLINTBEGIN

namespace beast = boost::beast;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace obcx::test {

constexpr size_t SERVER_STARTUP_DELAY = 1000;
constexpr size_t CONNECTION_ESTABLISH_DELAY = 500;
constexpr size_t CONCURRENT_WRITE_COUNT = 10;
constexpr size_t WEAK_NETWORK_WRITE_COUNT = 20;
constexpr size_t WEAK_NETWORK_DELAY_MS = 100;

inline uint16_t get_random_port() {
  static std::mt19937 gen(static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  static std::uniform_int_distribution<uint16_t> dist(40000, 65535);
  return dist(gen);
}

/**
 * 模拟弱网环境的WebSocket服务器
 */
class MockWebSocketServer {
public:
  MockWebSocketServer(const std::string &host, uint16_t port)
      : ioc_(), endpoint_(asio::ip::make_address(host), port),
        acceptor_(ioc_, endpoint_), work_guard_(asio::make_work_guard(ioc_)),
        accepting_(true) {
    acceptor_.set_option(asio::socket_base::reuse_address(true));
  }

  ~MockWebSocketServer() {
    if (thread_.joinable()) {
      join_and_stop();
    }
  }

  void start() {
    thread_ = std::thread([this]() {
      OBCX_DEBUG("服务器线程启动于 {}:{}", endpoint_.address().to_string(),
                 endpoint_.port());
      do_accept();
      ioc_.run();
      OBCX_DEBUG("服务器线程停止");
    });
  }

  void join_and_stop() {
    accepting_ = false;
    asio::post(ioc_, [this]() {
      OBCX_DEBUG("正在停止服务器...");
      acceptor_.close();
      if (ws_ && ws_->is_open()) {
        ws_->async_close(beast::websocket::close_code::normal,
                         [this](const boost::system::error_code &ec) {
                           work_guard_.reset();
                           ioc_.stop();
                         });
      } else {
        work_guard_.reset();
        ioc_.stop();
      }
    });

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  [[nodiscard]] auto get_port() const -> uint16_t { return endpoint_.port(); }

  [[nodiscard]] auto get_received_count() const -> size_t {
    return received_count_.load();
  }

private:
  void do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (!acceptor_.is_open() || !accepting_) {
        return;
      }
      if (!ec) {
        OBCX_DEBUG("接受到新连接");
        handle_websocket(std::move(socket));
      }
      do_accept();
    });
  }

  void handle_websocket(tcp::socket socket) {
    ws_ = std::make_shared<beast::websocket::stream<tcp::socket>>(
        std::move(socket));

    ws_->async_accept([this, ws = ws_](beast::error_code ec) {
      if (!ec) {
        start_read_loop(ws);
      }
    });
  }

  void start_read_loop(
      const std::shared_ptr<beast::websocket::stream<tcp::socket>> &ws) {
    auto buffer = std::make_shared<beast::flat_buffer>();

    ws->async_read(*buffer, [this, ws,
                             buffer](beast::error_code ec,
                                     std::size_t /*bytes_transferred*/) {
      if (ec == beast::websocket::error::closed ||
          ec == asio::error::operation_aborted) {
        return;
      }
      if (!ec) {
        received_count_++;
        std::string message = beast::buffers_to_string(buffer->data());
        OBCX_DEBUG("收到消息 #{}: {}", received_count_.load(), message);

        // 模拟弱网环境：延迟响应
        auto timer = std::make_shared<asio::steady_timer>(ioc_);
        timer->expires_after(std::chrono::milliseconds(WEAK_NETWORK_DELAY_MS));
        timer->async_wait([this, ws, timer](beast::error_code ec) {
          if (!ec) {
            queue_write("OK");
          }
        });

        start_read_loop(ws);
      }
    });
  }

  void queue_write(std::string message) {
    bool write_in_progress = !write_queue_.empty();
    write_queue_.push(std::move(message));

    if (!write_in_progress) {
      do_write();
    }
  }

  void do_write() {
    if (write_queue_.empty() || !ws_ || !ws_->is_open()) {
      return;
    }

    ws_->async_write(asio::buffer(write_queue_.front()),
                     [this](beast::error_code ec, std::size_t /*bytes*/) {
                       if (!ec) {
                         write_queue_.pop();
                         if (!write_queue_.empty()) {
                           do_write();
                         }
                       }
                     });
  }

  asio::io_context ioc_;
  tcp::endpoint endpoint_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::thread thread_;

  std::shared_ptr<beast::websocket::stream<tcp::socket>> ws_;
  std::atomic<bool> accepting_;
  std::atomic<size_t> received_count_{0};

  std::queue<std::string> write_queue_;
};

/**
 * WebSocket队列测试类
 */
class WebSocketQueueTest : public testing::Test {
protected:
  void SetUp() override {
    common::Logger::initialize(spdlog::level::trace);

    test_port_ = get_random_port();
    server_ = std::make_unique<MockWebSocketServer>("127.0.0.1", test_port_);
    server_->start();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(SERVER_STARTUP_DELAY));

    client_ = std::make_shared<network::WebsocketClient>(client_ioc_);
  }

  void TearDown() override {
    if (client_) {
      try {
        asio::co_spawn(
            client_ioc_,
            [this]() -> asio::awaitable<void> { co_await client_->close(); },
            asio::detached);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      } catch (...) {
      }
    }

    work_guard_.reset();
    client_ioc_.stop();
    if (client_thread_.joinable()) {
      client_thread_.join();
    }

    if (server_) {
      server_->join_and_stop();
    }
  }

  void start_client_ioc() {
    client_thread_ = std::thread([this]() {
      work_guard_.emplace(client_ioc_.get_executor());
      client_ioc_.run();
    });
  }

  void connect_to_server() {
    asio::co_spawn(
        client_ioc_,
        [this]() -> asio::awaitable<void> {
          co_await client_->run(
              "127.0.0.1", std::to_string(server_->get_port()), "",
              [](const beast::error_code &ec, const std::string &msg) {
                if (ec) {
                  if (ec != asio::error::operation_aborted) {
                    OBCX_ERROR("WebSocket错误: {}", ec.message());
                  }
                } else if (!msg.empty()) {
                  OBCX_DEBUG("收到消息: {}", msg);
                }
              });
        },
        asio::detached);

    std::this_thread::sleep_for(
        std::chrono::milliseconds(CONNECTION_ESTABLISH_DELAY));
  }

  asio::io_context client_ioc_;
  std::unique_ptr<MockWebSocketServer> server_;
  std::shared_ptr<network::WebsocketClient> client_;
  std::thread client_thread_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
  uint16_t test_port_{0};
};

/**
 * 测试并发写入
 */
TEST_F(WebSocketQueueTest, ConcurrentWrites) {
  start_client_ioc();
  connect_to_server();

  OBCX_INFO("开始测试并发写入...");

  std::atomic<size_t> success_count{0};
  std::atomic<size_t> error_count{0};
  std::vector<std::future<void>> futures;

  for (size_t i = 0; i < CONCURRENT_WRITE_COUNT; ++i) {
    std::promise<void> promise;
    futures.push_back(promise.get_future());

    asio::co_spawn(
        client_ioc_,
        [this, i, &success_count, &error_count,
         p = std::move(promise)]() mutable -> asio::awaitable<void> {
          std::string message = "消息 " + std::to_string(i);
          OBCX_DEBUG("发送消息: {}", message);

          try {
            co_await client_->send(message);
            success_count++;
            OBCX_DEBUG("消息 {} 发送成功", i);
          } catch (const std::exception &e) {
            error_count++;
            OBCX_ERROR("消息 {} 发送失败: {}", i, e.what());
          }
          p.set_value();
        },
        asio::detached);
  }

  // 等待所有任务完成
  for (auto &future : futures) {
    auto status = future.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "并发写入任务应在5秒内完成";
  }

  OBCX_INFO("并发写入完成: 成功={}, 失败={}", success_count.load(),
            error_count.load());

  EXPECT_EQ(success_count.load(), CONCURRENT_WRITE_COUNT)
      << "所有消息都应该发送成功";
  EXPECT_EQ(error_count.load(), 0) << "不应该有发送失败的消息";

  // 等待服务器接收完所有消息
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ(server_->get_received_count(), CONCURRENT_WRITE_COUNT)
      << "服务器应该接收到所有消息";
}

/**
 * 测试弱网环境下的连续写入
 */
TEST_F(WebSocketQueueTest, WeakNetworkWrites) {
  start_client_ioc();
  connect_to_server();

  OBCX_INFO("开始测试弱网环境下的写入...");

  std::atomic<size_t> success_count{0};
  std::atomic<size_t> error_count{0};
  std::promise<void> completion_promise;
  auto completion_future = completion_promise.get_future();

  asio::co_spawn(
      client_ioc_,
      [this, &success_count, &error_count,
       p = std::move(completion_promise)]() mutable -> asio::awaitable<void> {
        for (size_t i = 0; i < WEAK_NETWORK_WRITE_COUNT; ++i) {
          std::string message = "弱网测试消息 " + std::to_string(i);
          OBCX_DEBUG("发送弱网测试消息: {}", message);

          try {
            co_await client_->send(message);
            success_count++;
            OBCX_DEBUG("弱网测试消息 {} 发送成功", i);

            // 短暂等待，模拟业务逻辑
            co_await asio::steady_timer(co_await asio::this_coro::executor,
                                        std::chrono::milliseconds(10))
                .async_wait(asio::use_awaitable);
          } catch (const std::exception &e) {
            error_count++;
            OBCX_ERROR("弱网测试消息 {} 发送失败: {}", i, e.what());
          }
        }
        p.set_value();
      },
      asio::detached);

  // 等待测试完成
  auto status = completion_future.wait_for(std::chrono::seconds(10));
  ASSERT_EQ(status, std::future_status::ready) << "弱网测试应在10秒内完成";

  OBCX_INFO("弱网环境写入完成: 成功={}, 失败={}", success_count.load(),
            error_count.load());

  EXPECT_EQ(success_count.load(), WEAK_NETWORK_WRITE_COUNT)
      << "所有消息都应该发送成功";
  EXPECT_EQ(error_count.load(), 0) << "不应该有发送失败的消息";

  // 等待服务器接收完所有消息
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_EQ(server_->get_received_count(), WEAK_NETWORK_WRITE_COUNT)
      << "服务器应该接收到所有消息";
}

} // namespace obcx::test
// NOLINTEND
