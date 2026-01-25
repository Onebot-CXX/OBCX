#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <random>
#include <thread>

#include "common/logger.hpp"
#include "common/message_type.hpp"
#include "network/http_client.hpp"
// NOLINTBEGIN

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace obcx::test {

constexpr size_t SERVER_STARTUP_DELAY_MS = 500;
constexpr std::chrono::milliseconds SHORT_TIMEOUT{2000}; // 2 seconds
constexpr std::chrono::milliseconds EXTENDED_WAIT{5000}; // 5 seconds
constexpr std::chrono::milliseconds NORMAL_RESPONSE_DELAY{100};

// Generate random port in high range (40000-65535)
inline uint16_t get_random_port() {
  static std::mt19937 gen(static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count()));
  static std::uniform_int_distribution<uint16_t> dist(40000, 65535);
  return dist(gen);
}

/**
 * Mock HTTP server for timeout testing
 * Can be configured to:
 * - Respond normally
 * - Respond with delay
 * - Never respond (to test timeout)
 */
class MockHttpServer {
public:
  MockHttpServer(const std::string &host, uint16_t port)
      : ioc_(), endpoint_(asio::ip::make_address(host), port),
        acceptor_(ioc_, endpoint_), work_guard_(asio::make_work_guard(ioc_)) {
    acceptor_.set_option(asio::socket_base::reuse_address(true));
  }

  ~MockHttpServer() {
    if (thread_.joinable()) {
      stop();
    }
  }

  void start() {
    thread_ = std::thread([this]() {
      OBCX_DEBUG("HTTP Mock server started on {}:{}",
                 endpoint_.address().to_string(), endpoint_.port());
      do_accept();
      ioc_.run();
      OBCX_DEBUG("HTTP Mock server stopped");
    });
  }

  void stop() {
    asio::post(ioc_, [this]() {
      OBCX_DEBUG("Stopping HTTP mock server...");
      acceptor_.close();
      work_guard_.reset();
      ioc_.stop();
    });

    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void set_response_delay(std::chrono::milliseconds delay) {
    response_delay_ = delay;
  }

  void set_should_respond(bool should_respond) {
    should_respond_ = should_respond;
  }

  [[nodiscard]] auto get_port() const -> uint16_t { return endpoint_.port(); }

private:
  void do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (!acceptor_.is_open()) {
        return;
      }
      if (!ec) {
        OBCX_DEBUG("Accepted new HTTP connection");
        handle_request(std::move(socket));
      }
      do_accept();
    });
  }

  void handle_request(tcp::socket socket) {
    auto sock = std::make_shared<tcp::socket>(std::move(socket));
    auto buffer = std::make_shared<beast::flat_buffer>();
    auto req = std::make_shared<http::request<http::string_body>>();

    http::async_read(
        *sock, *buffer, *req,
        [this, sock, buffer, req](beast::error_code ec,
                                  std::size_t /*bytes_transferred*/) {
          if (ec) {
            OBCX_DEBUG("Read error: {}", ec.message());
            return;
          }

          OBCX_DEBUG("Received request: {} {}", req->method_string(),
                     req->target());

          if (!should_respond_.load()) {
            OBCX_DEBUG("Configured to not respond - connection will hang");
            // Keep the socket alive but don't respond
            // This simulates a server that accepts but never responds
            pending_sockets_.push_back(sock);
            return;
          }

          auto delay = response_delay_.load();
          if (delay.count() > 0) {
            auto timer = std::make_shared<asio::steady_timer>(ioc_);
            timer->expires_after(delay);
            timer->async_wait([this, sock, timer](beast::error_code ec) {
              if (!ec) {
                send_response(sock);
              }
            });
          } else {
            send_response(sock);
          }
        });
  }

  void send_response(const std::shared_ptr<tcp::socket> &sock) {
    auto res = std::make_shared<http::response<http::string_body>>(
        http::status::ok, 11);
    res->set(http::field::server, "MockHttpServer/1.0");
    res->set(http::field::content_type, "application/json");
    res->body() = R"({"status":"ok","data":{}})";
    res->prepare_payload();

    http::async_write(*sock, *res,
                      [sock, res](beast::error_code ec, std::size_t) {
                        if (ec) {
                          OBCX_DEBUG("Write error: {}", ec.message());
                        } else {
                          OBCX_DEBUG("Response sent successfully");
                        }
                        // Close connection after response
                        boost::system::error_code close_ec;
                        sock->shutdown(tcp::socket::shutdown_both, close_ec);
                      });
  }

  asio::io_context ioc_;
  tcp::endpoint endpoint_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::thread thread_;

  std::atomic<std::chrono::milliseconds> response_delay_{
      std::chrono::milliseconds(0)};
  std::atomic<bool> should_respond_{true};

  // Keep sockets alive when not responding
  std::vector<std::shared_ptr<tcp::socket>> pending_sockets_;
};

