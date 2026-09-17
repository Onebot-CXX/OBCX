#include "network/curl_asio_multi.hpp"

#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
namespace fs = std::filesystem;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

class LocalHttpServer final {
public:
  LocalHttpServer()
      : acceptor_(ioc_, {asio::ip::make_address("127.0.0.1"), 0}),
        guard_(asio::make_work_guard(ioc_)) {}

  ~LocalHttpServer() { stop(); }

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
      for (const auto &socket : hanging_) {
        socket->close(ignored);
      }
      hanging_.clear();
      guard_.reset();
    });
    thread_.join();
  }

  [[nodiscard]] auto url(std::string_view target) const -> std::string {
    return "http://127.0.0.1:" +
           std::to_string(acceptor_.local_endpoint().port()) +
           std::string(target);
  }

  [[nodiscard]] auto requests() const noexcept -> int {
    return requests_.load();
  }

private:
  static auto gzip(std::string_view input) -> std::string {
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
      throw std::runtime_error("gzip initialization failed");
    }
    stream.next_in =
        reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::string output(256, '\0');
    for (;;) {
      stream.next_out =
          reinterpret_cast<Bytef *>(output.data() + stream.total_out);
      stream.avail_out = static_cast<uInt>(output.size() - stream.total_out);
      const auto result = deflate(&stream, Z_FINISH);
      if (result == Z_STREAM_END) {
        output.resize(stream.total_out);
        deflateEnd(&stream);
        return output;
      }
      if (result != Z_OK) {
        deflateEnd(&stream);
        throw std::runtime_error("gzip encoding failed");
      }
      output.resize(output.size() * 2);
    }
  }

  void accept() {
    acceptor_.async_accept([this](beast::error_code error, tcp::socket socket) {
      if (!error) {
        read(std::move(socket));
      }
      if (acceptor_.is_open()) {
        accept();
      }
    });
  }

  void read(tcp::socket socket) {
    auto owned = std::make_shared<tcp::socket>(std::move(socket));
    auto buffer = std::make_shared<beast::flat_buffer>();
    auto request = std::make_shared<http::request<http::string_body>>();
    http::async_read(
        *owned, *buffer, *request,
        [this, owned, buffer, request](beast::error_code error, std::size_t) {
          if (error) {
            return;
          }
          ++requests_;
          if (request->target() == "/hang") {
            hanging_.push_back(owned);
            return;
          }
          auto response = std::make_shared<http::response<http::string_body>>(
              http::status::ok, request->version());
          if (request->target() == "/gzip") {
            response->set(http::field::content_encoding, "gzip");
            response->body() = gzip(std::string(4096, 'x'));
          } else if (request->target() == "/large") {
            response->body() = std::string(4096, 'w');
          } else {
            response->body() = "ok:" + std::string(request->target());
          }
          if (request->target() == "/large-header") {
            response->set("X-OBCX-Large", std::string(4096, 'h'));
          }
          response->keep_alive(false);
          response->prepare_payload();
          http::async_write(*owned, *response,
                            [owned, response](beast::error_code, std::size_t) {
                              boost::system::error_code ignored;
                              owned->shutdown(tcp::socket::shutdown_both,
                                              ignored);
                            });
        });
  }

  asio::io_context ioc_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> guard_;
  std::thread thread_;
  std::vector<std::shared_ptr<tcp::socket>> hanging_;
  std::atomic_int requests_{0};
};

class LocalTlsOrigin final {
public:
  LocalTlsOrigin()
      : context_(ssl::context::tls_server),
        acceptor_(ioc_, {asio::ip::make_address("127.0.0.1"), 0}),
        guard_(asio::make_work_guard(ioc_)) {
    const fs::path fixtures{OBCX_CURL_TLS_FIXTURE_DIR};
    context_.use_certificate_chain_file((fixtures / "server.pem").string());
    context_.use_private_key_file((fixtures / "server-key.pem").string(),
                                  ssl::context::pem);
  }

  ~LocalTlsOrigin() { stop(); }

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
      guard_.reset();
    });
    thread_.join();
  }

  [[nodiscard]] auto url(std::string_view host = "localhost") const
      -> std::string {
    return "https://" + std::string(host) + ":" +
           std::to_string(acceptor_.local_endpoint().port()) + "/secure";
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return acceptor_.local_endpoint().port();
  }

