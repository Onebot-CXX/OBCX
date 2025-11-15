#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace obcx::network {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

/**
 * @brief 现代化WebSocket客户端
 *
 * 完全基于C++20协程(asio::awaitable)进行设计，负责底层的网络通信。
 * 实现了写入队列机制，确保线程安全和并发写入的正确性。
 */
class WebsocketClient : public std::enable_shared_from_this<WebsocketClient> {
public:
  // 定义一个回调函数类型，用于向上层传递收到的消息
  using MessageHandler =
      std::function<void(const beast::error_code &, const std::string &)>;

  explicit WebsocketClient(asio::io_context &ioc);

  /**
   * @brief 连接到服务器并开始循环读取消息。
   * 这是一个协程，它会一直运行直到连接关闭或发生错误。
   * @param host 服务器主机名或IP地址。
   * @param port 服务器端口。
   * @param access_token OneBot v11 的 access-token。
   * @param on_message 收到消息时的回调函数。
   */
  asio::awaitable<void> run(std::string host, std::string port,
                            std::string access_token,
                            MessageHandler on_message);

  /**
   * @brief 异步发送一条消息。
   * @param message 要发送的文本消息。
   * @return 返回一个awaitable，当消息真正发送完成时恢复执行
   */
  asio::awaitable<void> send(std::string message);

  /**
   * @brief 异步关闭WebSocket连接。
   */
  asio::awaitable<void> close();

  /**
   * @brief 获取执行器
   */
  auto get_executor() -> websocket::stream<beast::tcp_stream>::executor_type {
    return ws_.get_executor();
  }

private:
  /**
   * @brief 写入队列中的条目
   */
  struct WriteRequest {
    std::string message;
    std::promise<void> promise;

    WriteRequest(std::string msg) : message(std::move(msg)) {}
  };

  /**
   * @brief 启动写入器协程
   */
  void start_writer();

  /**
   * @brief 写入器协程 - 负责从队列中取出消息并逐一发送
   */
  asio::awaitable<void> writer_coro();

  /**
   * @brief 停止写入器协程
   */
  void stop_writer();

  websocket::stream<beast::tcp_stream> ws_;
  std::string host_;
  std::string access_token_;
  MessageHandler on_message_;
  beast::flat_buffer buffer_;

  // 写入队列相关
  std::queue<std::shared_ptr<WriteRequest>> write_queue_;
  std::mutex write_queue_mutex_;
  asio::steady_timer write_queue_timer_;
  bool writer_running_ = false;
  std::exception_ptr writer_error_;
};

} // namespace obcx::network