#include "common/logger.hpp"
#include "http_client_impl.hpp"
#include "network/compression.hpp"

#include <openssl/ssl.h>

#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <memory>
#include <string>
#include <utility>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

HttpClient::HttpClient(asio::io_context &ioc,
                       const common::ConnectionConfig &config)
    : pimpl_(std::make_unique<Impl>(ioc, config)) {
  OBCX_I18N_INFO(common::LogMessageKey::HTTP_CLIENT_INIT, config.host,
                 config.port);
}

HttpClient::~HttpClient() = default;

// ============================================================
// 新的协程异步API实现
// ============================================================

auto HttpClient::post(std::string_view path, std::string_view body,
                      const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_POST_DEBUG, path, body);

  auto executor = co_await asio::this_coro::executor;

  try {
    http::request<http::string_body> req{http::verb::post, std::string(path),
                                         11};
    req.set(http::field::host, pimpl_->config.host);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    prepare_request(req, headers);

    HttpResponse response;

    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      tcp::resolver resolver(executor);
      beast::ssl_stream<beast::tcp_stream> stream(executor, *pimpl_->ssl_ctx);

      auto results = co_await resolver.async_resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port),
          asio::use_awaitable);

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await beast::get_lowest_layer(stream).async_connect(
          results, asio::use_awaitable);

      pimpl_->connected = true;

      SSL_set_tlsext_host_name(stream.native_handle(),
                               pimpl_->config.host.c_str());

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await stream.async_handshake(ssl::stream_base::client,
                                      asio::use_awaitable);

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await http::async_write(stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await http::async_read(stream, buffer, res, asio::use_awaitable);

      decompress_inplace(res);
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      tcp::resolver resolver(executor);
      beast::tcp_stream stream(executor);

      auto results = co_await resolver.async_resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port),
          asio::use_awaitable);

      stream.expires_after(pimpl_->config.connect_timeout);
      co_await stream.async_connect(results, asio::use_awaitable);

      pimpl_->connected = true;

      stream.expires_after(pimpl_->config.connect_timeout);
      co_await http::async_write(stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      stream.expires_after(pimpl_->config.connect_timeout);
      co_await http::async_read(stream, buffer, res, asio::use_awaitable);

      decompress_inplace(res);
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    OBCX_I18N_TRACE(common::LogMessageKey::HTTP_RESPONSE_STATUS,
                    response.status_code);
    OBCX_I18N_TRACE(common::LogMessageKey::HTTP_RESPONSE_BODY, response.body);

    co_return response;

  } catch (const std::exception &e) {
    pimpl_->connected = false;
    OBCX_I18N_ERROR(common::LogMessageKey::HTTP_POST_FAILED, e.what());
    throw HttpClientError(std::string("HTTP POST request failed: ") + e.what());
  }
}

auto HttpClient::get(std::string_view path,
                     const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_GET_DEBUG, path);

  auto executor = co_await asio::this_coro::executor;

  try {
    http::request<http::string_body> req{http::verb::get, std::string(path),
                                         11};
    req.set(http::field::host, pimpl_->config.host);

    prepare_request(req, headers);

    HttpResponse response;

    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      tcp::resolver resolver(executor);
      beast::ssl_stream<beast::tcp_stream> stream(executor, *pimpl_->ssl_ctx);

      auto results = co_await resolver.async_resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port),
          asio::use_awaitable);

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await beast::get_lowest_layer(stream).async_connect(
          results, asio::use_awaitable);

      pimpl_->connected = true;

      SSL_set_tlsext_host_name(stream.native_handle(),
                               pimpl_->config.host.c_str());

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await stream.async_handshake(ssl::stream_base::client,
                                      asio::use_awaitable);

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await http::async_write(stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await http::async_read(stream, buffer, res, asio::use_awaitable);

      decompress_inplace(res);
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      tcp::resolver resolver(executor);
      beast::tcp_stream stream(executor);

      auto results = co_await resolver.async_resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port),
          asio::use_awaitable);

      stream.expires_after(pimpl_->config.connect_timeout);
      co_await stream.async_connect(results, asio::use_awaitable);

      pimpl_->connected = true;

      stream.expires_after(pimpl_->config.connect_timeout);
      co_await http::async_write(stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      stream.expires_after(pimpl_->config.connect_timeout);
      co_await http::async_read(stream, buffer, res, asio::use_awaitable);

      decompress_inplace(res);
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_RESPONSE_STATUS,
                    response.status_code);
    OBCX_I18N_DEBUG(common::LogMessageKey::HTTP_RESPONSE_BODY, response.body);

    co_return response;

  } catch (const std::exception &e) {
    pimpl_->connected = false;
    OBCX_I18N_ERROR(common::LogMessageKey::HTTP_GET_FAILED, e.what());
    throw HttpClientError(std::string("HTTP GET request failed: ") + e.what());
  }
}

