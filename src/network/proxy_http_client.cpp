#include "network/proxy_http_client.hpp"
#include "common/logger.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <openssl/ssl.h>
#include <sstream>

namespace obcx::network {

namespace {
// Base64 encoding helper for HTTP proxy authentication
auto base64_encode(const std::string &input) -> std::string {
  static constexpr char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  size_t i = 0;
  const auto *data = reinterpret_cast<const unsigned char *>(input.data());
  size_t len = input.size();

  while (i < len) {
    uint32_t octet_a = i < len ? data[i++] : 0;
    uint32_t octet_b = i < len ? data[i++] : 0;
    uint32_t octet_c = i < len ? data[i++] : 0;

    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    output += table[(triple >> 18) & 0x3F];
    output += table[(triple >> 12) & 0x3F];
    output += (i > len + 1) ? '=' : table[(triple >> 6) & 0x3F];
    output += (i > len) ? '=' : table[triple & 0x3F];
  }

  return output;
}
} // namespace

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

ProxyHttpClient::ProxyHttpClient(asio::io_context &ioc,
                                 ProxyConfig proxy_config,
                                 const common::ConnectionConfig &config)
    : HttpClient(ioc, config), ioc_(ioc),
      proxy_config_(std::move(proxy_config)), target_host_(config.host),
      target_port_(config.port) {
  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_CLIENT_CREATED,
                  proxy_config_.host, proxy_config_.port, target_host_,
                  target_port_);
}

// ============================================================
// 新的协程异步API实现
// ============================================================

auto ProxyHttpClient::post(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  try {
    // 建立代理隧道
    auto tunnel_stream = co_await connect_through_proxy_async();

    // 构建HTTP请求
    http::request<http::string_body> req{http::verb::post, std::string(path),
                                         11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");
    req.set(http::field::content_type, "application/json");

    // 设置请求头
    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    // 设置请求体
    if (!body.empty()) {
      req.body() = body;
      req.prepare_payload();
    }

    HttpResponse response;

    // 如果目标端口是443，需要使用SSL
    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(tunnel_stream),
                                                      ssl_ctx);

      // 设置SNI
      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_I18N_WARN(common::LogMessageKey::PROXY_TARGET_SNI_FAILED,
                       target_host_);
      }

      // SSL握手
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                          asio::use_awaitable);

      // 发送请求
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_write(ssl_stream, req, asio::use_awaitable);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_read(ssl_stream, buffer, res, asio::use_awaitable);

      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      // 普通HTTP
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_write(tunnel_stream, req, asio::use_awaitable);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_read(tunnel_stream, buffer, res,
                                asio::use_awaitable);

      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_POST_FAILED, e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    co_return error_response;
  }
}

auto ProxyHttpClient::get(std::string_view path,
                          const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  try {
    // 建立代理隧道
    auto tunnel_stream = co_await connect_through_proxy_async();

    // 构建HTTP请求
    http::request<http::string_body> req{http::verb::get, std::string(path),
                                         11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");

    // 设置请求头
    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    HttpResponse response;

    // 如果目标端口是443，需要使用SSL
    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(tunnel_stream),
                                                      ssl_ctx);

      // 设置SNI
      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_I18N_WARN(common::LogMessageKey::PROXY_TARGET_SNI_FAILED,
                       target_host_);
      }

      // SSL握手
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                          asio::use_awaitable);

      // 发送请求
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_write(ssl_stream, req, asio::use_awaitable);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_read(ssl_stream, buffer, res, asio::use_awaitable);

      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      // 普通HTTP
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_write(tunnel_stream, req, asio::use_awaitable);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_read(tunnel_stream, buffer, res,
                                asio::use_awaitable);

      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_GET_FAILED, e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    co_return error_response;
  }
}

