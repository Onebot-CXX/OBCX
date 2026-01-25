#include "network/http_client.hpp"
#include "common/logger.hpp"

#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

struct HttpClient::Impl {
  asio::io_context &ioc;
  common::ConnectionConfig config;
  std::optional<ssl::context> ssl_ctx;
  std::atomic<bool> connected{false};

  Impl(asio::io_context &io, common::ConnectionConfig cfg)
      : ioc(io), config(std::move(cfg)) {
    // 如果是HTTPS连接，初始化SSL上下文
    if (config.port == 443 || config.use_ssl) {
      ssl_ctx.emplace(ssl::context::tlsv12_client);
      ssl_ctx->set_verify_mode(ssl::verify_none);
    }
  }
};

HttpClient::HttpClient(asio::io_context &ioc,
                       const common::ConnectionConfig &config)
    : pimpl_(std::make_unique<Impl>(ioc, config)) {
  OBCX_I18N_INFO(common::LogMessageKey::HTTP_CLIENT_INIT, config.host,
                 config.port);
}

HttpClient::~HttpClient() = default;

template <typename RequestType>
void HttpClient::prepare_request(
    RequestType &request, const std::map<std::string, std::string> &headers) {
  // 设置默认User-Agent (现代Firefox)
  if (!request.count(http::field::user_agent)) {
    request.set(http::field::user_agent,
                "Mozilla/5.0 (X11; Linux x86_64; rv:142.0) Gecko/20100101 "
                "Firefox/142.0");
  }

  // 设置浏览器标准头部
  if (!request.count(http::field::accept)) {
    request.set(
        http::field::accept,
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
  }

  if (!request.count(http::field::accept_language)) {
    request.set(http::field::accept_language, "en-US,en;q=0.5");
  }

  if (!request.count(http::field::accept_encoding)) {
    request.set(http::field::accept_encoding, "gzip, deflate, br, zstd");
  }

  // 隐私和安全头部
  if (!request.count("DNT")) {
    request.set("DNT", "1");
  }

  if (!request.count("Sec-GPC")) {
    request.set("Sec-GPC", "1");
  }

  if (!request.count(http::field::connection)) {
    request.set(http::field::connection, "keep-alive");
  }

  if (!request.count("Upgrade-Insecure-Requests")) {
    request.set("Upgrade-Insecure-Requests", "1");
  }

  if (!request.count("Sec-Fetch-Dest")) {
    request.set("Sec-Fetch-Dest", "document");
  }

  if (!request.count("Sec-Fetch-Mode")) {
    request.set("Sec-Fetch-Mode", "navigate");
  }

  if (!request.count("Sec-Fetch-Site")) {
    request.set("Sec-Fetch-Site", "cross-site");
  }

  if (!request.count("Priority")) {
    request.set("Priority", "u=0, i");
  }

  // 缓存控制头部
  if (!request.count(http::field::pragma)) {
    request.set(http::field::pragma, "no-cache");
  }

  if (!request.count(http::field::cache_control)) {
    request.set(http::field::cache_control, "no-cache");
  }

  // 设置默认Content-Type (仅当有body时)
  if (!request.count(http::field::content_type) && !request.body().empty()) {
    request.set(http::field::content_type, "application/json");
  }

  // 设置访问令牌
  if (!pimpl_->config.access_token.empty()) {
    request.set(http::field::authorization,
                "Bot " + pimpl_->config.access_token);
  }

  // 添加自定义头部 (会覆盖默认头部)
  for (const auto &[key, value] : headers) {
    request.set(key, value);
  }
}

auto HttpClient::post_async(std::string_view path, std::string_view body,
                            const std::map<std::string, std::string> &headers)
    -> std::future<HttpResponse> {
  auto promise = std::make_shared<std::promise<HttpResponse>>();
  auto future = promise->get_future();

  // 在单独的线程中执行同步请求
  std::thread([this, promise, path = std::string(path),
               body = std::string(body), headers]() {
    try {
      auto response = post_sync(path, body, headers);
      promise->set_value(response);
    } catch (const std::exception &e) {
      promise->set_exception(std::current_exception());
    }
  }).detach();

  return future;
}

auto HttpClient::get_async(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> std::future<HttpResponse> {
  auto promise = std::make_shared<std::promise<HttpResponse>>();
  auto future = promise->get_future();

  // 在单独的线程中执行同步请求
  std::thread([this, promise, path = std::string(path), headers]() {
    try {
      auto response = get_sync(path, headers);
      promise->set_value(response);
    } catch (const std::exception &e) {
      promise->set_exception(std::current_exception());
    }
  }).detach();

  return future;
}

auto HttpClient::head_async(std::string_view path,
                            const std::map<std::string, std::string> &headers)
    -> std::future<HttpResponse> {
  auto promise = std::make_shared<std::promise<HttpResponse>>();
  auto future = promise->get_future();

  // 在单独的线程中执行同步请求
  std::thread([this, promise, path = std::string(path), headers]() {
    try {
      auto response = head_sync(path, headers);
      promise->set_value(response);
    } catch (const std::exception &e) {
      promise->set_exception(std::current_exception());
    }
  }).detach();

  return future;
}

auto HttpClient::post_sync(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  OBCX_I18N_DEBUG_TRACE(common::LogMessageKey::HTTP_POST_DEBUG, path, body);

  // Use a dedicated io_context for this sync operation with timeout
  asio::io_context local_ioc;

  try {
    // 创建请求
    http::request<http::string_body> req{http::verb::post, path, 11};
    req.set(http::field::host, pimpl_->config.host);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    // 添加头部
    prepare_request(req, headers);

    HttpResponse response;
    boost::system::error_code final_ec;
    bool operation_completed = false;

    // 判断是否需要使用HTTPS
    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      // HTTPS请求
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      // 使用beast::tcp_stream以支持expires_after超时
      tcp::resolver resolver(local_ioc);
      beast::ssl_stream<beast::tcp_stream> stream(local_ioc, *pimpl_->ssl_ctx);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 设置超时并异步连接
      beast::get_lowest_layer(stream).expires_after(pimpl_->config.timeout);
      beast::get_lowest_layer(stream).async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              operation_completed = true;
              return;
            }
            pimpl_->connected = true;

            // SSL握手
            beast::get_lowest_layer(stream).expires_after(
                pimpl_->config.timeout);
            stream.async_handshake(
                ssl::stream_base::client, [&](boost::system::error_code ec) {
                  if (ec) {
                    final_ec = ec;
                    operation_completed = true;
                    return;
                  }

                  // 发送请求
                  beast::get_lowest_layer(stream).expires_after(
                      pimpl_->config.timeout);
                  http::async_write(
                      stream, req,
                      [&](boost::system::error_code ec, std::size_t) {
                        if (ec) {
                          final_ec = ec;
                          operation_completed = true;
                          return;
                        }

                        // 接收响应
                        auto buffer = std::make_shared<beast::flat_buffer>();
                        auto res = std::make_shared<
                            http::response<http::string_body>>();
                        beast::get_lowest_layer(stream).expires_after(
                            pimpl_->config.timeout);
                        http::async_read(
                            stream, *buffer, *res,
                            [&, buffer, res](boost::system::error_code ec,
                                             std::size_t) {
                              if (ec) {
                                final_ec = ec;
                              } else {
                                response.status_code = res->result_int();
                                response.body = res->body();
                                response.raw_response = std::move(*res);
                              }
                              operation_completed = true;
                            });
                      });
                });
          });

      // Run io_context until operation completes
      local_ioc.run();

    } else {
      // HTTP请求
      tcp::resolver resolver(local_ioc);
      beast::tcp_stream stream(local_ioc);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 设置超时并异步连接
      stream.expires_after(pimpl_->config.timeout);
      stream.async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              operation_completed = true;
              return;
            }
            pimpl_->connected = true;

            // 发送请求
            stream.expires_after(pimpl_->config.timeout);
            http::async_write(
                stream, req, [&](boost::system::error_code ec, std::size_t) {
                  if (ec) {
                    final_ec = ec;
                    operation_completed = true;
                    return;
                  }

                  // 接收响应
                  auto buffer = std::make_shared<beast::flat_buffer>();
                  auto res =
                      std::make_shared<http::response<http::string_body>>();
                  stream.expires_after(pimpl_->config.timeout);
                  http::async_read(
                      stream, *buffer, *res,
                      [&, buffer, res](boost::system::error_code ec,
                                       std::size_t) {
                        if (ec) {
                          final_ec = ec;
                        } else {
                          response.status_code = res->result_int();
                          response.body = res->body();
                          response.raw_response = std::move(*res);
                        }
                        operation_completed = true;
                      });
                });
          });

      // Run io_context until operation completes
      local_ioc.run();
    }

    if (final_ec) {
      pimpl_->connected = false;
      throw boost::system::system_error(final_ec);
    }

    OBCX_I18N_DEBUG_TRACE(common::LogMessageKey::HTTP_RESPONSE_STATUS,
                          response.status_code);
    OBCX_I18N_DEBUG_TRACE(common::LogMessageKey::HTTP_RESPONSE_BODY,
                          response.body);

    return response;
  } catch (const std::exception &e) {
    pimpl_->connected = false;
    OBCX_I18N_ERROR(common::LogMessageKey::HTTP_POST_FAILED, e.what());
    throw HttpClientError(std::string("HTTP POST request failed: ") + e.what());
  }
}

