#pragma once

#include "common/message_type.hpp"
#include "network/http_client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast/core.hpp>
#include <cstdint>
#include <optional>

namespace obcx::network {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

// 代理协议类型
enum class ProxyType : std::uint8_t {
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

  [[nodiscard]] auto is_enabled() const -> bool {
    return !host.empty() && port > 0;
  }
};

/**
 * @brief HTTP代理客户端
 *
 * 继承HttpClient，通过HTTP代理服务器发送请求
 * 使用C++20协程实现真正的异步操作
 */
class ProxyHttpClient : public HttpClient {
public:
  explicit ProxyHttpClient(asio::io_context &ioc, ProxyConfig proxy_config,
                           const common::ConnectionConfig &config);
  ~ProxyHttpClient() override = default;

  // ============================================================
  // 新的协程异步API（覆盖父类虚方法）
  // ============================================================

  /**
   * @brief 异步发送POST请求（协程版本）
   * 通过代理隧道发送请求
   */
  auto post(std::string_view path, std::string_view body,
            const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse> override;

  /**
   * @brief 异步发送GET请求（协程版本）
   * 通过代理隧道发送请求
   */
  auto get(std::string_view path,
           const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse> override;

  /**
   * @brief 异步发送HEAD请求（协程版本）
   * 通过代理隧道发送请求
   */
  auto head(std::string_view path,
            const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse> override;

  // ============================================================
  // 已弃用的同步API（覆盖父类虚方法，保留以便向后兼容）
  // ============================================================

  [[deprecated("Use post() awaitable instead")]]
  auto post_sync(std::string_view path, std::string_view body,
                 const std::map<std::string, std::string> &headers = {})
      -> HttpResponse override;

  [[deprecated("Use get() awaitable instead")]]
  auto get_sync(std::string_view path,
                const std::map<std::string, std::string> &headers = {})
      -> HttpResponse override;

  void close() override;

private:
  asio::io_context &ioc_;
  ProxyConfig proxy_config_;
  std::string target_host_;
  uint16_t target_port_ = 443;

  // ============================================================
  // 协程版本的代理隧道建立方法
  // ============================================================

  /**
   * @brief 异步建立代理隧道（协程版本）
   * @return 隧道TCP流
   */
  auto connect_through_proxy_async() -> asio::awaitable<beast::tcp_stream>;

  /**
   * @brief 异步建立HTTP代理隧道
   * @param stream TCP流
   */
  auto establish_http_tunnel_async(beast::tcp_stream &stream)
      -> asio::awaitable<void>;

  /**
   * @brief 异步建立HTTPS代理隧道
   * @param ssl_stream SSL流
   * @return 底层TCP流
   */
  auto establish_https_tunnel_async(
      beast::ssl_stream<beast::tcp_stream> &ssl_stream)
      -> asio::awaitable<beast::tcp_stream>;

  /**
   * @brief 异步建立SOCKS5代理隧道
   * @param stream TCP流
   */
  auto establish_socks5_tunnel_async(beast::tcp_stream &stream)
      -> asio::awaitable<void>;

  // ============================================================
  // 已弃用的同步版本方法（内部使用）
  // ============================================================

  // 建立代理隧道（同步版本）
  tcp::socket connect_through_proxy();

  // HTTP代理方法（同步版本）
  tcp::socket establish_http_tunnel(tcp::socket &proxy_socket,
                                    const std::string &target_host,
                                    uint16_t target_port);

  // HTTPS代理方法（同步版本）
  tcp::socket establish_https_tunnel(ssl::stream<tcp::socket> &ssl_socket,
                                     const std::string &target_host,
                                     uint16_t target_port);

  // SOCKS5代理方法（同步版本）
  tcp::socket establish_socks5_tunnel(tcp::socket &proxy_socket,
                                      const std::string &target_host,
                                      uint16_t target_port);

  // 通过隧道发送HTTP请求（同步版本）
  HttpResponse send_http_request(
      tcp::socket &tunnel_socket, const std::string &method,
      const std::string &path, const std::string &body,
      const std::map<std::string, std::string> &headers);
};

} // namespace obcx::network