auto ProxyHttpClient::head(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  try {
    // 建立代理隧道
    auto tunnel_stream = co_await connect_through_proxy_async();

    // 构建HTTP请求
    http::request<http::string_body> req{http::verb::head, std::string(path),
                                         11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");

    // 设置请求头
    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    HttpResponse response;

    // 如果目标端口是443，需要使用SSL
    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client};
      ssl_ctx.set_verify_mode(ssl::verify_none);
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      beast::ssl_stream<beast::tcp_stream> ssl_stream(std::move(tunnel_stream),
                                                      ssl_ctx);

      // 设置SNI
      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_I18N_WARN(common::LogMessageKey::PROXY_TARGET_SNI_FAILED,
                       target_host_);
      }

      // SSL握手
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                          asio::use_awaitable);

      // 发送请求
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
      co_await http::async_write(ssl_stream, req, asio::use_awaitable);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());

      boost::system::error_code ec;
      co_await http::async_read(ssl_stream, buffer, res,
                                asio::redirect_error(asio::use_awaitable, ec));

      // HEAD响应可能没有body，忽略某些错误
      if (ec && ec != http::error::end_of_stream &&
          ec != http::error::partial_message) {
        throw boost::system::system_error(ec);
      }

      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);

    } else {
      // 普通HTTP
      tunnel_stream.expires_after(get_timeout());
      co_await http::async_write(tunnel_stream, req, asio::use_awaitable);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      tunnel_stream.expires_after(get_timeout());

      boost::system::error_code ec;
      co_await http::async_read(tunnel_stream, buffer, res,
                                asio::redirect_error(asio::use_awaitable, ec));

      // HEAD响应可能没有body，忽略某些错误
      if (ec && ec != http::error::end_of_stream &&
          ec != http::error::partial_message) {
        throw boost::system::system_error(ec);
      }

      response.status_code = static_cast<unsigned int>(res.result_int());
      response.body = res.body();
      response.raw_response = std::move(res);
    }

    co_return response;

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_GET_FAILED, e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    co_return error_response;
  }
}

// ============================================================
// 协程版本的代理隧道建立方法
// ============================================================

auto ProxyHttpClient::connect_through_proxy_async()
    -> asio::awaitable<beast::tcp_stream> {
  auto executor = co_await asio::this_coro::executor;

  // 解析代理地址
  tcp::resolver resolver(executor);
  auto proxy_results = co_await resolver.async_resolve(
      proxy_config_.host, std::to_string(proxy_config_.port),
      asio::use_awaitable);

  // 创建TCP流
  beast::tcp_stream stream(executor);

  // 连接到代理服务器
  stream.expires_after(get_timeout());
  co_await stream.async_connect(proxy_results, asio::use_awaitable);

  // 根据代理类型建立隧道
  switch (proxy_config_.type) {
  case ProxyType::HTTP:
    co_await establish_http_tunnel_async(stream);
    co_return stream;

  case ProxyType::HTTPS: {
    // HTTPS代理：先与代理建立SSL连接，然后发送CONNECT
    ssl::context ssl_ctx{ssl::context::tlsv12_client};
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(ssl::verify_none);
    ssl_ctx.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                        ssl::context::single_dh_use);

    beast::ssl_stream<beast::tcp_stream> ssl_stream{std::move(stream), ssl_ctx};

    // 设置SNI
    if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                  proxy_config_.host.c_str())) {
      OBCX_I18N_WARN(common::LogMessageKey::PROXY_HTTPS_SNI_FAILED,
                     proxy_config_.host);
    }

    // SSL握手
    beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
    co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                        asio::use_awaitable);

    OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_HTTPS_SSL_SUCCESS);
    co_return co_await establish_https_tunnel_async(ssl_stream);
  }

  case ProxyType::SOCKS5:
    co_await establish_socks5_tunnel_async(stream);
    co_return stream;

  default:
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_UNSUPPORTED_TYPE));
  }
}