private:
  void accept() {
    acceptor_.async_accept([this](beast::error_code error, tcp::socket socket) {
      if (!error) {
        auto stream = std::make_shared<ssl::stream<tcp::socket>>(
            std::move(socket), context_);
        stream->async_handshake(ssl::stream_base::server,
                                [this, stream](beast::error_code handshake) {
                                  if (!handshake) {
                                    read(stream);
                                  }
                                });
      }
      if (acceptor_.is_open()) {
        accept();
      }
    });
  }

  void read(const std::shared_ptr<ssl::stream<tcp::socket>> &stream) {
    auto buffer = std::make_shared<beast::flat_buffer>();
    auto incoming = std::make_shared<http::request<http::string_body>>();
    http::async_read(
        *stream, *buffer, *incoming,
        [stream, buffer, incoming](beast::error_code error, std::size_t) {
          if (error) {
            return;
          }
          auto response = std::make_shared<http::response<http::string_body>>(
              http::status::ok, incoming->version());
          response->body() = "secure";
          response->keep_alive(false);
          response->prepare_payload();
          http::async_write(*stream, *response,
                            [stream, response](beast::error_code, std::size_t) {
                              boost::system::error_code ignored;
                              stream->next_layer().shutdown(
                                  tcp::socket::shutdown_both, ignored);
                            });
        });
  }

  asio::io_context ioc_;
  ssl::context context_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> guard_;
  std::thread thread_;
};

template <typename ClientStream>
class ConnectSession final
    : public std::enable_shared_from_this<ConnectSession<ClientStream>> {
public:
  ConnectSession(ClientStream stream, const tcp::endpoint target,
                 std::atomic_int &connect_requests)
      : client_(std::move(stream)), target_(client_.get_executor()),
        target_endpoint_(target), connect_requests_(connect_requests) {}

  void start(const bool handshake) {
    if constexpr (std::is_same_v<ClientStream, ssl::stream<tcp::socket>>) {
      if (handshake) {
        auto self = this->shared_from_this();
        client_.async_handshake(ssl::stream_base::server,
                                [self](beast::error_code error) {
                                  if (!error) {
                                    self->read_connect();
                                  }
                                });
        return;
      }
    }
    read_connect();
  }

private:
  void read_connect() {
    auto self = this->shared_from_this();
    http::async_read(
        client_, connect_buffer_, connect_request_,
        [self](beast::error_code error, std::size_t) {
          if (!error &&
              self->connect_request_.method() == http::verb::connect) {
            ++self->connect_requests_;
            self->target_.async_connect(self->target_endpoint_,
                                        [self](beast::error_code connected) {
                                          if (!connected) {
                                            self->write_connected();
                                          }
                                        });
          }
        });
  }

  void write_connected() {
    connect_response_.version(11);
    connect_response_.result(http::status::ok);
    connect_response_.content_length(0);
    auto self = this->shared_from_this();
    http::async_write(client_, connect_response_,
                      [self](beast::error_code error, std::size_t) {
                        if (!error) {
                          self->read_client();
                          self->read_target();
                        }
                      });
  }

  void read_client() {
    auto self = this->shared_from_this();
    client_.async_read_some(
        asio::buffer(client_buffer_),
        [self](beast::error_code error, const std::size_t bytes) {
          if (error) {
            self->close();
            return;
          }
          asio::async_write(self->target_,
                            asio::buffer(self->client_buffer_, bytes),
                            [self](beast::error_code written, std::size_t) {
                              if (written) {
                                self->close();
                              } else {
                                self->read_client();
                              }
                            });
        });
  }

  void read_target() {
    auto self = this->shared_from_this();
    target_.async_read_some(
        asio::buffer(target_buffer_),
        [self](beast::error_code error, const std::size_t bytes) {
          if (error) {
            self->close();
            return;
          }
          asio::async_write(self->client_,
                            asio::buffer(self->target_buffer_, bytes),
                            [self](beast::error_code written, std::size_t) {
                              if (written) {
                                self->close();
                              } else {
                                self->read_target();
                              }
                            });
        });
  }

  void close() {
    boost::system::error_code ignored;
    beast::get_lowest_layer(client_).close(ignored);
    target_.close(ignored);
  }

  ClientStream client_;
  tcp::socket target_;
  tcp::endpoint target_endpoint_;
  std::atomic_int &connect_requests_;
  beast::flat_buffer connect_buffer_;
  http::request<http::string_body> connect_request_;
  http::response<http::empty_body> connect_response_;
  std::array<char, 8192> client_buffer_{};
  std::array<char, 8192> target_buffer_{};
};