auto HttpClient::get_sync(std::string_view path,
                          const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  OBCX_I18N_DEBUG_TRACE(common::LogMessageKey::HTTP_GET_DEBUG, path);

  // Use a dedicated io_context for this sync operation with timeout
  asio::io_context local_ioc;

  try {
    // 创建请求
    http::request<http::string_body> req{http::verb::get, path, 11};
    req.set(http::field::host, pimpl_->config.host);

    // 添加头部
    prepare_request(req, headers);

    HttpResponse response;
    boost::system::error_code final_ec;
    bool operation_completed = false;

    // 判断是否需要使用HTTPS
    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      // HTTPS请求
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      // 使用beast::tcp_stream以支持expires_after超时
      tcp::resolver resolver(local_ioc);
      beast::ssl_stream<beast::tcp_stream> stream(local_ioc, *pimpl_->ssl_ctx);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 设置超时并异步连接
      beast::get_lowest_layer(stream).expires_after(pimpl_->config.timeout);
      beast::get_lowest_layer(stream).async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              operation_completed = true;
              return;
            }
            pimpl_->connected = true;

            // SSL握手
            beast::get_lowest_layer(stream).expires_after(
                pimpl_->config.timeout);
            stream.async_handshake(
                ssl::stream_base::client, [&](boost::system::error_code ec) {
                  if (ec) {
                    final_ec = ec;
                    operation_completed = true;
                    return;
                  }

                  // 发送请求
                  beast::get_lowest_layer(stream).expires_after(
                      pimpl_->config.timeout);
                  http::async_write(
                      stream, req,
                      [&](boost::system::error_code ec, std::size_t) {
                        if (ec) {
                          final_ec = ec;
                          operation_completed = true;
                          return;
                        }

                        // 接收响应
                        auto buffer = std::make_shared<beast::flat_buffer>();
                        auto res = std::make_shared<
                            http::response<http::string_body>>();
                        beast::get_lowest_layer(stream).expires_after(
                            pimpl_->config.timeout);
                        http::async_read(
                            stream, *buffer, *res,
                            [&, buffer, res](boost::system::error_code ec,
                                             std::size_t) {
                              if (ec) {
                                final_ec = ec;
                              } else {
                                response.status_code = res->result_int();
                                response.body = res->body();
                                response.raw_response = std::move(*res);
                              }
                              operation_completed = true;
                            });
                      });
                });
          });

      // Run io_context until operation completes
      local_ioc.run();

    } else {
      // HTTP请求
      tcp::resolver resolver(local_ioc);
      beast::tcp_stream stream(local_ioc);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 设置超时并异步连接
      stream.expires_after(pimpl_->config.timeout);
      stream.async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              operation_completed = true;
              return;
            }
            pimpl_->connected = true;

            // 发送请求
            stream.expires_after(pimpl_->config.timeout);
            http::async_write(
                stream, req, [&](boost::system::error_code ec, std::size_t) {
                  if (ec) {
                    final_ec = ec;
                    operation_completed = true;
                    return;
                  }

                  // 接收响应
                  auto buffer = std::make_shared<beast::flat_buffer>();
                  auto res =
                      std::make_shared<http::response<http::string_body>>();
                  stream.expires_after(pimpl_->config.timeout);
                  http::async_read(
                      stream, *buffer, *res,
                      [&, buffer, res](boost::system::error_code ec,
                                       std::size_t) {
                        if (ec) {
                          final_ec = ec;
                        } else {
                          response.status_code = res->result_int();
                          response.body = res->body();
                          response.raw_response = std::move(*res);
                        }
                        operation_completed = true;
                      });
                });
          });

      // Run io_context until operation completes
      local_ioc.run();
    }

    if (final_ec) {
      pimpl_->connected = false;
      throw boost::system::system_error(final_ec);
    }

    OBCX_I18N_DEBUG_TRACE(common::LogMessageKey::HTTP_RESPONSE_STATUS,
                          response.status_code);
    OBCX_I18N_DEBUG_TRACE(common::LogMessageKey::HTTP_RESPONSE_BODY,
                          response.body);

    return response;
  } catch (const std::exception &e) {
    pimpl_->connected = false;
    OBCX_I18N_ERROR(common::LogMessageKey::HTTP_GET_FAILED, e.what());
    throw HttpClientError(std::string("HTTP GET request failed: ") + e.what());
  }
}