auto ProxyHttpClient::establish_http_tunnel_async(beast::tcp_stream &stream)
    -> asio::awaitable<void> {
  // 构建CONNECT请求
  std::string connect_target =
      target_host_ + ":" + std::to_string(target_port_);
  std::ostringstream connect_request;
  connect_request << "CONNECT " << connect_target << " HTTP/1.1\r\n";
  connect_request << "Host: " << connect_target << "\r\n";
  connect_request << "User-Agent: OBCX/1.0\r\n";
  connect_request << "Proxy-Connection: keep-alive\r\n";

  // 添加代理认证（如果需要）
  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_request << "Proxy-Authorization: Basic "
                    << base64_encode(credentials) << "\r\n";
  }

  connect_request << "\r\n"; // 结束头部

  std::string request_str = connect_request.str();

  // 发送CONNECT请求
  stream.expires_after(get_timeout());
  co_await asio::async_write(stream, asio::buffer(request_str),
                             asio::use_awaitable);

  // 读取响应状态行
  std::string response_data;
  response_data.resize(1024);

  stream.expires_after(get_timeout());
  size_t bytes_read = co_await stream.async_read_some(
      asio::buffer(response_data), asio::use_awaitable);

  response_data.resize(bytes_read);

  // 检查响应是否包含200状态码
  if (response_data.find("200") == std::string::npos) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_CONNECT_FAILED_EXCEPTION, response_data));
  }

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_RESPONSE_HEADER, response_data);

  // 隧道建立成功后，取消超时定时器，避免影响后续SSL操作
  stream.expires_never();
}

auto ProxyHttpClient::establish_https_tunnel_async(
    beast::ssl_stream<beast::tcp_stream> &ssl_stream)
    -> asio::awaitable<beast::tcp_stream> {
  // 构建CONNECT请求
  std::string connect_target =
      target_host_ + ":" + std::to_string(target_port_);
  http::request<http::string_body> connect_req{http::verb::connect,
                                               connect_target, 11};
  connect_req.set(http::field::host, connect_target);
  connect_req.set(http::field::user_agent, "OBCX/1.0");
  connect_req.set(http::field::proxy_connection, "keep-alive");
  connect_req.set(http::field::connection, "keep-alive");

  // 添加代理认证（如果需要）
  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_req.set(http::field::proxy_authorization,
                    "Basic " + base64_encode(credentials));
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_HTTPS_CONNECT_SEND,
                  connect_target);

  // 通过SSL连接发送CONNECT请求
  beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
  co_await http::async_write(ssl_stream, connect_req, asio::use_awaitable);

  // 读取CONNECT响应
  beast::flat_buffer buffer;
  http::response<http::string_body> connect_response;
  beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
  co_await http::async_read(ssl_stream, buffer, connect_response,
                            asio::use_awaitable);

  // 检查CONNECT响应
  if (connect_response.result() != http::status::ok) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_HTTPS_CONNECT_ERROR,
                    static_cast<int>(connect_response.result()),
                    connect_response.reason(), connect_response.body());
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_STATUS_ERROR,
        static_cast<int>(connect_response.result())));
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_HTTPS_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host_,
                  target_port_);

  // 隧道建立成功后，取消超时定时器，避免影响后续SSL操作
  beast::get_lowest_layer(ssl_stream).expires_never();

  // 返回底层TCP流
  co_return std::move(beast::get_lowest_layer(ssl_stream));
}