class LocalConnectProxy final {
public:
  LocalConnectProxy(const tcp::endpoint target, const bool tls)
      : tls_(tls), context_(ssl::context::tls_server), target_(target),
        acceptor_(ioc_, {asio::ip::make_address("127.0.0.1"), 0}),
        guard_(asio::make_work_guard(ioc_)) {
    if (tls_) {
      const fs::path fixtures{OBCX_CURL_TLS_FIXTURE_DIR};
      context_.use_certificate_chain_file((fixtures / "server.pem").string());
      context_.use_private_key_file((fixtures / "server-key.pem").string(),
                                    ssl::context::pem);
    }
  }

  ~LocalConnectProxy() { stop(); }

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
      guard_.reset();
    });
    thread_.join();
  }

  [[nodiscard]] auto host() const -> std::string {
    return tls_ ? "localhost" : "127.0.0.1";
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return acceptor_.local_endpoint().port();
  }

  [[nodiscard]] auto connect_requests() const noexcept -> int {
    return connect_requests_.load();
  }

private:
  void accept() {
    acceptor_.async_accept([this](beast::error_code error, tcp::socket socket) {
      if (!error) {
        if (tls_) {
          auto session =
              std::make_shared<ConnectSession<ssl::stream<tcp::socket>>>(
                  ssl::stream<tcp::socket>{std::move(socket), context_},
                  target_, connect_requests_);
          session->start(true);
        } else {
          auto session = std::make_shared<ConnectSession<tcp::socket>>(
              std::move(socket), target_, connect_requests_);
          session->start(false);
        }
      }
      if (acceptor_.is_open()) {
        accept();
      }
    });
  }

  bool tls_;
  asio::io_context ioc_;
  ssl::context context_;
  tcp::endpoint target_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> guard_;
  std::thread thread_;
  std::atomic_int connect_requests_{0};
};

class Socks5Session final : public std::enable_shared_from_this<Socks5Session> {
public:
  Socks5Session(tcp::socket client, const tcp::endpoint target,
                std::atomic_bool &remote_dns)
      : client_(std::move(client)), target_(client_.get_executor()),
        endpoint_(target), remote_dns_(remote_dns) {}

  void start() { read_greeting(); }

private:
  void read_greeting() {
    auto self = shared_from_this();
    asio::async_read(client_, asio::buffer(greeting_),
                     [self](beast::error_code error, std::size_t) {
                       if (!error && self->greeting_[0] == 5 &&
                           self->greeting_[1] > 0) {
                         self->methods_.resize(self->greeting_[1]);
                         self->read_methods();
                       }
                     });
  }

  void read_methods() {
    auto self = shared_from_this();
    asio::async_read(client_, asio::buffer(methods_),
                     [self](beast::error_code error, std::size_t) {
                       if (!error && std::ranges::find(self->methods_, 0) !=
                                         self->methods_.end()) {
                         self->write_method();
                       }
                     });
  }

  void write_method() {
    auto self = shared_from_this();
    asio::async_write(client_, asio::buffer(method_),
                      [self](beast::error_code error, std::size_t) {
                        if (!error) {
                          self->read_command();
                        }
                      });
  }

  void read_command() {
    auto self = shared_from_this();
    asio::async_read(client_, asio::buffer(command_),
                     [self](beast::error_code error, std::size_t) {
                       if (!error && self->command_[0] == 5 &&
                           self->command_[1] == 1 && self->command_[3] == 3) {
                         self->read_domain_size();
                       }
                     });
  }

  void read_domain_size() {
    auto self = shared_from_this();
    asio::async_read(client_, asio::buffer(domain_size_),
                     [self](beast::error_code error, std::size_t) {
                       if (!error && self->domain_size_[0] > 0) {
                         self->domain_.resize(self->domain_size_[0]);
                         self->read_domain();
                       }
                     });
  }

  void read_domain() {
    auto self = shared_from_this();
    asio::async_read(client_, asio::buffer(domain_),
                     [self](beast::error_code error, std::size_t) {
                       if (!error) {
                         self->remote_dns_.store(self->domain_ == "localhost");
                         self->read_port();
                       }
                     });
  }

