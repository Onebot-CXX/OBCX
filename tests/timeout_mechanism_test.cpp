#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

#include "common/logger.hpp"
#include "common/message_type.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/network/websocket/connection_manager.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace obcx::test {

constexpr size_t SERVER_STARTUP_DELAY = 1000;
constexpr size_t CONNECTION_ESTABLISH_DELAY = 200;
constexpr size_t NORMAL_RESPONSE_DELAY = 100;
constexpr size_t DELAYED_RESPONSE_TIME = 3000;
// 客户端的默认超时时间，根据 TimeoutScenario 测试推断为30秒
constexpr std::chrono::seconds CLIENT_DEFAULT_TIMEOUT{5};
// 为测试用例设置一个比客户端默认超时更长的等待时间
constexpr std::chrono::seconds EXTENDED_TIMEOUT{CLIENT_DEFAULT_TIMEOUT +
                                                std::chrono::seconds(5)};
// 用于测试延迟响应的等待时间，应大于延迟但小于客户端默认超时
constexpr std::chrono::seconds DELAYED_WAIT_TIMEOUT{10};
constexpr uint64_t TEST_ECHO_1 = 12345;
constexpr uint64_t TEST_ECHO_2 = 54321;
constexpr uint64_t TEST_ECHO_3 = 67890;

/**
 * 模拟WebSocket服务器用于测试
 * (重写版本: 自包含，管理自己的线程和io_context)
 */
class MockWebSocketServer {
public:
  MockWebSocketServer(const std::string &host, uint16_t port)
      : ioc_(), endpoint_(asio::ip::make_address(host), port),
        acceptor_(ioc_, endpoint_), work_guard_(asio::make_work_guard(ioc_)) {
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
    asio::post(ioc_, [this]() {
      OBCX_DEBUG("正在停止服务器...");
      acceptor_.close();
      if (ws_ && ws_->is_open()) {
        ws_->async_close(websocket::close_code::normal,
                         [this](const boost::system::error_code &ec) {
                           work_guard_.reset();
                           ioc_.stop();
                         });
      }
    });

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void set_response_delay(size_t delay) { response_delay_ = delay; }

  void set_should_respond(bool should_respond) {
    should_respond_ = should_respond;
  }

  [[nodiscard]] auto get_port() const -> uint16_t { return endpoint_.port(); }

private:
  void do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (!acceptor_.is_open()) {
        return; // 服务器正在停止
      }
      if (!ec) {
        OBCX_DEBUG("接受到新连接");
        handle_websocket(std::move(socket));
      }
      do_accept(); // 继续监听下一个连接
    });
  }

  void handle_websocket(tcp::socket socket) {
    ws_ = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));

    ws_->async_accept([this, ws = ws_](beast::error_code ec) {
      if (!ec) {
        start_read_loop(ws);
      }
    });
  }

  void start_read_loop(
      const std::shared_ptr<websocket::stream<tcp::socket>> &ws) {
    auto buffer = std::make_shared<beast::flat_buffer>();

    ws->async_read(
        *buffer, [this, ws, buffer](beast::error_code ec,
                                    std::size_t /*bytes_transferred*/) {
          if (ec == websocket::error::closed ||
              ec == asio::error::operation_aborted) {
            return; // 连接关闭
          }
          if (!ec) {
            std::string message = beast::buffers_to_string(buffer->data());
            OBCX_DEBUG("收到消息: {}", message);
            handle_message(ws, message);
            start_read_loop(ws); // 继续读取
          }
        });
  }

  void handle_message(const std::shared_ptr<websocket::stream<tcp::socket>> &ws,
                      const std::string &message) {
    if (!should_respond_) {
      OBCX_DEBUG("配置为不响应");
      return;
    }

    try {
      nlohmann::json request = nlohmann::json::parse(message);
      uint64_t echo = request.value("echo", 0);

      nlohmann::json response;
      response["retcode"] = 0;
      response["status"] = "ok";
      response["data"] = {};
      response["echo"] = echo;

      std::string response_str = response.dump();

      if (response_delay_.load() > 0) {
        auto timer = std::make_shared<asio::steady_timer>(ioc_);
        timer->expires_after(std::chrono::milliseconds(response_delay_));
        timer->async_wait(
            [this, ws, response_str, timer, echo](beast::error_code ec) {
              if (!ec) {
                OBCX_DEBUG("延迟 {}ms 后发送响应 (echo: {})",
                           response_delay_.load(), echo);
                ws->async_write(asio::buffer(response_str), asio::detached);
              }
            });
      } else {
        OBCX_DEBUG("立即发送响应 (echo: {})", echo);
        ws->async_write(asio::buffer(response_str), asio::detached);
      }
    } catch (const nlohmann::json::parse_error &e) {
      OBCX_ERROR("JSON 解析错误: {}", e.what());
    }
  }

  asio::io_context ioc_;
  tcp::endpoint endpoint_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::thread thread_;

  std::shared_ptr<websocket::stream<tcp::socket>> ws_;
  std::atomic<size_t> response_delay_{0};
  std::atomic<bool> should_respond_{true};
};