auto ProxyHttpClient::establish_socks5_tunnel_async(beast::tcp_stream &stream)
    -> asio::awaitable<void> {
  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_START,
                  proxy_config_.host, target_host_, target_port_);

  // SOCKS5 握手: 发送初始请求
  std::vector<uint8_t> greeting;
  greeting.push_back(0x05); // SOCKS version 5

  if (proxy_config_.username && proxy_config_.password) {
    greeting.push_back(0x02); // 两种认证方法
    greeting.push_back(0x00); // 无认证
    greeting.push_back(0x02); // 用户名/密码认证
  } else {
    greeting.push_back(0x01); // 一种认证方法
    greeting.push_back(0x00); // 无认证
  }

  stream.expires_after(get_timeout());
  co_await asio::async_write(stream, asio::buffer(greeting),
                             asio::use_awaitable);

  // 读取服务器响应
  std::vector<uint8_t> response(2);
  stream.expires_after(get_timeout());
  co_await asio::async_read(stream, asio::buffer(response),
                            asio::use_awaitable);

  if (response[0] != 0x05) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_SOCKS5_VERSION_MISMATCH));
  }

  // 处理认证
  if (response[1] == 0x02) {
    // 用户名/密码认证
    if (!proxy_config_.username || !proxy_config_.password) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::PROXY_AUTH_REQUIRED));
    }

    std::vector<uint8_t> auth_req;
    auth_req.push_back(0x01); // 认证版本
    auth_req.push_back(static_cast<uint8_t>(proxy_config_.username->length()));
    auth_req.insert(auth_req.end(), proxy_config_.username->begin(),
                    proxy_config_.username->end());
    auth_req.push_back(static_cast<uint8_t>(proxy_config_.password->length()));
    auth_req.insert(auth_req.end(), proxy_config_.password->begin(),
                    proxy_config_.password->end());

    stream.expires_after(get_timeout());
    co_await asio::async_write(stream, asio::buffer(auth_req),
                               asio::use_awaitable);

    std::vector<uint8_t> auth_resp(2);
    stream.expires_after(get_timeout());
    co_await asio::async_read(stream, asio::buffer(auth_resp),
                              asio::use_awaitable);

    if (auth_resp[1] != 0x00) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::PROXY_SOCKS5_AUTH_FAILED));
    }
  } else if (response[1] != 0x00) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_SOCKS5_UNSUPPORTED_AUTH));
  }

  // 发送连接请求
  std::vector<uint8_t> connect_req;
  connect_req.push_back(0x05); // SOCKS版本
  connect_req.push_back(0x01); // CONNECT命令
  connect_req.push_back(0x00); // 保留字段
  connect_req.push_back(0x03); // 域名类型
  connect_req.push_back(static_cast<uint8_t>(target_host_.length()));
  connect_req.insert(connect_req.end(), target_host_.begin(),
                     target_host_.end());
  connect_req.push_back(static_cast<uint8_t>(target_port_ >> 8));
  connect_req.push_back(static_cast<uint8_t>(target_port_ & 0xFF));

  stream.expires_after(get_timeout());
  co_await asio::async_write(stream, asio::buffer(connect_req),
                             asio::use_awaitable);

  // 读取连接响应
  std::vector<uint8_t> connect_resp(4);
  stream.expires_after(get_timeout());
  co_await asio::async_read(stream, asio::buffer(connect_resp),
                            asio::use_awaitable);

  if (connect_resp[0] != 0x05 || connect_resp[1] != 0x00) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_CONNECT_FAILED_EXCEPTION,
        std::to_string(connect_resp[1])));
  }

  // 根据地址类型读取剩余部分
  size_t remaining_bytes = 0;
  if (connect_resp[3] == 0x01) {        // IPv4
    remaining_bytes = 6;                // 4字节IP + 2字节端口
  } else if (connect_resp[3] == 0x03) { // 域名
    std::vector<uint8_t> domain_len(1);
    stream.expires_after(get_timeout());
    co_await asio::async_read(stream, asio::buffer(domain_len),
                              asio::use_awaitable);
    remaining_bytes = domain_len[0] + 2; // 域名长度 + 2字节端口
  } else if (connect_resp[3] == 0x04) {  // IPv6
    remaining_bytes = 18;                // 16字节IP + 2字节端口
  }

  if (remaining_bytes > 0) {
    std::vector<uint8_t> addr_data(remaining_bytes);
    stream.expires_after(get_timeout());
    co_await asio::async_read(stream, asio::buffer(addr_data),
                              asio::use_awaitable);
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host_,
                  target_port_);

  // 隧道建立成功后，取消超时定时器，避免影响后续SSL操作
  stream.expires_never();
}

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

auto ProxyHttpClient::post_sync(
    std::string_view path, std::string_view body,
    const std::map<std::string, std::string> &headers) -> HttpResponse {

  try {
    // 建立代理隧道
    auto tunnel_socket = connect_through_proxy();

    // 通过隧道发送POST请求
    return send_http_request(tunnel_socket, "POST", std::string(path),
                             std::string(body), headers);
  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_POST_FAILED, e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    return error_response;
  }
}

