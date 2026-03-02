#include "common/logger.hpp"
#include "network/proxy_http_client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <openssl/ssl.h>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

namespace {

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

// ============================================================
// 协程版本的代理隧道建立方法
// ============================================================

auto ProxyHttpClient::connect_through_proxy_async()
    -> asio::awaitable<beast::tcp_stream> {
  auto executor = co_await asio::this_coro::executor;

  tcp::resolver resolver(executor);
  auto proxy_results = co_await resolver.async_resolve(
      proxy_config_.host, std::to_string(proxy_config_.port),
      asio::use_awaitable);

  beast::tcp_stream stream(executor);

  stream.expires_after(get_timeout());
  co_await stream.async_connect(proxy_results, asio::use_awaitable);

  switch (proxy_config_.type) {
  case ProxyType::HTTP:
    co_await establish_http_tunnel_async(stream);
    co_return stream;

  case ProxyType::HTTPS: {
    ssl::context ssl_ctx{ssl::context::tls_client};
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(ssl::verify_none);
    ssl_ctx.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                        ssl::context::single_dh_use);

    beast::ssl_stream<beast::tcp_stream> ssl_stream{std::move(stream), ssl_ctx};

    if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                  proxy_config_.host.c_str())) {
      OBCX_I18N_WARN(common::LogMessageKey::PROXY_HTTPS_SNI_FAILED,
                     proxy_config_.host);
    }

    beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
    co_await ssl_stream.async_handshake(ssl::stream_base::client,
                                        asio::use_awaitable);

    OBCX_I18N_TRACE(common::LogMessageKey::PROXY_HTTPS_SSL_SUCCESS);
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
  std::string connect_target =
      target_host_ + ":" + std::to_string(target_port_);
  std::ostringstream connect_request;
  connect_request << "CONNECT " << connect_target << " HTTP/1.1\r\n";
  connect_request << "Host: " << connect_target << "\r\n";
  connect_request << "User-Agent: OBCX/1.0\r\n";
  connect_request << "Proxy-Connection: keep-alive\r\n";

  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_request << "Proxy-Authorization: Basic "
                    << base64_encode(credentials) << "\r\n";
  }

  connect_request << "\r\n";

  std::string request_str = connect_request.str();

  stream.expires_after(get_timeout());
  co_await asio::async_write(stream, asio::buffer(request_str),
                             asio::use_awaitable);

  beast::flat_buffer buffer;
  http::response<http::string_body> connect_response;
  stream.expires_after(get_timeout());
  co_await http::async_read(stream, buffer, connect_response,
                            asio::use_awaitable);

  if (connect_response.result() != http::status::ok) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_HTTPS_CONNECT_ERROR,
                    static_cast<int>(connect_response.result()),
                    connect_response.reason(), connect_response.body());
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_CONNECT_FAILED_EXCEPTION,
        std::string(connect_response.reason())));
  }

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_RESPONSE_HEADER,
                  std::string(connect_response.reason()));

  stream.expires_never();
}

auto ProxyHttpClient::establish_https_tunnel_async(
    beast::ssl_stream<beast::tcp_stream> &ssl_stream)
    -> asio::awaitable<beast::tcp_stream> {
  std::string connect_target =
      target_host_ + ":" + std::to_string(target_port_);
  http::request<http::string_body> connect_req{http::verb::connect,
                                               connect_target, 11};
  connect_req.set(http::field::host, connect_target);
  connect_req.set(http::field::user_agent, "OBCX/1.0");
  connect_req.set(http::field::proxy_connection, "keep-alive");
  connect_req.set(http::field::connection, "keep-alive");

  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_req.set(http::field::proxy_authorization,
                    "Basic " + base64_encode(credentials));
  }

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_HTTPS_CONNECT_SEND,
                  connect_target);

  beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
  co_await http::async_write(ssl_stream, connect_req, asio::use_awaitable);

  beast::flat_buffer buffer;
  http::response<http::string_body> connect_response;
  beast::get_lowest_layer(ssl_stream).expires_after(get_timeout());
  co_await http::async_read(ssl_stream, buffer, connect_response,
                            asio::use_awaitable);

  if (connect_response.result() != http::status::ok) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_HTTPS_CONNECT_ERROR,
                    static_cast<int>(connect_response.result()),
                    connect_response.reason(), connect_response.body());
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_STATUS_ERROR,
        static_cast<int>(connect_response.result())));
  }

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_HTTPS_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host_,
                  target_port_);

  beast::get_lowest_layer(ssl_stream).expires_never();

  co_return std::move(beast::get_lowest_layer(ssl_stream));
}

