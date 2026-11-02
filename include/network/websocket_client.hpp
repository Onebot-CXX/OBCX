#pragma once

#include "network/detail/websocket_write_queue.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <memory>
#include <mutex>
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
  using MessageHandlerFactory = std::function<MessageHandler()>;
  using WriteEligibility = detail::WebsocketWriteQueue::WriteEligibility;

  explicit WebsocketClient(asio::io_context &ioc);

  // on_connection receives establishment/errors, not message payloads. After
  // establishment notification, before_read creates one nonempty handler
  // immediately before each async_read; that handler stays fixed across the
  // await. Capturing policy/authority belongs to the caller, not this client.
  auto run(std::string host, std::string port, std::string access_token,
           MessageHandler on_connection, MessageHandlerFactory before_read)
      -> asio::awaitable<void>;

  // Captures this shared-owned client and the current writer synchronously.
  // Absence of a writer is an error. Queue claim semantics
  // apply; acceptance is neither delivery nor prevention of later writes.
  auto send(std::string message, WriteEligibility eligible)
      -> asio::awaitable<void>;

  /**
   * @brief 异步关闭WebSocket连接。
   */
  auto close() -> asio::awaitable<void>;

  /**
   * @brief 获取执行器
   */
  auto get_executor() -> websocket::stream<beast::tcp_stream>::executor_type {
    return ws_.get_executor();
  }

private:
  /**
   * @brief 启动写入器协程
   */
  void start_writer();

  /**
   * @brief 停止写入器协程
   */
  void stop_writer();

  websocket::stream<beast::tcp_stream> ws_;
  std::string host_;
  std::string access_token_;
  MessageHandler on_message_;
  beast::flat_buffer buffer_;

  std::mutex write_queue_mutex_;
  std::shared_ptr<detail::WebsocketWriteQueue> write_queue_;
};

} // namespace obcx::network