  void read_port() {
    auto self = shared_from_this();
    asio::async_read(client_, asio::buffer(requested_port_),
                     [self](beast::error_code error, std::size_t) {
                       if (!error) {
                         self->target_.async_connect(
                             self->endpoint_,
                             [self](beast::error_code connected) {
                               if (!connected) {
                                 self->write_success();
                               }
                             });
                       }
                     });
  }

  void write_success() {
    auto self = shared_from_this();
    asio::async_write(client_, asio::buffer(success_),
                      [self](beast::error_code error, std::size_t) {
                        if (!error) {
                          self->read_client();
                          self->read_target();
                        }
                      });
  }

  void read_client() {
    auto self = shared_from_this();
    client_.async_read_some(
        asio::buffer(client_buffer_),
        [self](beast::error_code error, const std::size_t bytes) {
          if (error) {
            self->close();
            return;
          }
          asio::async_write(self->target_,
                            asio::buffer(self->client_buffer_, bytes),
                            [self](beast::error_code written, std::size_t) {
                              if (written) {
                                self->close();
                              } else {
                                self->read_client();
                              }
                            });
        });
  }

  void read_target() {
    auto self = shared_from_this();
    target_.async_read_some(
        asio::buffer(target_buffer_),
        [self](beast::error_code error, const std::size_t bytes) {
          if (error) {
            self->close();
            return;
          }
          asio::async_write(self->client_,
                            asio::buffer(self->target_buffer_, bytes),
                            [self](beast::error_code written, std::size_t) {
                              if (written) {
                                self->close();
                              } else {
                                self->read_target();
                              }
                            });
        });
  }

  void close() {
    boost::system::error_code ignored;
    client_.close(ignored);
    target_.close(ignored);
  }

  tcp::socket client_;
  tcp::socket target_;
  tcp::endpoint endpoint_;
  std::atomic_bool &remote_dns_;
  std::array<unsigned char, 2> greeting_{};
  std::vector<unsigned char> methods_;
  const std::array<unsigned char, 2> method_{5, 0};
  std::array<unsigned char, 4> command_{};
  std::array<unsigned char, 1> domain_size_{};
  std::string domain_;
  std::array<unsigned char, 2> requested_port_{};
  const std::array<unsigned char, 10> success_{5, 0, 0, 1, 127, 0, 0, 1, 0, 1};
  std::array<char, 8192> client_buffer_{};
  std::array<char, 8192> target_buffer_{};
};

class LocalSocks5Proxy final {
public:
  explicit LocalSocks5Proxy(const tcp::endpoint target)
      : target_(target),
        acceptor_(ioc_, {asio::ip::make_address("127.0.0.1"), 0}),
        guard_(asio::make_work_guard(ioc_)) {}

  ~LocalSocks5Proxy() { stop(); }

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
      guard_.reset();
    });
    thread_.join();
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return acceptor_.local_endpoint().port();
  }

  [[nodiscard]] auto used_remote_dns() const noexcept -> bool {
    return remote_dns_.load();
  }

private:
  void accept() {
    acceptor_.async_accept([this](beast::error_code error, tcp::socket socket) {
      if (!error) {
        std::make_shared<Socks5Session>(std::move(socket), target_, remote_dns_)
            ->start();
      }
      if (acceptor_.is_open()) {
        accept();
      }
    });
  }

  asio::io_context ioc_;
  tcp::endpoint target_;
  tcp::acceptor acceptor_;
  asio::executor_work_guard<asio::io_context::executor_type> guard_;
  std::thread thread_;
  std::atomic_bool remote_dns_{false};
};

auto request(std::string url, const std::uint64_t body_limit = 8192)
    -> obcx::network::detail::CurlRequest {
  return {
      .method = obcx::network::detail::CurlHttpMethod::Get,
      .url = std::move(url),
      .headers = {},
      .body = {},
      .connect_timeout = 1s,
      .total_timeout = 2s,
      .maximum_response_bytes = body_limit,
      .maximum_header_bytes = 16 * 1024,
      .ca_bundle = std::nullopt,
      .proxy = std::nullopt,
  };
}