auto HttpClient::head_sync(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  // Use a dedicated io_context for this sync operation with timeout
  asio::io_context local_ioc;

  try {
    // 创建请求
    http::request<http::string_body> req{http::verb::head, path, 11};
    req.set(http::field::host, pimpl_->config.host);

    // 添加头部
    prepare_request(req, headers);

    HttpResponse response;
    boost::system::error_code final_ec;
    bool operation_completed = false;

    // 判断是否需要使用HTTPS
    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      // HTTPS请求
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      // 使用beast::tcp_stream以支持expires_after超时
      tcp::resolver resolver(local_ioc);
      beast::ssl_stream<beast::tcp_stream> stream(local_ioc, *pimpl_->ssl_ctx);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 设置超时并异步连接
      beast::get_lowest_layer(stream).expires_after(pimpl_->config.timeout);
      beast::get_lowest_layer(stream).async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              operation_completed = true;
              return;
            }
            pimpl_->connected = true;

            // SSL握手
            beast::get_lowest_layer(stream).expires_after(
                pimpl_->config.timeout);
            stream.async_handshake(
                ssl::stream_base::client, [&](boost::system::error_code ec) {
                  if (ec) {
                    final_ec = ec;
                    operation_completed = true;
                    return;
                  }

                  // 发送请求
                  beast::get_lowest_layer(stream).expires_after(
                      pimpl_->config.timeout);
                  http::async_write(
                      stream, req,
                      [&](boost::system::error_code ec, std::size_t) {
                        if (ec) {
                          final_ec = ec;
                          operation_completed = true;
                          return;
                        }

                        // 接收响应
                        auto buffer = std::make_shared<beast::flat_buffer>();
                        auto res = std::make_shared<
                            http::response<http::string_body>>();
                        beast::get_lowest_layer(stream).expires_after(
                            pimpl_->config.timeout);
                        http::async_read(
                            stream, *buffer, *res,
                            [&, buffer, res](boost::system::error_code ec,
                                             std::size_t) {
                              // HEAD响应可能没有body，忽略某些错误
                              if (ec && ec != http::error::end_of_stream &&
                                  ec != http::error::partial_message) {
                                final_ec = ec;
                              } else {
                                response.status_code = res->result_int();
                                response.body = res->body();
                                response.raw_response = std::move(*res);
                              }
                              operation_completed = true;
                            });
                      });
                });
          });

      // Run io_context until operation completes
      local_ioc.run();

    } else {
      // HTTP请求
      tcp::resolver resolver(local_ioc);
      beast::tcp_stream stream(local_ioc);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 设置超时并异步连接
      stream.expires_after(pimpl_->config.timeout);
      stream.async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              operation_completed = true;
              return;
            }
            pimpl_->connected = true;

            // 发送请求
            stream.expires_after(pimpl_->config.timeout);
            http::async_write(
                stream, req, [&](boost::system::error_code ec, std::size_t) {
                  if (ec) {
                    final_ec = ec;
                    operation_completed = true;
                    return;
                  }

                  // 接收响应
                  auto buffer = std::make_shared<beast::flat_buffer>();
                  auto res =
                      std::make_shared<http::response<http::string_body>>();
                  stream.expires_after(pimpl_->config.timeout);
                  http::async_read(
                      stream, *buffer, *res,
                      [&, buffer, res](boost::system::error_code ec,
                                       std::size_t) {
                        // HEAD响应可能没有body，忽略某些错误
                        if (ec && ec != http::error::end_of_stream &&
                            ec != http::error::partial_message) {
                          final_ec = ec;
                        } else {
                          response.status_code = res->result_int();
                          response.body = res->body();
                          response.raw_response = std::move(*res);
                        }
                        operation_completed = true;
                      });
                });
          });

      // Run io_context until operation completes
      local_ioc.run();
    }

    if (final_ec) {
      pimpl_->connected = false;
      throw boost::system::system_error(final_ec);
    }

    return response;
  } catch (const std::exception &e) {
    pimpl_->connected = false;
    OBCX_I18N_ERROR(common::LogMessageKey::HTTP_HEAD_FAILED, e.what());
    throw HttpClientError(std::string("HTTP HEAD request failed: ") + e.what());
  }
}

void HttpClient::set_timeout(std::chrono::milliseconds timeout) {
  pimpl_->config.timeout = timeout;
}

auto HttpClient::is_connected() const -> bool {
  return pimpl_->connected.load();
}

auto HttpClient::get_timeout() const -> std::chrono::milliseconds {
  return pimpl_->config.timeout;
}

void HttpClient::close() {
  pimpl_->connected = false;
  OBCX_I18N_INFO(common::LogMessageKey::HTTP_CLIENT_CLOSED);
}

} // namespace obcx::network