/**
 * 超时机制测试类
 */
class WsTimeoutMechanismTest : public testing::Test {

protected:
  void SetUp() override {
    common::Logger::initialize(spdlog::level::trace);
    server_ = std::make_unique<MockWebSocketServer>("127.0.0.1", 61221);
    server_->start();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(SERVER_STARTUP_DELAY));

    adapter_ = std::make_unique<adapter::onebot11::ProtocolAdapter>();
    connection_manager_ = std::make_unique<network::WebSocketConnectionManager>(
        client_ioc_, *adapter_);
  }

  void TearDown() override {
    if (connection_manager_) {
      connection_manager_->disconnect();
    }
    work_guard_.reset();
    client_ioc_.stop();
    if (client_thread_.joinable()) {
      client_thread_.join();
    }
  }

  void start_client_ioc() {
    client_thread_ = std::thread([this]() {
      work_guard_.emplace(client_ioc_.get_executor());
      client_ioc_.run();
    });
  }

  void connect_to_server() {
    common::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.port = server_->get_port();
    config.access_token = "test_token";

    connection_manager_->connect(config);
    while (!connection_manager_->is_connected()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(CONNECTION_ESTABLISH_DELAY));
    }
  }

  asio::io_context client_ioc_;
  std::unique_ptr<MockWebSocketServer> server_;
  std::unique_ptr<adapter::onebot11::ProtocolAdapter> adapter_;
  std::unique_ptr<network::WebSocketConnectionManager> connection_manager_;
  std::thread client_thread_;
  std::optional<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
};

/**
 * 测试正常响应情况
 */
TEST_F(WsTimeoutMechanismTest, NormalResponse) {
  start_client_ioc();
  connect_to_server();

  server_->set_should_respond(true);
  server_->set_response_delay(NORMAL_RESPONSE_DELAY);

  nlohmann::json request;
  request["action"] = "get_login_info";
  request["echo"] = TEST_ECHO_1;

  auto start_time = std::chrono::steady_clock::now();

  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  asio::co_spawn(
      client_ioc_,
      [this, request, &result_promise]() -> asio::awaitable<void> {
        try {
          OBCX_INFO("Start sending");
          auto result =
              co_await connection_manager_->send_action_and_wait_async(
                  request.dump(), TEST_ECHO_1);
          result_promise.set_value(result);
        } catch (const std::exception &e) {
          result_promise.set_exception(std::current_exception());
        }
      },
      asio::detached);

  // 使用比正常响应时间宽裕的超时时间等待
  auto status = result_future.wait_for(std::chrono::seconds(3));
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready) << "请求应该在2秒内完成";

  std::string response;
  ASSERT_NO_THROW(response = result_future.get())
      << "请求应该成功完成，没有异常";

  ASSERT_FALSE(response.empty()) << "响应不应该为空";

  auto response_json = nlohmann::json::parse(response);
  EXPECT_EQ(response_json["echo"], TEST_ECHO_1) << "Echo应该匹配";
  EXPECT_EQ(response_json["retcode"], 0) << "返回码应该为0";

  EXPECT_LT(duration.count(), 1000) << "响应时间应该小于1秒";
  EXPECT_GT(duration.count(), NORMAL_RESPONSE_DELAY - 50)
      << "响应时间应该略大于服务器延迟";
}