class ScopedEnvironment final {
public:
  ScopedEnvironment(std::string name, const char *value)
      : name_(std::move(name)) {
    if (const auto *existing = std::getenv(name_.c_str())) {
      previous_ = existing;
    }
    if (value == nullptr) {
      unsetenv(name_.c_str());
    } else {
      setenv(name_.c_str(), value, 1);
    }
  }

  ~ScopedEnvironment() {
    if (previous_.has_value()) {
      setenv(name_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  std::optional<std::string> previous_;
};

auto ca_bundle() -> std::string {
  return (fs::path{OBCX_CURL_TLS_FIXTURE_DIR} / "ca.pem").string();
}

auto proxy_settings(const obcx::network::detail::CurlProxyKind kind,
                    std::string host, const std::uint16_t port,
                    std::optional<std::string> ca = std::nullopt)
    -> obcx::network::detail::CurlProxySettings {
  return {
      .kind = kind,
      .host = std::move(host),
      .port = port,
      .username = std::nullopt,
      .password = std::nullopt,
      .ca_bundle = std::move(ca),
  };
}

TEST(CurlAsioMultiTest, LinkedRuntimeHasRequiredCapabilities) {
  const auto capabilities = obcx::network::detail::curl_runtime_capabilities();
  EXPECT_FALSE(capabilities.version.empty());
  EXPECT_FALSE(capabilities.ssl_backend.empty());
  EXPECT_TRUE(capabilities.asynchronous_dns);
  EXPECT_TRUE(capabilities.tls);
  EXPECT_TRUE(capabilities.https_proxy);
  EXPECT_TRUE(capabilities.http);
  EXPECT_TRUE(capabilities.https);
  EXPECT_TRUE(capabilities.socks5_hostname);
}

TEST(CurlAsioMultiTest, TransfersConcurrentRequestsOnThreadPoolExecutor) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(2);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto first = asio::co_spawn(
      pool, driver->perform(request(server.url("/one"))), asio::use_future);
  auto second = asio::co_spawn(
      pool, driver->perform(request(server.url("/two"))), asio::use_future);
  EXPECT_EQ(first.get().body, "ok:/one");
  EXPECT_EQ(second.get().body, "ok:/two");
  EXPECT_EQ(server.requests(), 2);
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, CompletionReturnsToAssociatedExecutor) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool driver_pool(1);
  asio::io_context caller;
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      driver_pool.get_executor());
  auto result = asio::co_spawn(
      caller,
      [driver,
       url = server.url("/executor")]() -> asio::awaitable<std::thread::id> {
        (void)co_await driver->perform(request(url));
        co_return std::this_thread::get_id();
      },
      asio::use_future);
  std::thread caller_thread([&caller] { caller.run(); });
  const auto caller_id = caller_thread.get_id();
  EXPECT_EQ(result.get(), caller_id);
  caller_thread.join();
  driver->shutdown();
  driver_pool.join();
}

