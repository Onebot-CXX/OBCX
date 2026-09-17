#ifndef OBCX_INCLUDE_NETWORK_PROXY_HTTP_CLIENT_HPP_
#define OBCX_INCLUDE_NETWORK_PROXY_HTTP_CLIENT_HPP_

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
  explicit ProxyHttpClient(asio::any_io_executor executor,
                           ProxyConfig proxy_config,
                           const common::ConnectionConfig &config);
  ~ProxyHttpClient() override = default;

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
           const std::map<std::string, std::string> &headers = {},
           std::optional<std::uint64_t> response_body_limit = std::nullopt)
      -> asio::awaitable<HttpResponse> override;

  /**
   * @brief 异步发送HEAD请求（协程版本）
   * 通过代理隧道发送请求
   */
  auto head(std::string_view path,
            const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse> override;

  [[deprecated("Use post() awaitable instead")]]
  auto post_sync(std::string_view path, std::string_view body,
                 const std::map<std::string, std::string> &headers = {})
      -> HttpResponse override;

  [[deprecated("Use get() awaitable instead")]]
  auto get_sync(std::string_view path,
                const std::map<std::string, std::string> &headers = {})
      -> HttpResponse override;

  void close() override;
};

} // namespace obcx::network

#endif // OBCX_INCLUDE_NETWORK_PROXY_HTTP_CLIENT_HPP_