/**
 * HTTP Client Timeout Test Suite
 */
class HttpClientTimeoutTest : public testing::Test {
protected:
  void SetUp() override {
    common::Logger::initialize(spdlog::level::trace);
    test_port_ = get_random_port();
    server_ = std::make_unique<MockHttpServer>("127.0.0.1", test_port_);
    server_->start();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(SERVER_STARTUP_DELAY_MS));
  }

  uint16_t test_port_{0};

  void TearDown() override {
    if (server_) {
      server_->stop();
    }
  }

  auto create_client(std::chrono::milliseconds timeout)
      -> std::unique_ptr<network::HttpClient> {
    common::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.port = server_->get_port();
    config.use_ssl = false;
    config.timeout = timeout;

    return std::make_unique<network::HttpClient>(ioc_, config);
  }

  asio::io_context ioc_;
  std::unique_ptr<MockHttpServer> server_;
};

/**
 * Test: Normal response within timeout
 */
TEST_F(HttpClientTimeoutTest, NormalResponseWithinTimeout) {
  server_->set_should_respond(true);
  server_->set_response_delay(NORMAL_RESPONSE_DELAY);

  auto client = create_client(SHORT_TIMEOUT);

  auto start_time = std::chrono::steady_clock::now();
  network::HttpResponse response;

  ASSERT_NO_THROW(response = client->get_sync("/test"));

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  EXPECT_EQ(response.status_code, 200) << "Response status should be 200";
  EXPECT_FALSE(response.body.empty()) << "Response body should not be empty";
  EXPECT_LT(duration.count(), SHORT_TIMEOUT.count())
      << "Request should complete before timeout";
}

/**
 * Test: Request times out when server doesn't respond
 */
TEST_F(HttpClientTimeoutTest, TimeoutWhenServerDoesNotRespond) {
  server_->set_should_respond(false);

  auto client = create_client(SHORT_TIMEOUT);

  auto start_time = std::chrono::steady_clock::now();

  EXPECT_THROW(
      {
        try {
          [[maybe_unused]] auto response = client->get_sync("/test");
        } catch (const network::HttpClientError &e) {
          std::string error_msg = e.what();
          OBCX_DEBUG("Caught expected timeout error: {}", error_msg);
          // Verify error message indicates timeout
          EXPECT_TRUE(error_msg.find("timeout") != std::string::npos ||
                      error_msg.find("timed out") != std::string::npos ||
                      error_msg.find("Operation canceled") != std::string::npos)
              << "Error should indicate timeout: " << error_msg;
          throw;
        }
      },
      network::HttpClientError);

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  // Verify timeout occurred within expected range (timeout +/- 500ms tolerance)
  EXPECT_GE(duration.count(), SHORT_TIMEOUT.count() - 500)
      << "Timeout should occur around the configured timeout";
  EXPECT_LE(duration.count(), SHORT_TIMEOUT.count() + 1000)
      << "Timeout should not take much longer than configured";
}

/**
 * Test: POST request times out
 */