TEST(CurlAsioMultiTest, VerifiesOriginCertificateAndHostname) {
  LocalTlsOrigin origin;
  origin.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());

  auto trusted_request = request(origin.url());
  trusted_request.ca_bundle = ca_bundle();
  auto trusted = asio::co_spawn(
      pool, driver->perform(std::move(trusted_request)), asio::use_future);
  EXPECT_EQ(trusted.get().body, "secure");

  auto untrusted = asio::co_spawn(pool, driver->perform(request(origin.url())),
                                  asio::use_future);
  EXPECT_THROW((void)untrusted.get(), boost::system::system_error);

  auto mismatched_request = request(origin.url("127.0.0.1"));
  mismatched_request.ca_bundle = ca_bundle();
  auto mismatched = asio::co_spawn(
      pool, driver->perform(std::move(mismatched_request)), asio::use_future);
  EXPECT_THROW((void)mismatched.get(), boost::system::system_error);
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, DisabledProxyOverridesAmbientEnvironment) {
  ScopedEnvironment all_proxy("ALL_PROXY", "http://127.0.0.1:1");
  ScopedEnvironment no_proxy("NO_PROXY", nullptr);
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto result = asio::co_spawn(
      pool, driver->perform(request(server.url("/direct"))), asio::use_future);
  EXPECT_EQ(result.get().body, "ok:/direct");
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, ReachesTlsOriginThroughHttpConnectProxy) {
  LocalTlsOrigin origin;
  origin.start();
  LocalConnectProxy proxy({asio::ip::make_address("127.0.0.1"), origin.port()},
                          false);
  proxy.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  ScopedEnvironment no_proxy("NO_PROXY", "*");
  auto value = request(origin.url());
  value.ca_bundle = ca_bundle();
  value.proxy = proxy_settings(obcx::network::detail::CurlProxyKind::Http,
                               proxy.host(), proxy.port());
  auto result =
      asio::co_spawn(pool, driver->perform(std::move(value)), asio::use_future);
  EXPECT_EQ(result.get().body, "secure");
  EXPECT_EQ(proxy.connect_requests(), 1);
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, PreservesBothTlsLayersThroughHttpsProxy) {
  LocalTlsOrigin origin;
  origin.start();
  LocalConnectProxy proxy({asio::ip::make_address("127.0.0.1"), origin.port()},
                          true);
  proxy.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto value = request(origin.url());
  value.ca_bundle = ca_bundle();
  value.proxy = proxy_settings(obcx::network::detail::CurlProxyKind::Https,
                               proxy.host(), proxy.port(), ca_bundle());
  auto result =
      asio::co_spawn(pool, driver->perform(std::move(value)), asio::use_future);
  EXPECT_EQ(result.get().body, "secure");
  EXPECT_EQ(proxy.connect_requests(), 1);
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, VerifiesHttpsProxyCertificate) {
  LocalTlsOrigin origin;
  origin.start();
  LocalConnectProxy proxy({asio::ip::make_address("127.0.0.1"), origin.port()},
                          true);
  proxy.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto value = request(origin.url());
  value.ca_bundle = ca_bundle();
  value.proxy = proxy_settings(obcx::network::detail::CurlProxyKind::Https,
                               proxy.host(), proxy.port());
  auto result =
      asio::co_spawn(pool, driver->perform(std::move(value)), asio::use_future);
  EXPECT_THROW((void)result.get(), boost::system::system_error);
  EXPECT_EQ(proxy.connect_requests(), 0);

  auto mismatched = request(origin.url());
  mismatched.ca_bundle = ca_bundle();
  mismatched.proxy = proxy_settings(obcx::network::detail::CurlProxyKind::Https,
                                    "127.0.0.1", proxy.port(), ca_bundle());
  auto mismatch_result = asio::co_spawn(
      pool, driver->perform(std::move(mismatched)), asio::use_future);
  EXPECT_THROW((void)mismatch_result.get(), boost::system::system_error);
  EXPECT_EQ(proxy.connect_requests(), 0);
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, UsesSocks5ProxySideDns) {
  LocalTlsOrigin origin;
  origin.start();
  LocalSocks5Proxy proxy({asio::ip::make_address("127.0.0.1"), origin.port()});
  proxy.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto value = request(origin.url());
  value.ca_bundle = ca_bundle();
  value.proxy =
      proxy_settings(obcx::network::detail::CurlProxyKind::Socks5Hostname,
                     "127.0.0.1", proxy.port());
  auto result =
      asio::co_spawn(pool, driver->perform(std::move(value)), asio::use_future);
  EXPECT_EQ(result.get().body, "secure");
  EXPECT_TRUE(proxy.used_remote_dns());
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, BoundsDecodedCompressedResponse) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto accepted =
      asio::co_spawn(pool, driver->perform(request(server.url("/gzip"), 4096)),
                     asio::use_future);
  EXPECT_EQ(accepted.get().body, std::string(4096, 'x'));
  auto rejected =
      asio::co_spawn(pool, driver->perform(request(server.url("/gzip"), 4095)),
                     asio::use_future);
  try {
    (void)rejected.get();
    FAIL() << "decoded response should exceed the selected bound";
  } catch (const boost::system::system_error &error) {
    EXPECT_EQ(error.code(), asio::error::message_size);
  }
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, BoundsPlainBodyAndResponseHeaders) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto body_result =
      asio::co_spawn(pool, driver->perform(request(server.url("/large"), 4095)),
                     asio::use_future);
  EXPECT_THROW(
      {
        try {
          (void)body_result.get();
        } catch (const boost::system::system_error &error) {
          EXPECT_EQ(error.code(), asio::error::message_size);
          throw;
        }
      },
      boost::system::system_error);

  auto header_request = request(server.url("/large-header"));
  header_request.maximum_header_bytes = 128;
  auto header_result = asio::co_spawn(
      pool, driver->perform(std::move(header_request)), asio::use_future);
  EXPECT_THROW(
      {
        try {
          (void)header_result.get();
        } catch (const boost::system::system_error &error) {
          EXPECT_EQ(error.code(), asio::error::message_size);
          throw;
        }
      },
      boost::system::system_error);
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, QueuedShutdownOwnsStateBeforeLazyRequestStarts) {
  asio::io_context owner;
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      owner.get_executor());
  auto operation = driver->perform(request("http://127.0.0.1:1/unused"));
  owner.stop();
  driver->shutdown();
  driver->shutdown();
  driver.reset();
  auto result = asio::co_spawn(owner, std::move(operation), asio::use_future);
  owner.restart();
  owner.run();
  try {
    (void)result.get();
    ADD_FAILURE() << "request queued after shutdown must be cancelled";
  } catch (const boost::system::system_error &error) {
    EXPECT_EQ(error.code(), asio::error::operation_aborted);
  }
}