auto ProxyHttpClient::get_sync(
    std::string_view path, const std::map<std::string, std::string> &headers)
    -> HttpResponse {

  try {
    // 建立代理隧道
    auto tunnel_socket = connect_through_proxy();

    // 通过隧道发送GET请求
    return send_http_request(tunnel_socket, "GET", std::string(path), "",
                             headers);
  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_GET_FAILED, e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    return error_response;
  }
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void ProxyHttpClient::close() {
  // ProxyHttpClient的close实现
  // 代理客户端每次请求都是新的连接，所以这里不需要特殊处理
  HttpClient::close();
}

auto ProxyHttpClient::connect_through_proxy() -> tcp::socket {
  // 解析代理地址
  tcp::resolver resolver(ioc_);
  auto proxy_results =
      resolver.resolve(proxy_config_.host, std::to_string(proxy_config_.port));

  // 根据代理类型建立连接
  switch (proxy_config_.type) {
  case ProxyType::HTTP: {
    // 普通HTTP代理连接
    tcp::socket proxy_socket(ioc_);
    asio::connect(proxy_socket, proxy_results);
    return establish_http_tunnel(proxy_socket, target_host_, target_port_);
  }
  case ProxyType::HTTPS: {
    // HTTPS代理：先与代理建立SSL连接，然后发送CONNECT
    tcp::socket plain_socket(ioc_);
    asio::connect(plain_socket, proxy_results);

    // 建立与代理服务器的SSL连接
    ssl::context ssl_ctx{ssl::context::tlsv12_client};
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(ssl::verify_none);
    ssl_ctx.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                        ssl::context::single_dh_use);

    ssl::stream<tcp::socket> ssl_socket{std::move(plain_socket), ssl_ctx};

    // 设置SNI
    if (!SSL_set_tlsext_host_name(ssl_socket.native_handle(),
                                  proxy_config_.host.c_str())) {
      OBCX_I18N_WARN(common::LogMessageKey::PROXY_HTTPS_SNI_FAILED,
                     proxy_config_.host);
    }

    // SSL握手
    boost::system::error_code ec;
    ssl_socket.handshake(ssl::stream_base::client, ec);
    if (ec) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::PROXY_HTTPS_SSL_HANDSHAKE_FAILED_EXCEPTION,
          ec.message()));
    }

    OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_HTTPS_SSL_SUCCESS);
    return establish_https_tunnel(ssl_socket, target_host_, target_port_);
  }
  case ProxyType::SOCKS5: {
    tcp::socket proxy_socket(ioc_);
    asio::connect(proxy_socket, proxy_results);
    return establish_socks5_tunnel(proxy_socket, target_host_, target_port_);
  }
  default:
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_UNSUPPORTED_TYPE));
  }
}

tcp::socket ProxyHttpClient::establish_http_tunnel(
    tcp::socket &proxy_socket, const std::string &target_host,
    uint16_t target_port) {
  // 使用原始字符串构建CONNECT请求，避免Beast的HTTP库可能的兼容性问题
  std::string connect_target = target_host + ":" + std::to_string(target_port);
  std::ostringstream connect_request;
  connect_request << "CONNECT " << connect_target << " HTTP/1.1\r\n";
  connect_request << "Host: " << connect_target << "\r\n";
  connect_request << "User-Agent: OBCX/1.0\r\n";
  connect_request << "Proxy-Connection: keep-alive\r\n";

  // 添加代理认证（如果需要）
  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_request << "Proxy-Authorization: Basic "
                    << base64_encode(credentials) << "\r\n";
  }

  connect_request << "\r\n"; // 结束头部

  std::string request_str = connect_request.str();

  boost::system::error_code ec;
  asio::write(proxy_socket, asio::buffer(request_str), ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_CONNECT_REQUEST_FAILED, ec.message()));
  }

  std::string response_line;
  char ch = 0;
  while (asio::read(proxy_socket, asio::buffer(&ch, 1), ec) && !ec) {
    if (ch == '\n') {
      if (!response_line.empty() && response_line.back() == '\r') {
        response_line.pop_back(); // 移除\r
      }
      break;
    }
    response_line += ch;
  }

  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_CONNECT_RESPONSE_READ_FAILED,
        ec.message()));
  }

  if (response_line.find("200") == std::string::npos) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_CONNECT_FAILED_EXCEPTION, response_line));
  }

  // 读取并丢弃响应头
  std::string header_line;
  while (true) {
    header_line.clear();
    while (asio::read(proxy_socket, asio::buffer(&ch, 1), ec) && !ec) {
      if (ch == '\n') {
        if (!header_line.empty() && header_line.back() == '\r') {
          header_line.pop_back();
        }
        break;
      }
      header_line += ch;
    }

    if (ec || header_line.empty()) {
      break; // 空行表示头部结束
    }

    OBCX_I18N_TRACE(common::LogMessageKey::PROXY_RESPONSE_HEADER, header_line);
  }

  return std::move(proxy_socket);
}