auto ProxyHttpClient::establish_socks5_tunnel_async(beast::tcp_stream &stream)
    -> asio::awaitable<void> {
  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_START,
                  proxy_config_.host, target_host_, target_port_);

  std::vector<uint8_t> greeting;
  greeting.push_back(0x05);

  if (proxy_config_.username && proxy_config_.password) {
    greeting.push_back(0x02);
    greeting.push_back(0x00);
    greeting.push_back(0x02);
  } else {
    greeting.push_back(0x01);
    greeting.push_back(0x00);
  }

  stream.expires_after(get_timeout());
  co_await asio::async_write(stream, asio::buffer(greeting),
                             asio::use_awaitable);

  std::vector<uint8_t> response(2);
  stream.expires_after(get_timeout());
  co_await asio::async_read(stream, asio::buffer(response),
                            asio::use_awaitable);

  if (response[0] != 0x05) {
    throw std::runtime_error(common::I18nLogMessages::get_message(
        common::LogMessageKey::PROXY_SOCKS5_VERSION_MISMATCH));
  }

  if (response[1] == 0x02) {
    if (!proxy_config_.username || !proxy_config_.password) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::PROXY_AUTH_REQUIRED));
    }

    std::vector<uint8_t> auth_req;
    auth_req.push_back(0x01);
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

  std::vector<uint8_t> connect_req;
  connect_req.push_back(0x05);
  connect_req.push_back(0x01);
  connect_req.push_back(0x00);
  connect_req.push_back(0x03);
  connect_req.push_back(static_cast<uint8_t>(target_host_.length()));
  connect_req.insert(connect_req.end(), target_host_.begin(),
                     target_host_.end());
  connect_req.push_back(static_cast<uint8_t>(target_port_ >> 8));
  connect_req.push_back(static_cast<uint8_t>(target_port_ & 0xFF));

  stream.expires_after(get_timeout());
  co_await asio::async_write(stream, asio::buffer(connect_req),
                             asio::use_awaitable);

  std::vector<uint8_t> connect_resp(4);
  stream.expires_after(get_timeout());
  co_await asio::async_read(stream, asio::buffer(connect_resp),
                            asio::use_awaitable);

  if (connect_resp[0] != 0x05 || connect_resp[1] != 0x00) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_CONNECT_FAILED_EXCEPTION,
        std::to_string(connect_resp[1])));
  }

  size_t remaining_bytes = 0;
  if (connect_resp[3] == 0x01) {
    remaining_bytes = 6;
  } else if (connect_resp[3] == 0x03) {
    std::vector<uint8_t> domain_len(1);
    stream.expires_after(get_timeout());
    co_await asio::async_read(stream, asio::buffer(domain_len),
                              asio::use_awaitable);
    remaining_bytes = domain_len[0] + 2;
  } else if (connect_resp[3] == 0x04) {
    remaining_bytes = 18;
  }

  if (remaining_bytes > 0) {
    std::vector<uint8_t> addr_data(remaining_bytes);
    stream.expires_after(get_timeout());
    co_await asio::async_read(stream, asio::buffer(addr_data),
                              asio::use_awaitable);
  }

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host_,
                  target_port_);

  stream.expires_never();
}

// ============================================================
// 已弃用的同步版本隧道方法
// ============================================================

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

auto ProxyHttpClient::connect_through_proxy() -> tcp::socket {
  tcp::resolver resolver(ioc_);
  auto proxy_results =
      resolver.resolve(proxy_config_.host, std::to_string(proxy_config_.port));

  switch (proxy_config_.type) {
  case ProxyType::HTTP: {
    tcp::socket proxy_socket(ioc_);
    asio::connect(proxy_socket, proxy_results);
    return establish_http_tunnel(proxy_socket, target_host_, target_port_);
  }
  case ProxyType::HTTPS: {
    tcp::socket plain_socket(ioc_);
    asio::connect(plain_socket, proxy_results);

    ssl::context ssl_ctx{ssl::context::tls_client};
    ssl_ctx.set_default_verify_paths();
    ssl_ctx.set_verify_mode(ssl::verify_none);
    ssl_ctx.set_options(ssl::context::default_workarounds |
                        ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                        ssl::context::single_dh_use);

    ssl::stream<tcp::socket> ssl_socket{std::move(plain_socket), ssl_ctx};

    if (!SSL_set_tlsext_host_name(ssl_socket.native_handle(),
                                  proxy_config_.host.c_str())) {
      OBCX_I18N_WARN(common::LogMessageKey::PROXY_HTTPS_SNI_FAILED,
                     proxy_config_.host);
    }

    boost::system::error_code ec;
    ssl_socket.handshake(ssl::stream_base::client, ec);
    if (ec) {
      throw std::runtime_error(common::I18nLogMessages::format_message(
          common::LogMessageKey::PROXY_HTTPS_SSL_HANDSHAKE_FAILED_EXCEPTION,
          ec.message()));
    }

    OBCX_I18N_TRACE(common::LogMessageKey::PROXY_HTTPS_SSL_SUCCESS);
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
  std::string connect_target = target_host + ":" + std::to_string(target_port);
  std::ostringstream connect_request;
  connect_request << "CONNECT " << connect_target << " HTTP/1.1\r\n";
  connect_request << "Host: " << connect_target << "\r\n";
  connect_request << "User-Agent: OBCX/1.0\r\n";
  connect_request << "Proxy-Connection: keep-alive\r\n";

  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_request << "Proxy-Authorization: Basic "
                    << base64_encode(credentials) << "\r\n";
  }

  connect_request << "\r\n";

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
        response_line.pop_back();
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
      break;
    }

    OBCX_I18N_TRACE(common::LogMessageKey::PROXY_RESPONSE_HEADER, header_line);
  }

  return std::move(proxy_socket);
}

