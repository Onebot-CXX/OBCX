#pragma once

#include "common/Logger.hpp"
#include "common/MessageType.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <functional>
#include <future>
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
  unsigned int status_code;
  std::string body;
  http::response<http::string_body> raw_response;

  bool is_success() const { return status_code >= 200 && status_code < 300; }
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

  /**
   * @brief 异步发送POST请求
   * @param path 请求路径
   * @param body 请求体
   * @param headers 额外的请求头
   * @return 响应的future
   */
  std::future<HttpResponse> post_async(
      std::string_view path, std::string_view body,
      const std::map<std::string, std::string> &headers = {});

  /**
   * @brief 异步发送GET请求
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return 响应的future
   */
  std::future<HttpResponse> get_async(
      std::string_view path,
      const std::map<std::string, std::string> &headers = {});

  /**
   * @brief 异步发送HEAD请求
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return 响应的future
   */
  std::future<HttpResponse> head_async(
      std::string_view path,
      const std::map<std::string, std::string> &headers = {});

  /**
   * @brief 同步发送POST请求
   * @param path 请求路径
   * @param body 请求体
   * @param headers 额外的请求头
   * @return HTTP响应
   */
  virtual HttpResponse post_sync(
      std::string_view path, std::string_view body,
      const std::map<std::string, std::string> &headers = {});

  /**
   * @brief 同步发送GET请求
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return HTTP响应
   */
  virtual HttpResponse get_sync(
      std::string_view path,
      const std::map<std::string, std::string> &headers = {});

  /**
   * @brief 同步发送HEAD请求
   * @param path 请求路径
   * @param headers 额外的请求头
   * @return HTTP响应
   */
  virtual HttpResponse head_sync(
      std::string_view path,
      const std::map<std::string, std::string> &headers = {});

  /**
   * @brief 设置请求超时
   * @param timeout 超时时间
   */
  void set_timeout(std::chrono::milliseconds timeout);

  /**
   * @brief 检查连接是否可用
   * @return 是否连接正常
   */
  bool is_connected() const;

  /**
   * @brief 关闭连接
   */
  virtual void close();

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  /**
   * @brief 执行HTTP请求的内部实现
   */
  template <typename RequestType>
  std::future<HttpResponse> execute_async(RequestType &&request);

  /**
   * @brief 同步执行HTTP请求的内部实现
   */
  template <typename RequestType>
  HttpResponse execute_sync(RequestType &&request);

  /**
   * @brief 准备请求头
   */
  template <typename RequestType>
  void prepare_request(RequestType &request,
                       const std::map<std::string, std::string> &headers);
};

/**
 * @brief HTTP客户端工厂
 */
class HttpClientFactory {
public:
  /**
   * @brief 创建HTTP客户端实例
   * @param ioc IO上下文
   * @param config 连接配置
   * @return HTTP客户端实例
   */
  static std::unique_ptr<HttpClient> create(
      asio::io_context &ioc, const common::ConnectionConfig &config);
};

} // namespace obcx::network