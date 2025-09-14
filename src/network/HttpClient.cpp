#include "network/HttpClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <cstdlib>
#include <iostream>
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
  std::chrono::milliseconds timeout{30000};
  std::optional<ssl::context> ssl_ctx;

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
  OBCX_INFO("HTTP Client initialized for {}:{}", config.host, config.port);
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
  std::thread([this, promise, path, body, headers]() {
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
  std::thread([this, promise, path, headers]() {
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
  std::thread([this, promise, path, headers]() {
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
  OBCX_DEBUG("POST {} with body: {}", path, body);

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

    // 判断是否需要使用HTTPS
    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      // HTTPS请求
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      // 创建SSL流
      tcp::resolver resolver(pimpl_->ioc);
      ssl::stream<tcp::socket> stream(pimpl_->ioc, *pimpl_->ssl_ctx);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 连接并握手
      asio::connect(stream.next_layer(), results.begin(), results.end());
      stream.handshake(ssl::stream_base::client);

      // 发送请求
      http::write(stream, req);

      // 接收响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(stream, buffer, res);

      // 设置响应
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    } else {
      // HTTP请求
      tcp::resolver resolver(pimpl_->ioc);
      tcp::socket socket(pimpl_->ioc);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 连接
      asio::connect(socket, results.begin(), results.end());

      // 发送请求
      http::write(socket, req);

      // 接收响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(socket, buffer, res);

      // 设置响应
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    OBCX_DEBUG("Received response with status code: {}", response.status_code);
    OBCX_DEBUG("Response body: {}", response.body);

    return response;
  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP POST request failed: {}", e.what());
    throw HttpClientError(std::string("HTTP POST request failed: ") + e.what());
  }
}

auto HttpClient::get_sync(std::string_view path,
                          const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  OBCX_DEBUG("GET {}", path);

  try {
    // 创建请求
    http::request<http::string_body> req{http::verb::get, path, 11};
    req.set(http::field::host, pimpl_->config.host);

    // 添加头部
    prepare_request(req, headers);

    HttpResponse response;

    // 判断是否需要使用HTTPS
    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      // HTTPS请求
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      // 创建SSL流
      tcp::resolver resolver(pimpl_->ioc);
      ssl::stream<tcp::socket> stream(pimpl_->ioc, *pimpl_->ssl_ctx);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 连接并握手
      asio::connect(stream.next_layer(), results.begin(), results.end());
      stream.handshake(ssl::stream_base::client);

      // 发送请求
      http::write(stream, req);

      // 接收响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(stream, buffer, res);

      // 设置响应
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    } else {
      // HTTP请求
      tcp::resolver resolver(pimpl_->ioc);
      tcp::socket socket(pimpl_->ioc);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 连接
      asio::connect(socket, results.begin(), results.end());

      // 发送请求
      http::write(socket, req);

      // 接收响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(socket, buffer, res);

      // 设置响应
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    OBCX_DEBUG("Received response with status code: {}", response.status_code);
    OBCX_DEBUG("Response body: {}", response.body);

    return response;
  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP GET request failed: {}", e.what());
    throw HttpClientError(std::string("HTTP GET request failed: ") + e.what());
  }
}

auto HttpClient::head_sync(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  try {
    // 创建请求
    http::request<http::string_body> req{http::verb::head, path, 11};
    req.set(http::field::host, pimpl_->config.host);

    // 添加头部
    prepare_request(req, headers);

    HttpResponse response;

    // 判断是否需要使用HTTPS
    if (pimpl_->config.port == 443 || pimpl_->config.use_ssl) {
      // HTTPS请求
      if (!pimpl_->ssl_ctx) {
        throw HttpClientError("SSL context not initialized for HTTPS request");
      }

      // 创建SSL流
      tcp::resolver resolver(pimpl_->ioc);
      ssl::stream<tcp::socket> stream(pimpl_->ioc, *pimpl_->ssl_ctx);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 连接并握手
      asio::connect(stream.next_layer(), results.begin(), results.end());
      stream.handshake(ssl::stream_base::client);

      // 发送请求
      http::write(stream, req);

      // 接收响应 - HEAD请求特殊处理
      beast::flat_buffer buffer;
      http::response<http::string_body> res;

      // HEAD响应可能没有body或连接提前关闭，需要处理partial message错误
      boost::system::error_code ec;
      http::read(stream, buffer, res, ec);

      if (ec && ec != http::error::end_of_stream &&
          ec != beast::http::error::partial_message) {
        throw beast::system_error{ec};
      }

      // 设置响应
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    } else {
      // HTTP请求
      tcp::resolver resolver(pimpl_->ioc);
      tcp::socket socket(pimpl_->ioc);

      // 解析主机名
      auto const results = resolver.resolve(
          pimpl_->config.host, std::to_string(pimpl_->config.port));

      // 连接
      asio::connect(socket, results.begin(), results.end());

      // 发送请求
      http::write(socket, req);

      // 接收响应 - HEAD请求特殊处理
      beast::flat_buffer buffer;
      http::response<http::string_body> res;

      // HEAD响应可能没有body或连接提前关闭，需要处理partial message错误
      boost::system::error_code ec;
      http::read(socket, buffer, res, ec);

      if (ec && ec != http::error::end_of_stream &&
          ec != beast::http::error::partial_message) {
        throw beast::system_error{ec};
      }

      // 设置响应
      response.status_code = res.result_int();
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    return response;
  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP HEAD request failed: {}", e.what());
    throw HttpClientError(std::string("HTTP HEAD request failed: ") + e.what());
  }
}

void HttpClient::set_timeout(std::chrono::milliseconds timeout) {
  pimpl_->timeout = timeout;
}

auto HttpClient::is_connected() const -> bool {
  // 简单实现，总是返回true
  return true;
}

void HttpClient::close() { OBCX_INFO("HTTP Client closed"); }

auto HttpClientFactory::create(asio::io_context &ioc,
                               const common::ConnectionConfig &config)
    -> std::unique_ptr<HttpClient> {
  return std::make_unique<HttpClient>(ioc, config);
}

} // namespace obcx::network