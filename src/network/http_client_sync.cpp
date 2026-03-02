#include "common/logger.hpp"
#include "http_client_impl.hpp"
#include "network/compression.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

// ============================================================
// 已弃用的同步API实现（保留以便向后兼容）
// ============================================================

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

auto HttpClient::post_sync(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_POST_DEBUG, path, body);

  asio::io_context local_ioc;

  try {
    http::request<http::string_body> req{http::verb::post, std::string(path),
                                         11};
    req.set(http::field::host, pimpl_->config.host);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    prepare_request(req, headers);

    HttpResponse response;
    boost::system::error_code final_ec;

    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      tcp::resolver resolver(local_ioc);
      beast::ssl_stream<beast::tcp_stream> stream(local_ioc, *pimpl_->ssl_ctx);

      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      beast::get_lowest_layer(stream).async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              return;
            }
            pimpl_->connected = true;

            beast::get_lowest_layer(stream).expires_after(
                pimpl_->config.connect_timeout);
            stream.async_handshake(
                ssl::stream_base::client, [&](boost::system::error_code ec) {
                  if (ec) {
                    final_ec = ec;
                    return;
                  }

                  beast::get_lowest_layer(stream).expires_after(
                      pimpl_->config.connect_timeout);
                  http::async_write(
                      stream, req,
                      [&](boost::system::error_code ec, std::size_t) {
                        if (ec) {
                          final_ec = ec;
                          return;
                        }

                        auto buffer = std::make_shared<beast::flat_buffer>();
                        auto res = std::make_shared<
                            http::response<http::string_body>>();
                        beast::get_lowest_layer(stream).expires_after(
                            pimpl_->config.connect_timeout);
                        http::async_read(
                            stream, *buffer, *res,
                            [&, buffer, res](boost::system::error_code ec,
                                             std::size_t) {
                              if (ec) {
                                final_ec = ec;
                              } else {
                                decompress_inplace(*res);
                                response.status_code = res->result_int();
                                response.body = res->body();
                                response.raw_response = std::move(*res);
                              }
                            });
                      });
                });
          });

      local_ioc.run();

    } else {
      tcp::resolver resolver(local_ioc);
      beast::tcp_stream stream(local_ioc);

      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      stream.expires_after(pimpl_->config.connect_timeout);
      stream.async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              return;
            }
            pimpl_->connected = true;

            stream.expires_after(pimpl_->config.connect_timeout);
            http::async_write(
                stream, req, [&](boost::system::error_code ec, std::size_t) {
                  if (ec) {
                    final_ec = ec;
                    return;
                  }

                  auto buffer = std::make_shared<beast::flat_buffer>();
                  auto res =
                      std::make_shared<http::response<http::string_body>>();
                  stream.expires_after(pimpl_->config.connect_timeout);
                  http::async_read(
                      stream, *buffer, *res,
                      [&, buffer, res](boost::system::error_code ec,
                                       std::size_t) {
                        if (ec) {
                          final_ec = ec;
                        } else {
                          decompress_inplace(*res);
                          response.status_code = res->result_int();
                          response.body = res->body();
                          response.raw_response = std::move(*res);
                        }
                      });
                });
          });

      local_ioc.run();
    }

    if (final_ec) {
      pimpl_->connected = false;
      throw boost::system::system_error(final_ec);
    }

    OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_RESPONSE_STATUS,
                    response.status_code);
    OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_RESPONSE_BODY, response.body);

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
  OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_GET_DEBUG, path);

  asio::io_context local_ioc;

  try {
    http::request<http::string_body> req{http::verb::get, std::string(path),
                                         11};
    req.set(http::field::host, pimpl_->config.host);

    prepare_request(req, headers);

    HttpResponse response;
    boost::system::error_code final_ec;

    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      tcp::resolver resolver(local_ioc);
      beast::ssl_stream<beast::tcp_stream> stream(local_ioc, *pimpl_->ssl_ctx);

      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      beast::get_lowest_layer(stream).async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              return;
            }
            pimpl_->connected = true;

            beast::get_lowest_layer(stream).expires_after(
                pimpl_->config.connect_timeout);
            stream.async_handshake(
                ssl::stream_base::client, [&](boost::system::error_code ec) {
                  if (ec) {
                    final_ec = ec;
                    return;
                  }

                  beast::get_lowest_layer(stream).expires_after(
                      pimpl_->config.connect_timeout);
                  http::async_write(
                      stream, req,
                      [&](boost::system::error_code ec, std::size_t) {
                        if (ec) {
                          final_ec = ec;
                          return;
                        }

                        auto buffer = std::make_shared<beast::flat_buffer>();
                        auto res = std::make_shared<
                            http::response<http::string_body>>();
                        beast::get_lowest_layer(stream).expires_after(
                            pimpl_->config.connect_timeout);
                        http::async_read(
                            stream, *buffer, *res,
                            [&, buffer, res](boost::system::error_code ec,
                                             std::size_t) {
                              if (ec) {
                                final_ec = ec;
                              } else {
                                decompress_inplace(*res);
                                response.status_code = res->result_int();
                                response.body = res->body();
                                response.raw_response = std::move(*res);
                              }
                            });
                      });
                });
          });

      local_ioc.run();

    } else {
      tcp::resolver resolver(local_ioc);
      beast::tcp_stream stream(local_ioc);

      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      stream.expires_after(pimpl_->config.connect_timeout);
      stream.async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              return;
            }
            pimpl_->connected = true;

            stream.expires_after(pimpl_->config.connect_timeout);
            http::async_write(
                stream, req, [&](boost::system::error_code ec, std::size_t) {
                  if (ec) {
                    final_ec = ec;
                    return;
                  }

                  auto buffer = std::make_shared<beast::flat_buffer>();
                  auto res =
                      std::make_shared<http::response<http::string_body>>();
                  stream.expires_after(pimpl_->config.connect_timeout);
                  http::async_read(
                      stream, *buffer, *res,
                      [&, buffer, res](boost::system::error_code ec,
                                       std::size_t) {
                        if (ec) {
                          final_ec = ec;
                        } else {
                          decompress_inplace(*res);
                          response.status_code = res->result_int();
                          response.body = res->body();
                          response.raw_response = std::move(*res);
                        }
                      });
                });
          });

      local_ioc.run();
    }

    if (final_ec) {
      pimpl_->connected = false;
      throw boost::system::system_error(final_ec);
    }

    OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_RESPONSE_STATUS,
                    response.status_code);
    OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_RESPONSE_BODY, response.body);

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
  asio::io_context local_ioc;

  try {
    http::request<http::string_body> req{http::verb::head, std::string(path),
                                         11};
    req.set(http::field::host, pimpl_->config.host);

    prepare_request(req, headers);

    HttpResponse response;
    boost::system::error_code final_ec;

    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      tcp::resolver resolver(local_ioc);
      beast::ssl_stream<beast::tcp_stream> stream(local_ioc, *pimpl_->ssl_ctx);

      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      beast::get_lowest_layer(stream).async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              return;
            }
            pimpl_->connected = true;

            beast::get_lowest_layer(stream).expires_after(
                pimpl_->config.connect_timeout);
            stream.async_handshake(
                ssl::stream_base::client, [&](boost::system::error_code ec) {
                  if (ec) {
                    final_ec = ec;
                    return;
                  }

                  beast::get_lowest_layer(stream).expires_after(
                      pimpl_->config.connect_timeout);
                  http::async_write(
                      stream, req,
                      [&](boost::system::error_code ec, std::size_t) {
                        if (ec) {
                          final_ec = ec;
                          return;
                        }

                        auto buffer = std::make_shared<beast::flat_buffer>();
                        auto res = std::make_shared<
                            http::response<http::string_body>>();
                        beast::get_lowest_layer(stream).expires_after(
                            pimpl_->config.connect_timeout);
                        http::async_read(
                            stream, *buffer, *res,
                            [&, buffer, res](boost::system::error_code ec,
                                             std::size_t) {
                              if (ec && ec != http::error::end_of_stream &&
                                  ec != http::error::partial_message) {
                                final_ec = ec;
                              } else {
                                decompress_inplace(*res);
                                response.status_code = res->result_int();
                                response.body = res->body();
                                response.raw_response = std::move(*res);
                              }
                            });
                      });
                });
          });

      local_ioc.run();

    } else {
      tcp::resolver resolver(local_ioc);
      beast::tcp_stream stream(local_ioc);

      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      stream.expires_after(pimpl_->config.connect_timeout);
      stream.async_connect(
          results, [&](boost::system::error_code ec,
                       tcp::resolver::results_type::endpoint_type) {
            if (ec) {
              final_ec = ec;
              return;
            }
            pimpl_->connected = true;

            stream.expires_after(pimpl_->config.connect_timeout);
            http::async_write(
                stream, req, [&](boost::system::error_code ec, std::size_t) {
                  if (ec) {
                    final_ec = ec;
                    return;
                  }

                  auto buffer = std::make_shared<beast::flat_buffer>();
                  auto res =
                      std::make_shared<http::response<http::string_body>>();
                  stream.expires_after(pimpl_->config.connect_timeout);
                  http::async_read(
                      stream, *buffer, *res,
                      [&, buffer, res](boost::system::error_code ec,
                                       std::size_t) {
                        if (ec && ec != http::error::end_of_stream &&
                            ec != http::error::partial_message) {
                          final_ec = ec;
                        } else {
                          decompress_inplace(*res);
                          response.status_code = res->result_int();
                          response.body = res->body();
                          response.raw_response = std::move(*res);
                        }
                      });
                });
          });

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

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace obcx::network
