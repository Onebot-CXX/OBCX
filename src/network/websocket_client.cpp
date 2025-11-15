#include "network/websocket_client.hpp"
#include "common/logger.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/errc.hpp>

namespace obcx::network {

WebsocketClient::WebsocketClient(asio::io_context &ioc)
    : ws_(ioc), write_queue_timer_(ioc) {}

auto WebsocketClient::run(std::string host, std::string port,
                          std::string access_token, MessageHandler on_message)
    -> asio::awaitable<void> {
  /*
   * \if CHINESE
   * 保存成员变量
   * \endif
   * \if ENGLISH
   * Save member variables
   * \endif
   */
  host_ = std::move(host);
  access_token_ = std::move(access_token);
  on_message_ = std::move(on_message);
  /*
   * \if CHINESE
   * 复制一份，因为 run 可能重入
   * \endif
   * \if ENGLISH
   * Make a copy as run may be reentrant
   * \endif
   */
  auto port_str = port;

  try {
    /*
     * \if CHINESE
     * 1. 解析地址
     * \endif
     * \if ENGLISH
     * 1. Resolve address
     * \endif
     */
    tcp::resolver resolver(co_await asio::this_coro::executor);
    auto const results =
        co_await resolver.async_resolve(host_, port_str, asio::use_awaitable);

    /*
     * \if CHINESE
     * 2. 建立TCP连接
     * \endif
     * \if ENGLISH
     * 2. Establish TCP connection
     * \endif
     */
    auto &lowest_layer = beast::get_lowest_layer(ws_);
    lowest_layer.expires_after(std::chrono::seconds(30));
    co_await lowest_layer.async_connect(results, asio::use_awaitable);

    /*
     * \if CHINESE
     * 3. 执行WebSocket握手
     * \endif
     * \if ENGLISH
     * 3. Perform WebSocket handshake
     * \endif
     */
    lowest_layer.expires_never();
    ws_.set_option(
        websocket::stream_base::decorator([this](websocket::request_type &req) {
          if (!access_token_.empty()) {
            req.set(beast::http::field::authorization,
                    "Bearer " + access_token_);
          }
          req.set(beast::http::field::host, host_);
          req.set(beast::http::field::user_agent, "OBCX-Framework");
        }));
    co_await ws_.async_handshake(host_, "/", asio::use_awaitable);

    OBCX_INFO("WebSocket 已成功连接到 ws://{}:{}", host_, port_str);

    // 启动写入器协程
    start_writer();

    /*
     * \if CHINESE
     * 通过一个空的错误码来通知上层连接成功
     * \endif
     * \if ENGLISH
     * Notify upper layer of successful connection with an empty error code
     * \endif
     */
    on_message_({}, "");

    /*
     * \if CHINESE
     * 4. 循环读取消息
     * \endif
     * \if ENGLISH
     * 4. Loop to read messages
     * \endif
     */
    while (ws_.is_open()) {
      buffer_.clear();
      co_await ws_.async_read(buffer_, asio::use_awaitable);
      /*
       * \if CHINESE
       * 将收到的消息传递给上层处理器
       * \endif
       * \if ENGLISH
       * Pass received messages to upper layer handler
       * \endif
       */
      on_message_({}, beast::buffers_to_string(buffer_.data()));
    }
  } catch (const beast::system_error &se) {
    /*
     * \if CHINESE
     * 如果不是主动关闭连接，则记录错误
     * \endif
     * \if ENGLISH
     * Log error if not actively closing connection
     * \endif
     */
    if (se.code() != websocket::error::closed) {
      OBCX_ERROR("WebSocket 运行错误: {}", se.what());
    }
    /*
     * \if CHINESE
     * 将错误码传递给上层
     * \endif
     * \if ENGLISH
     * Pass error code to upper layer
     * \endif
     */
    on_message_(se.code(), "");
  } catch (const std::exception &e) {
    OBCX_CRITICAL("WebSocket 捕获到未处理异常: {}", e.what());
    beast::error_code ec = asio::error::fault;
    /*
     * \if CHINESE
     * 传递一个通用错误
     * \endif
     * \if ENGLISH
     * Pass a generic error
     * \endif
     */
    on_message_(ec, "");
  }

  // 停止写入器协程
  stop_writer();
  OBCX_WARN("WebSocket 连接已关闭.");
}

auto WebsocketClient::send(std::string message) -> asio::awaitable<void> {
  if (!ws_.is_open()) {
    OBCX_WARN("WebSocket 未连接，无法发送消息.");
    co_return;
  }

  // 创建写入请求
  auto request = std::make_shared<WriteRequest>(std::move(message));
  auto future = request->promise.get_future();

  // 将请求加入队列
  {
    std::lock_guard lock(write_queue_mutex_);
    write_queue_.push(request);
  }

  // 通知写入器有新消息
  write_queue_timer_.cancel();

  // 等待写入完成
  try {
    // 等待future完成
    while (future.wait_for(std::chrono::milliseconds(1)) ==
           std::future_status::timeout) {
      co_await asio::steady_timer(co_await asio::this_coro::executor,
                                  std::chrono::milliseconds(1))
          .async_wait(asio::use_awaitable);
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("等待写入完成时发生错误: {}", e.what());
    throw;
  }
}

auto WebsocketClient::close() -> asio::awaitable<void> {
  if (ws_.is_open()) {
    try {
      co_await ws_.async_close(websocket::close_code::normal,
                               asio::use_awaitable);
    } catch (const beast::system_error &se) {
      if (se.code() != websocket::error::closed) {
        OBCX_ERROR("WebSocket 关闭错误: {}", se.what());
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("WebSocket 关闭时捕獲到未處理異常: {}", e.what());
    }
  }
}

void WebsocketClient::start_writer() {
  if (writer_running_) {
    return;
  }

  writer_running_ = true;
  writer_error_ = nullptr;

  // 启动写入器协程
  asio::co_spawn(
      ws_.get_executor(),
      [this]() -> asio::awaitable<void> { co_await writer_coro(); },
      asio::detached);
}

auto WebsocketClient::writer_coro() -> asio::awaitable<void> {
  while (writer_running_ && ws_.is_open()) {
    std::shared_ptr<WriteRequest> request = nullptr;

    // 从队列中取出一个请求
    {
      std::lock_guard lock(write_queue_mutex_);
      if (!write_queue_.empty()) {
        request = write_queue_.front();
        write_queue_.pop();
      }
    }

    if (request) {
      try {
        // 执行实际的写入操作
        co_await ws_.async_write(asio::buffer(request->message),
                                 asio::use_awaitable);

        // 通知写入完成
        request->promise.set_value();

        OBCX_DEBUG("消息已成功发送: {}", request->message);
      } catch (const std::exception &e) {
        OBCX_ERROR("写入消息时发生错误: {}", e.what());

        // 通知写入失败
        try {
          request->promise.set_exception(std::current_exception());
        } catch (...) {
          // 如果设置异常失败，忽略
        }

        // 记录错误
        writer_error_ = std::current_exception();
      }
    } else {
      // 队列为空，等待一段时间
      co_await asio::steady_timer(co_await asio::this_coro::executor,
                                  std::chrono::milliseconds(10))
          .async_wait(asio::use_awaitable);
    }
  }
}

void WebsocketClient::stop_writer() {
  writer_running_ = false;

  // 清空队列中的所有请求
  std::lock_guard lock(write_queue_mutex_);
  while (!write_queue_.empty()) {
    auto request = write_queue_.front();
    write_queue_.pop();

    // 通知所有未完成的请求
    try {
      if (writer_error_) {
        request->promise.set_exception(writer_error_);
      } else {
        request->promise.set_exception(
            std::make_exception_ptr(std::runtime_error("WebSocket连接已关闭")));
      }
    } catch (...) {
      // 忽略设置异常时的错误
    }
  }
}

} // namespace obcx::network