TEST(CurlAsioMultiTest, QueuedShutdownCancelsAfterExternalOwnerIsReleased) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool owner(1);
  asio::thread_pool caller(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      owner.get_executor());
  auto result = asio::co_spawn(
      caller, driver->perform(request(server.url("/hang"))), asio::use_future);
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (server.requests() == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(server.requests(), 1);
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto release_future = release.get_future();
  asio::post(owner, [&] {
    entered.set_value();
    release_future.wait();
  });
  entered_future.wait();
  driver->shutdown();
  driver->shutdown();
  driver.reset();
  release.set_value();
  EXPECT_EQ(result.wait_for(2s), std::future_status::ready);
  try {
    (void)result.get();
    ADD_FAILURE() << "shutdown must complete an admitted transfer";
  } catch (const boost::system::system_error &error) {
    EXPECT_EQ(error.code(), asio::error::operation_aborted);
  }
  caller.join();
  owner.join();
}

TEST(CurlAsioMultiTest, CancellationCompletesOnceAndReleasesTransfer) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  asio::cancellation_signal cancellation;
  auto result = asio::co_spawn(
      pool, driver->perform(request(server.url("/hang"))),
      asio::bind_cancellation_slot(cancellation.slot(), asio::use_future));
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (server.requests() == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(server.requests(), 1);
  cancellation.emit(asio::cancellation_type::terminal);
  try {
    (void)result.get();
    FAIL() << "cancelled transfer should fail";
  } catch (const boost::system::system_error &error) {
    EXPECT_EQ(error.code(), asio::error::operation_aborted);
  }
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, CancellationRaceDoesNotCorruptLaterTransfers) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(2);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  for (int iteration = 0; iteration < 50; ++iteration) {
    asio::cancellation_signal cancellation;
    auto cancelled = asio::co_spawn(
        pool, driver->perform(request(server.url("/hang"))),
        asio::bind_cancellation_slot(cancellation.slot(), asio::use_future));
    const auto expected_request = iteration * 2 + 1;
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (server.requests() < expected_request &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    ASSERT_EQ(server.requests(), expected_request);
    cancellation.emit(asio::cancellation_type::terminal);
    try {
      (void)cancelled.get();
    } catch (const boost::system::system_error &error) {
      EXPECT_EQ(error.code(), asio::error::operation_aborted);
    }
    auto next = asio::co_spawn(
        pool, driver->perform(request(server.url("/after-cancel"))),
        asio::use_future);
    EXPECT_EQ(next.get().body, "ok:/after-cancel");
  }
  driver->shutdown();
  pool.join();
}

TEST(CurlAsioMultiTest, TimesOutWithoutBlockingExecutor) {
  LocalHttpServer server;
  server.start();
  asio::thread_pool pool(1);
  auto driver = std::make_shared<obcx::network::detail::CurlAsioMulti>(
      pool.get_executor());
  auto timed_request = request(server.url("/hang"));
  timed_request.total_timeout = 50ms;
  auto result = asio::co_spawn(pool, driver->perform(std::move(timed_request)),
                               asio::use_future);
  try {
    (void)result.get();
    FAIL() << "hanging transfer should time out";
  } catch (const boost::system::system_error &error) {
    EXPECT_EQ(error.code(), asio::error::timed_out);
  }
  driver->shutdown();
  pool.join();
}

} // namespace