auto HttpClient::head(std::string_view path,
                      const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  auto executor = co_await asio::this_coro::executor;

  try {
    http::request<http::string_body> req{http::verb::head, std::string(path),
                                         11};
    req.set(http::field::host, pimpl_->config.host);

    prepare_request(req, headers);

    HttpResponse response;

    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      tcp::resolver resolver(executor);
      beast::ssl_stream<beast::tcp_stream> stream(executor, *pimpl_->ssl_ctx);

      auto results = co_await resolver.async_resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port),
          asio::use_awaitable);

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await beast::get_lowest_layer(stream).async_connect(
          results, asio::use_awaitable);

      pimpl_->connected = true;

      SSL_set_tlsext_host_name(stream.native_handle(),
                               pimpl_->config.host.c_str());

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await stream.async_handshake(ssl::stream_base::client,
                                      asio::use_awaitable);

      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);
      co_await http::async_write(stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      beast::get_lowest_layer(stream).expires_after(
          pimpl_->config.connect_timeout);

      boost::system::error_code ec;
      co_await http::async_read(stream, buffer, res,
                                asio::redirect_error(asio::use_awaitable, ec));

      if (ec && ec != http::error::end_of_stream &&
          ec != http::error::partial_message) {
        throw boost::system::system_error(ec);
      }

      decompress_inplace(res);
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      tcp::resolver resolver(executor);
      beast::tcp_stream stream(executor);

      auto results = co_await resolver.async_resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port),
          asio::use_awaitable);

      stream.expires_after(pimpl_->config.connect_timeout);
      co_await stream.async_connect(results, asio::use_awaitable);

      pimpl_->connected = true;

      stream.expires_after(pimpl_->config.connect_timeout);
      co_await http::async_write(stream, req, asio::use_awaitable);

      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      stream.expires_after(pimpl_->config.connect_timeout);

      boost::system::error_code ec;
      co_await http::async_read(stream, buffer, res,
                                asio::redirect_error(asio::use_awaitable, ec));

      if (ec && ec != http::error::end_of_stream &&
          ec != http::error::partial_message) {
        throw boost::system::system_error(ec);
      }

      decompress_inplace(res);
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    pimpl_->connected = false;
    OBCX_I18N_ERROR(common::LogMessageKey::HTTP_HEAD_FAILED, e.what());
    throw HttpClientError(std::string("HTTP HEAD request failed: ") + e.what());
  }
}

// ============================================================
// 辅助方法实现
// ============================================================

void HttpClient::set_timeout(std::chrono::milliseconds timeout) {
  pimpl_->config.connect_timeout = timeout;
}

auto HttpClient::is_connected() const -> bool {
  return pimpl_->connected.load();
}

auto HttpClient::get_timeout() const -> std::chrono::milliseconds {
  return pimpl_->config.connect_timeout;
}

auto HttpClient::get_host() const -> const std::string & {
  return pimpl_->config.host;
}

auto HttpClient::get_port() const -> uint16_t { return pimpl_->config.port; }

auto HttpClient::use_ssl() const -> bool {
  return pimpl_->config.port == 443 || pimpl_->config.use_ssl;
}

auto HttpClient::get_ssl_context() const -> ssl::context * {
  if (pimpl_->ssl_ctx) {
    return &(*pimpl_->ssl_ctx);
  }
  return nullptr;
}

void HttpClient::close() {
  pimpl_->connected = false;
  OBCX_I18N_INFO(common::LogMessageKey::HTTP_CLIENT_CLOSED);
}

} // namespace obcx::network