auto ProxyHttpClient::send_http_request(
    tcp::socket &tunnel_socket, const std::string &method,
    const std::string &path, const std::string &body,
    const std::map<std::string, std::string> &headers) -> HttpResponse {
  try {
    // 构建HTTP请求
    http::verb verb_type =
        (method == "GET") ? http::verb::get : http::verb::post;
    http::request<http::string_body> req{verb_type, path, 11};
    req.set(http::field::host, target_host_);
    req.set(http::field::user_agent, "OBCX/1.0");

    // 设置请求头
    for (const auto &[name, value] : headers) {
      req.set(name, value);
    }

    // 设置请求体
    if (!body.empty()) {
      req.set(http::field::content_type, "application/json");
      req.body() = body;
      req.prepare_payload();
    }

    // 如果目标端口是443，需要使用SSL
    if (target_port_ == 443) {
      ssl::context ssl_ctx{ssl::context::tls_client}; // 使用TLS客户端上下文
      ssl_ctx.set_verify_mode(ssl::verify_none); // 跳过证书验证以避免代理问题

      // 设置更宽松的SSL选项以提高兼容性和稳定性
      ssl_ctx.set_options(ssl::context::default_workarounds |
                          ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                          ssl::context::single_dh_use);

      // 设置超时选项以避免连接挂起（使用配置的超时时间）
      auto timeout_sec =
          std::chrono::duration_cast<std::chrono::seconds>(get_timeout())
              .count();
      SSL_CTX_set_timeout(ssl_ctx.native_handle(),
                          static_cast<long>(timeout_sec));

      ssl::stream<tcp::socket> ssl_stream{std::move(tunnel_socket), ssl_ctx};

      // 设置SNI（Server Name Indication）
      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_I18N_WARN(common::LogMessageKey::PROXY_TARGET_SNI_FAILED,
                       target_host_);
      }

      // SSL握手，使用增强的错误处理和重试逻辑
      boost::system::error_code ec;
      int max_retries = 3;
      for (int retry = 0; retry < max_retries; ++retry) {
        auto _ = ssl_stream.handshake(ssl::stream_base::client, ec);
        if (!ec) {
          OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_SSL_HANDSHAKE_SUCCESS,
                          retry);
          break;
        }

        OBCX_I18N_WARN(common::LogMessageKey::PROXY_SSL_HANDSHAKE_FAILED_RETRY,
                       retry + 1, max_retries, ec.message());

        if (retry < max_retries - 1) {
          // 指数退避重试策略：100ms, 200ms, 400ms (每次翻倍)
          auto wait_time = std::chrono::milliseconds(1000 << retry);
          OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_RETRY_WAIT,
                          wait_time.count());
          std::this_thread::sleep_for(wait_time);

          // 如果是stream truncated错误，可能需要重新创建连接
          if (ec.message().find("stream truncated") != std::string::npos) {
            OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_STREAM_TRUNCATED);
            // TODO: implement this
          }
        } else {
          throw std::runtime_error(common::I18nLogMessages::format_message(
              common::LogMessageKey::
                  PROXY_SSL_HANDSHAKE_FAILED_MAX_RETRIES_EXCEPTION,
              max_retries, ec.message()));
        }
      }

      // 发送请求，使用错误处理
      boost::system::error_code write_ec;
      http::write(ssl_stream, req, write_ec);
      if (write_ec) {
        throw std::runtime_error(common::I18nLogMessages::format_message(
            common::LogMessageKey::PROXY_SSL_SEND_FAILED, write_ec.message()));
      }

      // 读取响应，使用错误处理
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      boost::system::error_code read_ec;
      http::read(ssl_stream, buffer, res, read_ec);
      if (read_ec) {
        throw std::runtime_error(common::I18nLogMessages::format_message(
            common::LogMessageKey::PROXY_SSL_READ_FAILED, read_ec.message()));
      }

      // 创建HttpResponse
      HttpResponse result;
      result.status_code = static_cast<unsigned int>(res.result_int());
      result.body = res.body();
      result.raw_response = std::move(res);

      return result;
    } else {
      // 普通HTTP
      http::write(tunnel_socket, req);

      // 读取响应
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      http::read(tunnel_socket, buffer, res);

      // 创建HttpResponse
      HttpResponse result;
      result.status_code = static_cast<unsigned int>(res.result_int());
      result.body = res.body();
      result.raw_response = std::move(res);

      return result;
    }
  } catch (const std::exception &e) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTP_SEND_FAILED, e.what()));
  }
}