TEST_F(HttpClientTimeoutTest, PostTimeoutWhenServerDoesNotRespond) {
  server_->set_should_respond(false);

  auto client = create_client(SHORT_TIMEOUT);

  auto start_time = std::chrono::steady_clock::now();

  EXPECT_THROW(
      { [[maybe_unused]] auto response = client->post_sync("/test", "{}"); },
      network::HttpClientError);

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  // Verify timeout occurred within expected range
  EXPECT_GE(duration.count(), SHORT_TIMEOUT.count() - 500)
      << "POST timeout should occur around the configured timeout";
  EXPECT_LE(duration.count(), SHORT_TIMEOUT.count() + 1000)
      << "POST timeout should not take much longer than configured";
}

/**
 * Test: Async request timeout via future
 */
TEST_F(HttpClientTimeoutTest, AsyncTimeoutViaFuture) {
  server_->set_should_respond(false);

  auto client = create_client(SHORT_TIMEOUT);

  auto start_time = std::chrono::steady_clock::now();
  auto future = client->get_async("/test");

  // Wait for the future with extended timeout
  auto status = future.wait_for(EXTENDED_WAIT);

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  ASSERT_EQ(status, std::future_status::ready)
      << "Future should complete (with exception) before extended wait";

  // Verify it throws
  EXPECT_THROW(future.get(), network::HttpClientError);

  // Verify timeout occurred within expected range
  EXPECT_GE(duration.count(), SHORT_TIMEOUT.count() - 500)
      << "Async timeout should occur around the configured timeout";
  EXPECT_LE(duration.count(), SHORT_TIMEOUT.count() + 1000)
      << "Async timeout should not take much longer than configured";
}

/**
 * Test: set_timeout updates the timeout value
 */
TEST_F(HttpClientTimeoutTest, SetTimeoutUpdatesValue) {
  auto client = create_client(std::chrono::milliseconds(30000));

  // Change timeout to a shorter value
  client->set_timeout(SHORT_TIMEOUT);

  server_->set_should_respond(false);

  auto start_time = std::chrono::steady_clock::now();

  EXPECT_THROW(
      { [[maybe_unused]] auto response = client->get_sync("/test"); },
      network::HttpClientError);

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  // Should timeout based on the new SHORT_TIMEOUT, not the original 30s
  EXPECT_LT(duration.count(), 10000)
      << "Timeout should reflect the updated value (not original 30s)";
  EXPECT_GE(duration.count(), SHORT_TIMEOUT.count() - 500)
      << "Timeout should occur around the new timeout value";
}

/**
 * Test: is_connected returns correct state
 */
TEST_F(HttpClientTimeoutTest, IsConnectedReturnsCorrectState) {
  server_->set_should_respond(true);
  server_->set_response_delay(std::chrono::milliseconds(0));

  auto client = create_client(SHORT_TIMEOUT);

  // Before any request, connected should be false
  EXPECT_FALSE(client->is_connected())
      << "Should not be connected before first request";

  // After successful request, connected should be true
  auto response = client->get_sync("/test");
  EXPECT_EQ(response.status_code, 200);
  EXPECT_TRUE(client->is_connected())
      << "Should be connected after successful request";
}

/**
 * Test: Delayed response that arrives before timeout
 */
TEST_F(HttpClientTimeoutTest, DelayedResponseBeforeTimeout) {
  // Set delay shorter than timeout
  std::chrono::milliseconds response_delay{1000};
  server_->set_should_respond(true);
  server_->set_response_delay(response_delay);

  auto client = create_client(SHORT_TIMEOUT);

  auto start_time = std::chrono::steady_clock::now();
  network::HttpResponse response;

  ASSERT_NO_THROW(response = client->get_sync("/test"));

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  EXPECT_EQ(response.status_code, 200) << "Delayed response should succeed";

  // Should take at least the delay time
  EXPECT_GE(duration.count(), response_delay.count() - 100)
      << "Should wait for the delayed response";
  // But less than the timeout
  EXPECT_LT(duration.count(), SHORT_TIMEOUT.count())
      << "Should complete before timeout";
}

} // namespace obcx::test
// NOLINTEND
