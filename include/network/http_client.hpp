#pragma once

#include "common/message_type.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <memory>

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

  [[nodiscard]] auto is_success() -> bool const {
    return status_code >= 200 && status_code < 300;
  }
};

/**
 * @brief HTTP客户端错误类型
 */
class HttpClientError : public std::runtime_error {
public:
  explicit HttpClientError(std::string_view message)
      : std::runtime_error(message.data()) {}
};

/**
 * @brief 异步HTTP客户端
 * 基于Boost.Beast，支持HTTP和HTTPS
 * 使用C++20协程实现真正的异步操作
 */
class HttpClient {
public:
  /**
   * @brief 构造函数
   * @param ioc IO上下文
   * @param config 连接配置
   */
  explicit HttpClient(asio::io_context &ioc,
                      const common::ConnectionConfig &config);

  /**
   * @brief 析构函数
   */
  virtual ~HttpClient();

  // ============================================================
  // 新的协程异步API（推荐使用）
  // ============================================================

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
                   const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse>;

  /**
   * @brief 异步发送HEAD请求（协程版本）
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return 响应的awaitable
   */
  virtual auto head(std::string_view path,
                    const std::map<std::string, std::string> &headers = {})
      -> asio::awaitable<HttpResponse>;

  // ============================================================
  // 已弃用的同步API（保留以便向后兼容）
  // ============================================================

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
   * @brief 检查连接是否可用
   * @return 是否连接正常
   */
  [[nodiscard]] auto is_connected() const -> bool;

  /**
   * @brief 关闭连接
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
  struct Impl;
  std::unique_ptr<Impl> pimpl_;
};

} // namespace obcx::network