auto ProxyHttpClient::establish_https_tunnel(
    ssl::stream<tcp::socket> &ssl_socket, const std::string &target_host,
    uint16_t target_port) -> tcp::socket {
  // 构建CONNECT请求
  std::string connect_target = target_host + ":" + std::to_string(target_port);
  http::request<http::string_body> connect_req{http::verb::connect,
                                               connect_target, 11};
  connect_req.set(http::field::host, connect_target);
  connect_req.set(http::field::user_agent, "OBCX/1.0");
  connect_req.set(http::field::proxy_connection, "keep-alive");
  connect_req.set(http::field::connection, "keep-alive");

  // 添加代理认证（如果需要）
  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_req.set(http::field::proxy_authorization,
                    "Basic " + base64_encode(credentials));
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_HTTPS_CONNECT_SEND,
                  connect_target);

  // 通过SSL连接发送CONNECT请求
  boost::system::error_code ec;
  http::write(ssl_socket, connect_req, ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_REQUEST_FAILED,
        ec.message()));
  }

  // 读取CONNECT响应
  beast::flat_buffer buffer;
  http::response<http::string_body> connect_response;
  http::read(ssl_socket, buffer, connect_response, ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_RESPONSE_FAILED,
        ec.message()));
  }

  // 检查CONNECT响应
  if (connect_response.result() != http::status::ok) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_HTTPS_CONNECT_ERROR,
                    static_cast<int>(connect_response.result()),
                    connect_response.reason(), connect_response.body());
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_STATUS_ERROR,
        static_cast<int>(connect_response.result())));
  }

  // 清空buffer中的任何额外数据
  buffer.consume(buffer.size());

  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_HTTPS_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host,
                  target_port);

  // 返回底层socket，现在它已经通过SSL代理建立了到目标的隧道
  return std::move(ssl_socket.next_layer());
}

