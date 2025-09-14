#include "network/ProxyHttpClient.hpp"
#include "common/Logger.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <openssl/ssl.h>
#include <sstream>
#include <thread>

namespace obcx::network {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

ProxyHttpClient::ProxyHttpClient(asio::io_context &ioc,
                                 const ProxyConfig &proxy_config,
                                 const common::ConnectionConfig &config)
    : HttpClient(ioc, config), ioc_(ioc), proxy_config_(proxy_config),
      resolver_(ioc), target_host_(config.host), target_port_(config.port) {
  OBCX_DEBUG("ProxyHttpClient 创建，代理: {}:{} -> 目标: {}:{}",
             proxy_config_.host, proxy_config_.port, target_host_,
             target_port_);
}

HttpResponse ProxyHttpClient::post_sync(
    std::string_view path, std::string_view body,
    const std::map<std::string, std::string> &headers) {

  try {
    // 建立代理隧道
    auto tunnel_socket = connect_through_proxy();

    // 通过隧道发送POST请求
    return send_http_request(tunnel_socket, "POST", std::string(path),
                             std::string(body), headers);
  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient POST请求失败: {}", e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    return error_response;
  }
}

HttpResponse ProxyHttpClient::get_sync(
    std::string_view path, const std::map<std::string, std::string> &headers) {

  try {
    // 建立代理隧道
    auto tunnel_socket = connect_through_proxy();

    // 通过隧道发送GET请求
    return send_http_request(tunnel_socket, "GET", std::string(path), "",
                             headers);
  } catch (const std::exception &e) {
    OBCX_ERROR("ProxyHttpClient GET请求失败: {}", e.what());
    HttpResponse error_response;
    error_response.status_code = 0;
    error_response.body = e.what();
    return error_response;
  }
}

void ProxyHttpClient::close() {
  // ProxyHttpClient的close实现
  // 代理客户端每次请求都是新的连接，所以这里不需要特殊处理
  HttpClient::close();
}

tcp::socket ProxyHttpClient::connect_through_proxy() {
  // 解析代理地址
  auto proxy_results =
      resolver_.resolve(proxy_config_.host, std::to_string(proxy_config_.port));

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
      OBCX_WARN("无法为HTTPS代理设置SNI: {}", proxy_config_.host);
    }

    // SSL握手
    boost::system::error_code ec;
    ssl_socket.handshake(ssl::stream_base::client, ec);
    if (ec) {
      throw std::runtime_error("HTTPS代理SSL握手失败: " + ec.message());
    }

    OBCX_DEBUG("HTTPS代理SSL连接建立成功");
    return establish_https_tunnel(ssl_socket, target_host_, target_port_);
  }
  case ProxyType::SOCKS5: {
    tcp::socket proxy_socket(ioc_);
    asio::connect(proxy_socket, proxy_results);
    return establish_socks5_tunnel(proxy_socket, target_host_, target_port_);
  }
  default:
    throw std::runtime_error("不支持的代理类型");
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
    // TODO: 实现正确的Base64编码
    connect_request << "Proxy-Authorization: Basic " << credentials << "\r\n";
  }

  connect_request << "\r\n"; // 结束头部

  std::string request_str = connect_request.str();

  boost::system::error_code ec;
  asio::write(proxy_socket, asio::buffer(request_str), ec);
  if (ec) {
    throw std::runtime_error("发送CONNECT请求失败: " + ec.message());
  }

  std::string response_line;
  char ch;
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
    throw std::runtime_error("读取CONNECT响应失败: " + ec.message());
  }

  if (response_line.find("200") == std::string::npos) {
    throw std::runtime_error("代理CONNECT请求失败: " + response_line);
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

    OBCX_DEBUG("响应头: {}", header_line);
  }

  return std::move(proxy_socket);
}

HttpResponse ProxyHttpClient::send_http_request(
    tcp::socket &tunnel_socket, const std::string &method,
    const std::string &path, const std::string &body,
    const std::map<std::string, std::string> &headers) {
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

      // 设置超时选项以避免连接挂起
      SSL_CTX_set_timeout(ssl_ctx.native_handle(), 30);

      ssl::stream<tcp::socket> ssl_stream{std::move(tunnel_socket), ssl_ctx};

      // 设置SNI（Server Name Indication）
      if (!SSL_set_tlsext_host_name(ssl_stream.native_handle(),
                                    target_host_.c_str())) {
        OBCX_WARN("无法设置SNI为: {}", target_host_);
      }

      // 给隧道更多时间稳定，特别是在通过代理时
      std::this_thread::sleep_for(std::chrono::milliseconds(300));

      // SSL握手，使用增强的错误处理和重试逻辑
      boost::system::error_code ec;
      int max_retries = 3;
      for (int retry = 0; retry < max_retries; ++retry) {
        ssl_stream.handshake(ssl::stream_base::client, ec);
        if (!ec) {
          OBCX_DEBUG("SSL握手成功 (重试第{}次)", retry);
          break;
        }

        OBCX_WARN("SSL握手失败 (重试第{}/{}次): {}", retry + 1, max_retries,
                  ec.message());

        if (retry < max_retries - 1) {
          // 指数退避重试策略
          auto wait_time = std::chrono::milliseconds(200 * (retry + 1));
          std::this_thread::sleep_for(wait_time);

          // 如果是stream truncated错误，可能需要重新创建连接
          if (ec.message().find("stream truncated") != std::string::npos) {
            OBCX_DEBUG("检测到stream truncated错误，可能需要重建隧道连接");
            // TODO: implement this
          }
        } else {
          throw std::runtime_error("SSL握手失败，已尝试" +
                                   std::to_string(max_retries) +
                                   "次: " + ec.message());
        }
      }

      // 发送请求，使用错误处理
      boost::system::error_code write_ec;
      http::write(ssl_stream, req, write_ec);
      if (write_ec) {
        throw std::runtime_error("SSL发送HTTP请求失败: " + write_ec.message());
      }

      // 读取响应，使用错误处理
      beast::flat_buffer buffer;
      http::response<http::string_body> res;
      boost::system::error_code read_ec;
      http::read(ssl_stream, buffer, res, read_ec);
      if (read_ec) {
        throw std::runtime_error("SSL读取HTTP响应失败: " + read_ec.message());
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
    throw std::runtime_error("HTTP请求发送失败: " + std::string(e.what()));
  }
}