/**
 * 测试超时情况
 */
TEST_F(WsTimeoutMechanismTest, TimeoutScenario) {
  start_client_ioc();
  connect_to_server();

  server_->set_should_respond(false);

  nlohmann::json request;
  request["action"] = "get_login_info";
  request["echo"] = TEST_ECHO_2;

  auto start_time = std::chrono::steady_clock::now();

  std::promise<void> result_promise;
  auto result_future = result_promise.get_future();
  std::atomic timeout_occurred = false;

  asio::co_spawn(
      client_ioc_,
      [this, request, &result_promise,
       &timeout_occurred]() -> asio::awaitable<void> {
        try {
          [[maybe_unused]] std::string _ =
              co_await connection_manager_->send_action_and_wait_async(
                  request.dump(), TEST_ECHO_2);
          result_promise.set_value(); // 不应执行到这里
        } catch (const std::runtime_error &e) {
          std::string error_msg = e.what();
          // 假设超时异常信息包含 "超时" 或 "timeout"
          if (error_msg.find("超时") != std::string::npos ||
              error_msg.find("timeout") != std::string::npos) {
            timeout_occurred = true;
          }
          result_promise.set_exception(std::current_exception());
        } catch (...) {
          result_promise.set_exception(std::current_exception());
        }
      },
      asio::detached);

  // 等待一个比内部超时稍长的时间
  auto status = result_future.wait_for(EXTENDED_TIMEOUT);
  auto end_time = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready)
      << "协程应该在 " << EXTENDED_TIMEOUT.count() << " 秒内因超时而完成";

  EXPECT_THROW(result_future.get(), std::runtime_error)
      << "应该抛出 std::runtime_error 异常";
  EXPECT_TRUE(timeout_occurred) << "异常信息应该与超时有关";

  // 验证超时时间是否在预期范围内 (例如 30s +/- 2s)
  EXPECT_GE(duration.count(), CLIENT_DEFAULT_TIMEOUT.count() - 2)
      << "超时持续时间应接近 " << CLIENT_DEFAULT_TIMEOUT.count() << " 秒";
  EXPECT_LE(duration.count(), CLIENT_DEFAULT_TIMEOUT.count() + 2)
      << "超时持续时间应接近 " << CLIENT_DEFAULT_TIMEOUT.count() << " 秒";
}

/**
 * 测试延迟响应（在客户端超时之前）
 */
TEST_F(WsTimeoutMechanismTest, DelayedResponse) {
  start_client_ioc();
  connect_to_server();

  server_->set_should_respond(true);
  server_->set_response_delay(DELAYED_RESPONSE_TIME);

  nlohmann::json request;
  request["action"] = "get_login_info";
  request["echo"] = TEST_ECHO_3;

  auto start_time = std::chrono::steady_clock::now();

  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  asio::co_spawn(
      client_ioc_,
      [this, request, &result_promise]() -> asio::awaitable<void> {
        try {
          auto result =
              co_await connection_manager_->send_action_and_wait_async(
                  request.dump(), TEST_ECHO_3);
          result_promise.set_value(result);
        } catch (const std::exception &e) {
          result_promise.set_exception(std::current_exception());
        }
      },
      asio::detached);

  auto status = result_future.wait_for(DELAYED_WAIT_TIMEOUT);
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready)
      << "请求应该在 " << DELAYED_WAIT_TIMEOUT.count() << " 秒内完成";

  std::string response;
  ASSERT_NO_THROW(response = result_future.get()) << "延迟的请求应该成功完成";

  auto response_json = nlohmann::json::parse(response);
  EXPECT_EQ(response_json["echo"], TEST_ECHO_3) << "Echo应该匹配";

  EXPECT_GE(duration.count(), DELAYED_RESPONSE_TIME - 200)
      << "响应时间应略大于 " << DELAYED_RESPONSE_TIME << " ms";
  EXPECT_LE(duration.count(), DELAYED_RESPONSE_TIME + 500)
      << "响应时间应约等于 " << DELAYED_RESPONSE_TIME << " ms";
}

} // namespace obcx::test