auto ProxyHttpClient::establish_socks5_tunnel(tcp::socket &proxy_socket,
                                              const std::string &target_host,
                                              uint16_t target_port)
    -> tcp::socket {
  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_START,
                  proxy_config_.host, target_host, target_port);

  boost::system::error_code ec;

  // SOCKS5 握手: 发送初始请求
  std::vector<uint8_t> greeting;
  greeting.push_back(0x05); // SOCKS version 5

  if (proxy_config_.username && proxy_config_.password) {
    greeting.push_back(0x02); // 两种认证方法
    greeting.push_back(0x00); // 无认证
    greeting.push_back(0x02); // 用户名/密码认证
  } else {
    greeting.push_back(0x01); // 一种认证方法
    greeting.push_back(0x00); // 无认证
  }

  asio::write(proxy_socket, asio::buffer(greeting), ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_HANDSHAKE_FAILED, ec.message()));
  }

  // 读取服务器响应
  std::vector<uint8_t> response(2);
  asio::read(proxy_socket, asio::buffer(response), ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_RESPONSE_FAILED, ec.message()));
  }

  if (response[0] != 0x05) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_SOCKS5_VERSION_MISMATCH));
  }

  // 处理认证
  if (response[1] == 0x02) {
    // 用户名/密码认证
    if (!proxy_config_.username || !proxy_config_.password) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::PROXY_AUTH_REQUIRED));
    }

    std::vector<uint8_t> auth_req;
    auth_req.push_back(0x01); // 认证版本
    auth_req.push_back(static_cast<uint8_t>(proxy_config_.username->length()));
    auth_req.insert(auth_req.end(), proxy_config_.username->begin(),
                    proxy_config_.username->end());
    auth_req.push_back(static_cast<uint8_t>(proxy_config_.password->length()));
    auth_req.insert(auth_req.end(), proxy_config_.password->begin(),
                    proxy_config_.password->end());

    asio::write(proxy_socket, asio::buffer(auth_req), ec);
    if (ec) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::PROXY_SOCKS5_AUTH_REQUEST_FAILED,
          ec.message()));
    }

    std::vector<uint8_t> auth_resp(2);
    asio::read(proxy_socket, asio::buffer(auth_resp), ec);
    if (ec) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::PROXY_SOCKS5_AUTH_RESPONSE_FAILED,
          ec.message()));
    }

    if (auth_resp[1] != 0x00) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::PROXY_SOCKS5_AUTH_FAILED));
    }
  } else if (response[1] != 0x00) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_SOCKS5_UNSUPPORTED_AUTH));
  }

  // 发送连接请求
  std::vector<uint8_t> connect_req;
  connect_req.push_back(0x05); // SOCKS版本
  connect_req.push_back(0x01); // CONNECT命令
  connect_req.push_back(0x00); // 保留字段
  connect_req.push_back(0x03); // 域名类型
  connect_req.push_back(static_cast<uint8_t>(target_host.length()));
  connect_req.insert(connect_req.end(), target_host.begin(), target_host.end());
  connect_req.push_back(static_cast<uint8_t>(target_port >> 8));
  connect_req.push_back(static_cast<uint8_t>(target_port & 0xFF));

  asio::write(proxy_socket, asio::buffer(connect_req), ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_CONNECT_REQUEST_FAILED,
        ec.message()));
  }

  // 读取连接响应
  std::vector<uint8_t> connect_resp(10);                       // 最少10字节
  asio::read(proxy_socket, asio::buffer(connect_resp, 4), ec); // 先读前4字节
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_CONNECT_RESPONSE_FAILED,
        ec.message()));
  }

  if (connect_resp[0] != 0x05 || connect_resp[1] != 0x00) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_CONNECT_FAILED_EXCEPTION,
        std::to_string(connect_resp[1])));
  }

  // 根据地址类型读取剩余部分
  size_t remaining_bytes = 0;
  if (connect_resp[3] == 0x01) {        // IPv4
    remaining_bytes = 6;                // 4字节IP + 2字节端口
  } else if (connect_resp[3] == 0x03) { // 域名
    asio::read(proxy_socket, asio::buffer(&connect_resp[4], 1), ec);
    remaining_bytes = connect_resp[4] + 2; // 域名长度 + 2字节端口
  } else if (connect_resp[3] == 0x04) {    // IPv6
    remaining_bytes = 18;                  // 16字节IP + 2字节端口
  }

  if (remaining_bytes > 0) {
    std::vector<uint8_t> addr_data(remaining_bytes);
    asio::read(proxy_socket, asio::buffer(addr_data), ec);
    if (ec) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::PROXY_SOCKS5_ADDRESS_READ_FAILED,
          ec.message()));
    }
  }

  OBCX_I18N_DEBUG(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host,
                  target_port);

  return std::move(proxy_socket);
}

} // namespace obcx::network