tcp::socket ProxyHttpClient::establish_https_tunnel(
    ssl::stream<tcp::socket> &ssl_socket, const std::string &target_host,
    uint16_t target_port) {
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
    // TODO: 实现正确的Base64编码
    connect_req.set(http::field::proxy_authorization, "Basic " + credentials);
  }

  OBCX_DEBUG("通过HTTPS代理发送CONNECT请求: {}", connect_target);

  // 通过SSL连接发送CONNECT请求
  boost::system::error_code ec;
  http::write(ssl_socket, connect_req, ec);
  if (ec) {
    throw std::runtime_error("发送HTTPS CONNECT请求失败: " + ec.message());
  }

  // 读取CONNECT响应
  beast::flat_buffer buffer;
  http::response<http::string_body> connect_response;
  http::read(ssl_socket, buffer, connect_response, ec);
  if (ec) {
    throw std::runtime_error("读取HTTPS CONNECT响应失败: " + ec.message());
  }

  // 检查CONNECT响应
  if (connect_response.result() != http::status::ok) {
    OBCX_ERROR("HTTPS代理CONNECT响应: {}, 状态: {}, 内容: {}",
               static_cast<int>(connect_response.result()),
               connect_response.reason(), connect_response.body());
    throw std::runtime_error(
        "HTTPS代理CONNECT请求失败: " +
        std::to_string(static_cast<int>(connect_response.result())));
  }

  // 清空buffer中的任何额外数据
  buffer.consume(buffer.size());

  OBCX_DEBUG("HTTPS代理隧道建立成功: {}:{} -> {}:{}", proxy_config_.host,
             proxy_config_.port, target_host, target_port);

  // 返回底层socket，现在它已经通过SSL代理建立了到目标的隧道
  return std::move(ssl_socket.next_layer());
}

tcp::socket ProxyHttpClient::establish_socks5_tunnel(
    tcp::socket &proxy_socket, const std::string &target_host,
    uint16_t target_port) {
  OBCX_DEBUG("建立SOCKS5隧道: {} -> {}:{}", proxy_config_.host, target_host,
             target_port);

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
    throw std::runtime_error("SOCKS5握手失败: " + ec.message());
  }

  // 读取服务器响应
  std::vector<uint8_t> response(2);
  asio::read(proxy_socket, asio::buffer(response), ec);
  if (ec) {
    throw std::runtime_error("SOCKS5响应读取失败: " + ec.message());
  }

  if (response[0] != 0x05) {
    throw std::runtime_error("SOCKS5版本不匹配");
  }

  // 处理认证
  if (response[1] == 0x02) {
    // 用户名/密码认证
    if (!proxy_config_.username || !proxy_config_.password) {
      throw std::runtime_error("代理需要用户名/密码认证但未提供");
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
      throw std::runtime_error("SOCKS5认证请求失败: " + ec.message());
    }

    std::vector<uint8_t> auth_resp(2);
    asio::read(proxy_socket, asio::buffer(auth_resp), ec);
    if (ec) {
      throw std::runtime_error("SOCKS5认证响应读取失败: " + ec.message());
    }

    if (auth_resp[1] != 0x00) {
      throw std::runtime_error("SOCKS5认证失败");
    }
  } else if (response[1] != 0x00) {
    throw std::runtime_error("SOCKS5不支持的认证方法");
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
    throw std::runtime_error("SOCKS5连接请求失败: " + ec.message());
  }

  // 读取连接响应
  std::vector<uint8_t> connect_resp(10);                       // 最少10字节
  asio::read(proxy_socket, asio::buffer(connect_resp, 4), ec); // 先读前4字节
  if (ec) {
    throw std::runtime_error("SOCKS5连接响应读取失败: " + ec.message());
  }

  if (connect_resp[0] != 0x05 || connect_resp[1] != 0x00) {
    throw std::runtime_error("SOCKS5连接失败，错误码: " +
                             std::to_string(connect_resp[1]));
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
      throw std::runtime_error("SOCKS5地址数据读取失败: " + ec.message());
    }
  }

  OBCX_DEBUG("SOCKS5隧道建立成功: {}:{} -> {}:{}", proxy_config_.host,
             proxy_config_.port, target_host, target_port);

  return std::move(proxy_socket);
}

} // namespace obcx::network