auto ProxyHttpClient::establish_https_tunnel(
    ssl::stream<tcp::socket> &ssl_socket, const std::string &target_host,
    uint16_t target_port) -> tcp::socket {
  std::string connect_target = target_host + ":" + std::to_string(target_port);
  http::request<http::string_body> connect_req{http::verb::connect,
                                               connect_target, 11};
  connect_req.set(http::field::host, connect_target);
  connect_req.set(http::field::user_agent, "OBCX/1.0");
  connect_req.set(http::field::proxy_connection, "keep-alive");
  connect_req.set(http::field::connection, "keep-alive");

  if (proxy_config_.username && proxy_config_.password) {
    std::string credentials =
        *proxy_config_.username + ":" + *proxy_config_.password;
    connect_req.set(http::field::proxy_authorization,
                    "Basic " + base64_encode(credentials));
  }

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_HTTPS_CONNECT_SEND,
                  connect_target);

  boost::system::error_code ec;
  http::write(ssl_socket, connect_req, ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_REQUEST_FAILED,
        ec.message()));
  }

  beast::flat_buffer buffer;
  http::response<http::string_body> connect_response;
  http::read(ssl_socket, buffer, connect_response, ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_RESPONSE_FAILED,
        ec.message()));
  }

  if (connect_response.result() != http::status::ok) {
    OBCX_I18N_ERROR(common::LogMessageKey::PROXY_HTTPS_CONNECT_ERROR,
                    static_cast<int>(connect_response.result()),
                    connect_response.reason(), connect_response.body());
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_HTTPS_CONNECT_STATUS_ERROR,
        static_cast<int>(connect_response.result())));
  }

  buffer.consume(buffer.size());

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_HTTPS_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host,
                  target_port);

  return std::move(ssl_socket.next_layer());
}

auto ProxyHttpClient::establish_socks5_tunnel(tcp::socket &proxy_socket,
                                              const std::string &target_host,
                                              uint16_t target_port)
    -> tcp::socket {
  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_START,
                  proxy_config_.host, target_host, target_port);

  boost::system::error_code ec;

  std::vector<uint8_t> greeting;
  greeting.push_back(0x05);

  if (proxy_config_.username && proxy_config_.password) {
    greeting.push_back(0x02);
    greeting.push_back(0x00);
    greeting.push_back(0x02);
  } else {
    greeting.push_back(0x01);
    greeting.push_back(0x00);
  }

  asio::write(proxy_socket, asio::buffer(greeting), ec);
  if (ec) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::PROXY_SOCKS5_HANDSHAKE_FAILED, ec.message()));
  }

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

  if (response[1] == 0x02) {
    if (!proxy_config_.username || !proxy_config_.password) {
      throw std::runtime_error(common::I18nLogMessages::get_message(
          common::LogMessageKey::PROXY_AUTH_REQUIRED));
    }

    std::vector<uint8_t> auth_req;
    auth_req.push_back(0x01);
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

  std::vector<uint8_t> connect_req;
  connect_req.push_back(0x05);
  connect_req.push_back(0x01);
  connect_req.push_back(0x00);
  connect_req.push_back(0x03);
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

  std::vector<uint8_t> connect_resp(10);
  asio::read(proxy_socket, asio::buffer(connect_resp, 4), ec);
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

  size_t remaining_bytes = 0;
  if (connect_resp[3] == 0x01) {
    remaining_bytes = 6;
  } else if (connect_resp[3] == 0x03) {
    asio::read(proxy_socket, asio::buffer(&connect_resp[4], 1), ec);
    remaining_bytes = connect_resp[4] + 2;
  } else if (connect_resp[3] == 0x04) {
    remaining_bytes = 18;
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

  OBCX_I18N_TRACE(common::LogMessageKey::PROXY_SOCKS5_TUNNEL_SUCCESS,
                  proxy_config_.host, proxy_config_.port, target_host,
                  target_port);

  return std::move(proxy_socket);
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace obcx::network
