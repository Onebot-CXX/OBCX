#pragma once

#include "common/MessageType.hpp"
#include "network/HttpClient.hpp"
#include <boost/asio.hpp>
#include <optional>

namespace obcx::network {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// 代理协议类型
enum class ProxyType {
  HTTP,  // HTTP代理 (CONNECT方法)
  HTTPS, // HTTPS代理 (CONNECT方法，代理连接使用SSL)
  SOCKS5 // SOCKS5代理
};

// 代理配置
struct ProxyConfig {
  ProxyType type = ProxyType::HTTP;
  std::string host;
  uint16_t port = 0;
  std::optional<std::string> username;
  std::optional<std::string> password;

  bool is_enabled() const { return !host.empty() && port > 0; }
};

/**
 * @brief HTTP代理客户端
 *
 * 继承HttpClient，通过HTTP代理服务器发送请求
 */
class ProxyHttpClient : public HttpClient {
public:
  explicit ProxyHttpClient(asio::io_context &ioc,
                           const ProxyConfig &proxy_config,
                           const common::ConnectionConfig &config);
  ~ProxyHttpClient() override = default;

  // 重写父类的同步HTTP请求接口
  HttpResponse post_sync(
      std::string_view path, std::string_view body,
      const std::map<std::string, std::string> &headers = {}) override;

  HttpResponse get_sync(
      std::string_view path,
      const std::map<std::string, std::string> &headers = {}) override;

  void close() override;

private:
  asio::io_context &ioc_;
  ProxyConfig proxy_config_;
  tcp::resolver resolver_;
  std::string target_host_;
  uint16_t target_port_ = 443;

  // 建立代理隧道
  tcp::socket connect_through_proxy();

  // HTTP代理方法
  tcp::socket establish_http_tunnel(tcp::socket &proxy_socket,
                                    const std::string &target_host,
                                    uint16_t target_port);

  // HTTPS代理方法
  tcp::socket establish_https_tunnel(ssl::stream<tcp::socket> &ssl_socket,
                                     const std::string &target_host,
                                     uint16_t target_port);

  // SOCKS5代理方法
  tcp::socket establish_socks5_tunnel(tcp::socket &proxy_socket,
                                      const std::string &target_host,
                                      uint16_t target_port);

  // 通过隧道发送HTTP请求
  HttpResponse send_http_request(
      tcp::socket &tunnel_socket, const std::string &method,
      const std::string &path, const std::string &body,
      const std::map<std::string, std::string> &headers);
};

} // namespace obcx::network