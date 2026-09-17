#ifndef OBCX_INCLUDE_NETWORK_HTTP_CLIENT_HPP_
#define OBCX_INCLUDE_NETWORK_HTTP_CLIENT_HPP_

#include "common/message_type.hpp"
#include "network/connection_config.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <cstdint>
#include <memory>
#include <optional>

namespace obcx::network {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;

/**
 * @brief HTTP响应结果
 */
struct HttpResponse {
  unsigned int status_code{0};
  std::string body;
  http::response<http::string_body> raw_response;

  [[nodiscard]] auto is_success() const -> bool {
    return status_code >= 200 && status_code < 300;
  }
};

/** Whether an HTTP request could have reached the target application. */
enum class HttpRequestSubmissionState : std::uint8_t {
  DefinitelyNotSubmitted,
  PossiblySubmitted,
};

/**
 * @brief HTTP客户端错误类型
 */
class HttpClientError : public std::runtime_error {
public:
  explicit HttpClientError(std::string_view message,
                           HttpRequestSubmissionState submission_state =
                               HttpRequestSubmissionState::PossiblySubmitted)
      : std::runtime_error(std::string{message}),
        submission_state_{submission_state} {}

  [[nodiscard]] auto submission_state() const noexcept
      -> HttpRequestSubmissionState {
    return submission_state_;
  }

private:
  HttpRequestSubmissionState submission_state_;
};

/**
 * @brief 异步HTTP客户端
 * 基于Boost.Beast，支持HTTP和HTTPS
 * 使用C++20协程实现真正的异步操作
 */
class HttpClient {
public:
  static constexpr std::uint64_t kDefaultResponseBodyLimit =
      8ULL * 1024ULL * 1024ULL;

  /**
   * @brief 构造函数
   * @param ioc IO上下文
   * @param config 连接配置
   */
  explicit HttpClient(asio::io_context &ioc,
                      const common::ConnectionConfig &config);

  // Transport resources use this owning executor; completions resume on the
  // awaiting coroutine's executor. Keep the owning context alive and running
  // through close/drain and destroy the client before destroying that context.
  explicit HttpClient(asio::any_io_executor executor,
                      const common::ConnectionConfig &config);

  /**
   * @brief 析构函数
   */
  virtual ~HttpClient();
  HttpClient(const HttpClient &) = delete;
  auto operator=(const HttpClient &) -> HttpClient & = delete;

  /**
   * @brief 异步发送POST请求（协程版本）
   * @param path 请求路径
   * @param body 请求体
   * @param headers 额外的请求头
   * @return 响应的awaitable
   */
  virtual auto post(std::string_view path, std::string_view body,
                    const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse>;

  /**
   * @brief 异步发送GET请求（协程版本）
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return 响应的awaitable
   */
  virtual auto get(std::string_view path,
                   const std::map<std::string, std::string> &headers = {},
                   std::optional<std::uint64_t> response_body_limit =
                       std::nullopt) -> asio::awaitable<HttpResponse>;

  /**
   * @brief 异步发送HEAD请求（协程版本）
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return 响应的awaitable
   */
  virtual auto head(std::string_view path,
                    const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse>;

  /**
   * @brief 同步发送POST请求
   * @deprecated 使用 post() awaitable 版本代替
   */
  [[deprecated("Use post() awaitable instead")]]
  virtual auto post_sync(std::string_view path, std::string_view body,
                         const std::map<std::string, std::string> &headers = {})
      -> HttpResponse;

  /**
   * @brief 同步发送GET请求
   * @deprecated 使用 get() awaitable 版本代替
   */
  [[deprecated("Use get() awaitable instead")]]
  virtual auto get_sync(std::string_view path,
                        const std::map<std::string, std::string> &headers = {})
      -> HttpResponse;

  /**
   * @brief 同步发送HEAD请求
   * @deprecated 使用 head() awaitable 版本代替
   */
  [[deprecated("Use head() awaitable instead")]]
  virtual auto head_sync(std::string_view path,
                         const std::map<std::string, std::string> &headers = {})
      -> HttpResponse;

  /**
   * @brief 设置请求超时
   * @param timeout 超时时间
   */
  void set_timeout(std::chrono::milliseconds timeout);

  /**
   * @brief Set the maximum response body accepted by this client.
   * @param bytes Positive finite limit in bytes
   */
  void set_response_body_limit(std::uint64_t bytes);

  /** @return Maximum response body accepted by this client, in bytes. */
  [[nodiscard]] auto response_body_limit() const -> std::uint64_t;

  /**
   * @brief 检查连接是否可用
   * @return 是否连接正常
   */
  [[nodiscard]] auto is_connected() const -> bool;

  /**
   * @brief Close admission and request cancellation (idempotent, terminal).
   * Keep the owning executor running until queued cleanup/completions drain.
   */
  virtual void close();

protected:
  /**
   * @brief 获取配置的超时时间
   * @return 超时时间
   */
  [[nodiscard]] auto get_timeout() const -> std::chrono::milliseconds;

  /**
   * @brief 获取目标主机名
   * @return 主机名
   */
  [[nodiscard]] auto get_host() const -> const std::string &;

  /**
   * @brief 获取目标端口
   * @return 端口号
   */
  [[nodiscard]] auto get_port() const -> uint16_t;

  /**
   * @brief 检查是否使用SSL
   * @return 是否使用SSL
   */
  [[nodiscard]] auto use_ssl() const -> bool;

  /**
   * @brief 获取SSL上下文
   * @return SSL上下文的可选引用
   */
  [[nodiscard]] auto get_ssl_context() const -> ssl::context *;

  /**
   * @brief 准备请求头
   */
  template <typename RequestType>
  void prepare_request(RequestType &request,
                       const std::map<std::string, std::string> &headers);

private:
  friend class ProxyHttpClient;

  struct Impl;
  std::unique_ptr<Impl> pimpl_;
};

} // namespace obcx::network

#endif // OBCX_INCLUDE_NETWORK_HTTP_CLIENT_HPP_
