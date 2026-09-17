#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "common/logger.hpp"
#include "common/message_type.hpp"
#include "network/http_client.hpp"
#include "network/proxy_http_client.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/network/http/connection_manager.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/network/connection_manager.hpp"
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

/**
 * Helper to run an awaitable synchronously in tests
 * Runs the io_context until the coroutine completes and returns/throws
 */
template <typename T>
auto run_awaitable(asio::io_context &ioc, asio::awaitable<T> awaitable) -> T {
  std::optional<T> result;
  std::exception_ptr exception;

  asio::co_spawn(
      ioc,
      [&]() -> asio::awaitable<T> {
        try {
          result = co_await std::move(awaitable);
          co_return *result;
        } catch (...) {
          exception = std::current_exception();
          throw;
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

/**
 * Mock HTTP server for timeout testing
 * Can be configured to:
 * - Respond normally
 * - Respond with delay
 * - Never respond (to test timeout)
 */
class MockHttpServer {
public:
  explicit MockHttpServer(const std::string &host)
      : ioc_(), endpoint_(asio::ip::make_address(host), 0), acceptor_(ioc_),
        work_guard_(asio::make_work_guard(ioc_)) {
    acceptor_.open(endpoint_.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint_);
    acceptor_.listen();
    endpoint_ = acceptor_.local_endpoint();
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

  void set_response_body(std::string body) {
    std::scoped_lock lock(response_mutex_);
    response_body_ = std::move(body);
  }

  void set_chunked_response(bool chunked) { chunked_response_ = chunked; }

  [[nodiscard]] auto get_port() const -> uint16_t { return endpoint_.port(); }
  [[nodiscard]] auto requests() const -> int { return requests_.load(); }

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

          ++requests_;
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
    res->set(http::field::content_type, "application/octet-stream");
    {
      std::scoped_lock lock(response_mutex_);
      res->body() = response_body_;
    }
    if (chunked_response_.load()) {
      res->chunked(true);
    } else {
      res->prepare_payload();
    }

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

  std::atomic_int requests_{0};
  std::atomic<std::chrono::milliseconds> response_delay_{
      std::chrono::milliseconds(0)};
  std::atomic<bool> should_respond_{true};
  std::atomic<bool> chunked_response_{false};
  std::mutex response_mutex_;
  std::string response_body_{R"({"status":"ok","data":{}})"};

  // Keep sockets alive when not responding
  std::vector<std::shared_ptr<tcp::socket>> pending_sockets_;
};

/**
 * HTTP Client Timeout Test Suite
 */
class MockConnectProxy {
public:
  explicit MockConnectProxy(std::string response_body)
      : response_body_(std::move(response_body)),
        endpoint_(asio::ip::make_address("127.0.0.1"), 0), acceptor_(ioc_),
        work_guard_(asio::make_work_guard(ioc_)) {
    acceptor_.open(endpoint_.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint_);
    acceptor_.listen();
    endpoint_ = acceptor_.local_endpoint();
  }

  ~MockConnectProxy() { stop(); }

  void start() {
    accept();
    thread_ = std::thread([this] { ioc_.run(); });
  }

  void stop() {
    if (!thread_.joinable()) {
      return;
    }
    asio::post(ioc_, [this] {
      boost::system::error_code ignored;
      acceptor_.close(ignored);
      work_guard_.reset();
    });
    thread_.join();
  }

  [[nodiscard]] auto port() const -> std::uint16_t { return endpoint_.port(); }
  [[nodiscard]] auto connect_requests() const -> int {
    return connect_requests_.load();
  }

private:
  void accept() {
    acceptor_.async_accept([this](beast::error_code error, tcp::socket socket) {
      if (!error) {
        read_connect(std::move(socket));
      }
      if (acceptor_.is_open()) {
        accept();
      }
    });
  }

  void read_connect(tcp::socket socket) {
    auto stream = std::make_shared<tcp::socket>(std::move(socket));
    auto buffer = std::make_shared<beast::flat_buffer>();
    auto request = std::make_shared<http::request<http::string_body>>();
    http::async_read(
        *stream, *buffer, *request,
        [this, stream, buffer, request](beast::error_code error, std::size_t) {
          if (error || request->method() != http::verb::connect) {
            return;
          }
          connect_requests_.fetch_add(1);
          auto response = std::make_shared<http::response<http::empty_body>>(
              http::status::ok, 11);
          response->content_length(0);
          http::async_write(*stream, *response,
                            [this, stream, buffer,
                             response](beast::error_code error, std::size_t) {
                              if (!error) {
                                read_tunneled_request(stream, buffer);
                              }
                            });
        });
  }

  void read_tunneled_request(
      const std::shared_ptr<tcp::socket> &stream,
      const std::shared_ptr<beast::flat_buffer> &buffer) {
    auto request = std::make_shared<http::request<http::string_body>>();
    http::async_read(
        *stream, *buffer, *request,
        [this, stream, buffer, request](beast::error_code error, std::size_t) {
          if (error) {
            return;
          }
          auto response = std::make_shared<http::response<http::string_body>>(
              http::status::ok, 11);
          response->body() = response_body_;
          response->prepare_payload();
          http::async_write(*stream, *response,
                            [stream, response](beast::error_code, std::size_t) {
                              boost::system::error_code ignored;
                              stream->shutdown(tcp::socket::shutdown_both,
                                               ignored);
                            });
        });
  }

  std::string response_body_;
  asio::io_context ioc_;
  tcp::endpoint endpoint_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  std::thread thread_;
  std::atomic_int connect_requests_{0};
};

TEST(HttpClientActorExecutorTest, DirectRequestAndBodyLimitOnThreadPool) {
  common::Logger::initialize(spdlog::level::err);
  MockHttpServer server("127.0.0.1");
  server.set_response_body("actor-response");
  server.set_response_delay(std::chrono::milliseconds(0));
  server.start();
  asio::thread_pool pool(1);
  common::ConnectionConfig config;
  config.host = "127.0.0.1";
  config.port = server.get_port();
  config.use_ssl = false;
  config.connect_timeout = SHORT_TIMEOUT;
  network::HttpClient client(pool.get_executor(), config);
  client.set_response_body_limit(1024);
  const std::map<std::string, std::string> headers;
  auto response =
      asio::co_spawn(pool, client.get("/", headers), asio::use_future);
  EXPECT_EQ(response.get().body, "actor-response");
  auto oversized =
      asio::co_spawn(pool, client.get("/", headers, 4), asio::use_future);
  EXPECT_THROW(oversized.get(), network::HttpClientError);
  pool.join();
}

TEST(HttpClientActorExecutorTest, ProxyRequestAndBodyLimitOnThreadPool) {
  common::Logger::initialize(spdlog::level::err);
  MockConnectProxy proxy("actor-proxy-response");
  proxy.start();
  asio::thread_pool pool(1);
  common::ConnectionConfig config;
  config.host = "target.invalid";
  config.port = 80;
  config.use_ssl = false;
  config.connect_timeout = SHORT_TIMEOUT;
  const network::ProxyConfig proxy_config{.type = network::ProxyType::HTTP,
                                          .host = "127.0.0.1",
                                          .port = proxy.port(),
                                          .username = std::nullopt,
                                          .password = std::nullopt};
  network::ProxyHttpClient client(pool.get_executor(), proxy_config, config);
  client.set_response_body_limit(1024);
  const std::map<std::string, std::string> headers;
  auto response =
      asio::co_spawn(pool, client.get("/", headers), asio::use_future);
  EXPECT_EQ(response.get().body, "actor-proxy-response");
  auto oversized =
      asio::co_spawn(pool, client.get("/", headers, 4), asio::use_future);
  EXPECT_THROW(oversized.get(), network::HttpClientError);
  EXPECT_EQ(proxy.connect_requests(), 2);
  pool.join();
}

TEST(HttpClientActorExecutorTest, OwnerExecutorSurvivesCallerRetirement) {
  common::Logger::initialize(spdlog::level::err);
  for (const bool proxied : {false, true}) {
    SCOPED_TRACE(proxied);
    MockHttpServer server("127.0.0.1");
    server.set_response_delay(std::chrono::milliseconds{0});
    server.start();
    MockConnectProxy proxy("proxy-response");
    proxy.start();
    asio::io_context owner;
    auto guard = asio::make_work_guard(owner);
    common::ConnectionConfig config;
    config.host = proxied ? "target.invalid" : "127.0.0.1";
    config.port = proxied ? 80 : server.get_port();
    config.use_ssl = false;
    config.connect_timeout = SHORT_TIMEOUT;
    std::unique_ptr<network::HttpClient> client;
    if (proxied) {
      client = std::make_unique<network::ProxyHttpClient>(
          owner,
          network::ProxyConfig{.type = network::ProxyType::HTTP,
                               .host = "127.0.0.1",
                               .port = proxy.port(),
                               .username = std::nullopt,
                               .password = std::nullopt},
          config);
    } else {
      client = std::make_unique<network::HttpClient>(owner, config);
    }
    std::thread owner_thread;
    // Each completed caller pool is destroyed while the HTTP client survives.
    for (int method = 0; method < 3; ++method) {
      asio::thread_pool caller(1);
      auto result = asio::co_spawn(
          caller,
          [&client, method]() -> asio::awaitable<bool> {
            const auto before = std::this_thread::get_id();
            network::HttpResponse response;
            if (method == 0) {
              response = co_await client->get("/get");
            } else if (method == 1) {
              response = co_await client->post("/post", "payload");
            } else {
              response = co_await client->head("/head");
            }
            co_return response.status_code == 200 &&
                before == std::this_thread::get_id();
          },
          asio::use_future);
      if (method == 0) {
        EXPECT_EQ(result.wait_for(std::chrono::milliseconds{100}),
                  std::future_status::timeout);
        EXPECT_EQ(server.requests(), 0);
        EXPECT_EQ(proxy.connect_requests(), 0);
        owner_thread = std::thread([&owner] { owner.run(); });
      }
      EXPECT_TRUE(result.get());
      caller.join();
    }
    client->close();
    client->close();
    client.reset();
    guard.reset();
    owner_thread.join();
  }
}

TEST(HttpClientActorExecutorTest, CloseCancelsStalledPostAndRejectsNewWork) {
  common::Logger::initialize(spdlog::level::err);
  MockHttpServer server("127.0.0.1");
  server.set_should_respond(false);
  server.start();
  asio::thread_pool owner(1);
  asio::thread_pool caller(1);
  common::ConnectionConfig config;
  config.host = "127.0.0.1";
  config.port = server.get_port();
  config.use_ssl = false;
  config.connect_timeout = std::chrono::seconds{30};
  auto client =
      std::make_unique<network::HttpClient>(owner.get_executor(), config);
  auto result = asio::co_spawn(caller, client->post("/hang", "payload"),
                               asio::use_future);
  const auto deadline = std::chrono::steady_clock::now() + SHORT_TIMEOUT;
  while (server.requests() == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  EXPECT_EQ(server.requests(), 1);
  client->close();
  auto rejected =
      asio::co_spawn(caller, client->get("/late"), asio::use_future);
  client.reset(); // Admitted requests must not dereference a dead HttpClient.
  EXPECT_EQ(result.wait_for(SHORT_TIMEOUT), std::future_status::ready);
  try {
    (void)result.get();
    ADD_FAILURE() << "closed POST must be cancelled";
  } catch (const network::HttpClientError &error) {
    EXPECT_NE(std::string{error.what()}.find("cancelled"), std::string::npos);
    EXPECT_EQ(error.submission_state(),
              network::HttpRequestSubmissionState::PossiblySubmitted);
  }
  try {
    (void)rejected.get();
    ADD_FAILURE() << "closed client must reject new work";
  } catch (const network::HttpClientError &error) {
    EXPECT_EQ(error.submission_state(),
              network::HttpRequestSubmissionState::DefinitelyNotSubmitted);
  }
  caller.join();
  owner.join();
  EXPECT_EQ(server.requests(), 1);
}

TEST(HttpClientActorExecutorTest,
     LazyRequestRetainsStateButCannotReopenClosedClient) {
  asio::io_context owner;
  common::ConnectionConfig config;
  config.host = "127.0.0.1";
  config.port = 1;
  config.use_ssl = false;
  config.connect_timeout = SHORT_TIMEOUT;
  auto client = std::make_unique<network::HttpClient>(owner, config);
  auto operation = client->get(std::string{"/owned-path"});
  client.reset();
  auto result = asio::co_spawn(owner, std::move(operation), asio::use_future);
  owner.run();
  EXPECT_THROW((void)result.get(), network::HttpClientError);
}

TEST(HttpClientActorExecutorTest, HttpPollingDisconnectDrainsWithoutRetry) {
  common::Logger::initialize(spdlog::level::err);
  for (const bool telegram : {false, true}) {
    SCOPED_TRACE(telegram);
    MockHttpServer server("127.0.0.1");
    server.set_should_respond(false);
    server.start();
    asio::io_context owner;
    common::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.port = server.get_port();
    config.use_ssl = false;
    config.connect_timeout = std::chrono::seconds{30};
    config.poll_timeout = std::chrono::seconds{20};
    config.poll_force_close = std::chrono::seconds{30};
    config.poll_retry_interval = std::chrono::seconds{30};
    adapter::telegram::ProtocolAdapter telegram_protocol;
    adapter::onebot11::ProtocolAdapter onebot_protocol;
    network::TelegramConnectionManager telegram_manager(owner,
                                                        telegram_protocol);
    network::HttpConnectionManager onebot_manager(owner, onebot_protocol);
    onebot_manager.set_poll_interval(std::chrono::seconds{30});
    if (telegram) {
      telegram_manager.connect(config);
    } else {
      onebot_manager.connect(config);
    }
    std::promise<void> drained;
    auto completed = drained.get_future();
    std::thread runner([&] {
      owner.run();
      drained.set_value();
    });
    const auto deadline = std::chrono::steady_clock::now() + SHORT_TIMEOUT;
    while (server.requests() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_EQ(server.requests(), 1);
    telegram_manager.disconnect();
    onebot_manager.disconnect();
    const auto status = completed.wait_for(SHORT_TIMEOUT);
    EXPECT_EQ(status, std::future_status::ready);
    if (status != std::future_status::ready) {
      owner.stop(); // Test failure escape, never the production shutdown path.
    }
    runner.join();
    EXPECT_EQ(server.requests(), 1);
  }
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
TEST(HttpClientActorExecutorTest,
     SynchronousCallsUseIsolatedContextAndPreserveProxy) {
  common::Logger::initialize(spdlog::level::err);
  MockHttpServer server("127.0.0.1");
  server.set_response_delay(std::chrono::milliseconds{0});
  server.set_response_body("sync-response");
  server.start();
  MockConnectProxy proxy("proxy-response");
  proxy.start();
  asio::io_context idle_owner;
  common::ConnectionConfig config;
  config.host = "127.0.0.1";
  config.port = server.get_port();
  config.use_ssl = false;
  config.connect_timeout = SHORT_TIMEOUT;
  network::HttpClient client(idle_owner, config);
  EXPECT_EQ(client.get_sync("/get").body, "sync-response");
  EXPECT_EQ(client.post_sync("/post", "body").status_code, 200U);
  EXPECT_EQ(client.head_sync("/head").status_code, 200U);
  client.set_response_body_limit(4);
  EXPECT_THROW((void)client.get_sync("/too-large"), network::HttpClientError);
  EXPECT_FALSE(client.is_connected());
  network::ProxyHttpClient proxied(
      idle_owner,
      network::ProxyConfig{.type = network::ProxyType::HTTP,
                           .host = "127.0.0.1",
                           .port = proxy.port(),
                           .username = std::nullopt,
                           .password = std::nullopt},
      config);
  EXPECT_EQ(proxied.get_sync("/proxy").body, "proxy-response");
  EXPECT_EQ(proxy.connect_requests(), 1);
  client.close();
  EXPECT_THROW((void)client.get_sync("/closed"), network::HttpClientError);
  EXPECT_EQ(idle_owner.poll(), 0U);
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

class HttpClientTimeoutTest : public testing::Test {
protected:
  void SetUp() override {
    common::Logger::initialize(spdlog::level::err);
    server_ = std::make_unique<MockHttpServer>("127.0.0.1");
    server_->start();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(SERVER_STARTUP_DELAY_MS));
  }

  void TearDown() override {
    if (server_) {
      server_->stop();
    }
  }

  auto create_client(std::chrono::milliseconds connect_timeout)
      -> std::unique_ptr<network::HttpClient> {
    common::ConnectionConfig config;
    config.host = "127.0.0.1";
    config.port = server_->get_port();
    config.use_ssl = false;
    config.connect_timeout = connect_timeout;

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

  ASSERT_NO_THROW(response = run_awaitable(ioc_, client->get("/test")));

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
          [[maybe_unused]] auto response =
              run_awaitable(ioc_, client->get("/test"));
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

  try {
    [[maybe_unused]] auto response =
        run_awaitable(ioc_, client->post("/test", "{}"));
    FAIL() << "POST should time out after request submission";
  } catch (const network::HttpClientError &error) {
    EXPECT_EQ(error.submission_state(),
              network::HttpRequestSubmissionState::PossiblySubmitted);
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  // Verify timeout occurred within expected range
  EXPECT_GE(duration.count(), SHORT_TIMEOUT.count() - 500)
      << "POST timeout should occur around the configured timeout";
  EXPECT_LE(duration.count(), SHORT_TIMEOUT.count() + 1000)
      << "POST timeout should not take much longer than configured";
}

// Note: AsyncTimeoutViaFuture test removed because get_async() was replaced
// with coroutine-based get() awaitable API. The new API uses asio::awaitable
// for proper async support without std::future/std::thread.

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
      {
        [[maybe_unused]] auto response =
            run_awaitable(ioc_, client->get("/test"));
      },
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
  auto response = run_awaitable(ioc_, client->get("/test"));
  EXPECT_EQ(response.status_code, 200);
  EXPECT_TRUE(client->is_connected())
      << "Should be connected after successful request";
}

/**
 * Test: Delayed response that arrives before timeout
 */
TEST_F(HttpClientTimeoutTest, DefaultResponseBodyLimitRejectsOversizedBody) {
  server_->set_response_body(
      std::string(network::HttpClient::kDefaultResponseBodyLimit + 1, 'x'));
  auto client = create_client(EXTENDED_WAIT);

  EXPECT_EQ(client->response_body_limit(),
            network::HttpClient::kDefaultResponseBodyLimit);
  EXPECT_THROW([[maybe_unused]] auto response =
                   run_awaitable(ioc_, client->get("/oversized")),
               network::HttpClientError);
}

TEST_F(HttpClientTimeoutTest, CustomResponseBodyLimitAcceptsLargerBody) {
  constexpr std::uint64_t kTenMiB = 10ULL * 1024ULL * 1024ULL;
  constexpr std::size_t kNineMiB = 9ULL * 1024ULL * 1024ULL;
  server_->set_response_body(std::string(kNineMiB, 'x'));
  auto client = create_client(EXTENDED_WAIT);
  client->set_response_body_limit(kTenMiB);

  auto response = run_awaitable(ioc_, client->get("/large"));

  EXPECT_EQ(client->response_body_limit(), kTenMiB);
  EXPECT_EQ(response.body.size(), kNineMiB);
}

TEST_F(HttpClientTimeoutTest, PerRequestBodyLimitOverridesClientDefault) {
  constexpr std::uint64_t kTenMiB = 10ULL * 1024ULL * 1024ULL;
  constexpr std::size_t kNineMiB = 9ULL * 1024ULL * 1024ULL;
  server_->set_response_body(std::string(kNineMiB, 'x'));
  auto client = create_client(EXTENDED_WAIT);

  auto response =
      run_awaitable(ioc_, client->get("/large-per-request", {}, kTenMiB));

  EXPECT_EQ(response.body.size(), kNineMiB);
  EXPECT_EQ(client->response_body_limit(),
            network::HttpClient::kDefaultResponseBodyLimit);
}

TEST_F(HttpClientTimeoutTest, CustomResponseBodyLimitRejectsOneByteOver) {
  constexpr std::uint64_t kLimit = 1024;
  server_->set_response_body(std::string(kLimit + 1, 'x'));
  auto client = create_client(EXTENDED_WAIT);
  client->set_response_body_limit(kLimit);

  EXPECT_THROW([[maybe_unused]] auto response =
                   run_awaitable(ioc_, client->get("/over-custom-limit")),
               network::HttpClientError);
}

TEST_F(HttpClientTimeoutTest, ZeroResponseBodyLimitIsRejected) {
  auto client = create_client(SHORT_TIMEOUT);

  EXPECT_THROW(client->set_response_body_limit(0), std::invalid_argument);
  EXPECT_EQ(client->response_body_limit(),
            network::HttpClient::kDefaultResponseBodyLimit);
}

TEST_F(HttpClientTimeoutTest, ChunkedResponseCrossingLimitIsRejected) {
  constexpr std::uint64_t kLimit = 1024;
  server_->set_response_body(std::string(kLimit + 1, 'x'));
  server_->set_chunked_response(true);
  auto client = create_client(EXTENDED_WAIT);
  client->set_response_body_limit(kLimit);

  EXPECT_THROW([[maybe_unused]] auto response =
                   run_awaitable(ioc_, client->get("/chunked")),
               network::HttpClientError);
}

TEST_F(HttpClientTimeoutTest, PostResponseUsesConfiguredBodyLimit) {
  constexpr std::uint64_t kLimit = 1024;
  server_->set_response_body(std::string(kLimit + 1, 'x'));
  auto client = create_client(EXTENDED_WAIT);
  client->set_response_body_limit(kLimit);

  EXPECT_THROW([[maybe_unused]] auto response =
                   run_awaitable(ioc_, client->post("/post", "{}")),
               network::HttpClientError);
}

TEST(HttpClientSubmissionStateTest,
     ProxyPostConnectionRefusedIsDefinitelyNotSubmitted) {
  asio::io_context port_context;
  tcp::acceptor reservation(port_context, {tcp::v4(), 0});
  const auto refused_port = reservation.local_endpoint().port();
  reservation.close();

  asio::io_context ioc;
  common::ConnectionConfig target;
  target.host = "api.telegram.org";
  target.port = 443;
  target.use_ssl = true;
  target.connect_timeout = SHORT_TIMEOUT;
  network::ProxyConfig proxy_config;
  proxy_config.type = network::ProxyType::SOCKS5;
  proxy_config.host = "127.0.0.1";
  proxy_config.port = refused_port;
  network::ProxyHttpClient client(ioc, std::move(proxy_config), target);

  try {
    [[maybe_unused]] auto response =
        run_awaitable(ioc, client.post("/bot-redacted/sendMessage", "{}"));
    FAIL() << "POST through an unavailable proxy should fail";
  } catch (const network::HttpClientError &error) {
    EXPECT_EQ(error.submission_state(),
              network::HttpRequestSubmissionState::DefinitelyNotSubmitted);
  }
}

TEST(HttpClientResponseLimitTest, ProxyGetUsesConfiguredLimit) {
  constexpr std::uint64_t kLimit = 1024;
  MockConnectProxy proxy(std::string(kLimit + 1, 'x'));
  proxy.start();

  asio::io_context ioc;
  common::ConnectionConfig target;
  target.host = "target.invalid";
  target.port = 80;
  target.use_ssl = false;
  target.connect_timeout = EXTENDED_WAIT;
  network::ProxyConfig proxy_config;
  proxy_config.type = network::ProxyType::HTTP;
  proxy_config.host = "127.0.0.1";
  proxy_config.port = proxy.port();
  network::ProxyHttpClient client(ioc, std::move(proxy_config), target);
  client.set_response_body_limit(kLimit);

  EXPECT_THROW([[maybe_unused]] auto response =
                   run_awaitable(ioc, client.get("/proxied")),
               network::HttpClientError);
  EXPECT_EQ(proxy.connect_requests(), 1);
  proxy.stop();
}

TEST_F(HttpClientTimeoutTest, DelayedResponseBeforeTimeout) {
  // Set delay shorter than timeout
  std::chrono::milliseconds response_delay{1000};
  server_->set_should_respond(true);
  server_->set_response_delay(response_delay);

  auto client = create_client(SHORT_TIMEOUT);

  auto start_time = std::chrono::steady_clock::now();
  network::HttpResponse response;

  ASSERT_NO_THROW(response = run_awaitable(ioc_, client->get("/test")));

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
