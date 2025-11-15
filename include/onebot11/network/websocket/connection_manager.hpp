#pragma once

#include "common/message_type.hpp"
#include "interfaces/connection_manager.hpp"
#include "network/websocket_client.hpp"
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace obcx::adapter {
class ProtocolAdapter;
}

namespace obcx::network {

// 配置：是否使用协程异步等待（true）还是轮询等待（false）
inline constexpr bool USE_COROUTINE_ASYNC_WAIT = true;

/**
 * @brief WebSocket连接管理器
 *
 * 实现通过WebSocket与 OneBot v11 实现的持久连接。
 * 管理 WebsocketClient 的生命周期，并实现自动重连逻辑。
 */
class WebSocketConnectionManager : public IConnectionManager {
public:
  WebSocketConnectionManager(asio::io_context &ioc,
                             adapter::onebot11::ProtocolAdapter &adapter);

  void connect(const common::ConnectionConfig &config) override;
  void disconnect() override;
  auto is_connected() const -> bool override;
  auto send_action_and_wait_async(std::string action_payload, uint64_t echo_id)
      -> asio::awaitable<std::string> override;
  void set_event_callback(EventCallback callback) override;
  auto get_connection_type() const -> std::string override;

  /**
   * @brief 通过 WebSocket 启动连接过程。(兼容方法)
   * @param host 主机
   * @param port 端口
   * @param access_token 访问令牌
   */
  void connect_ws(std::string host, uint16_t port, std::string access_token);

  /**
   * @brief 获取发送操作的strand，用于确保发送操作的线程安全
   * @return 发送操作的strand引用
   */
  auto get_send_strand() -> auto & { return send_strand_; }

private:
  /**
   * @brief WebsocketClient 的消息处理回调。
   * @param ec 错误码。如果无错误，则为空。
   * @param message 消息内容。如果连接断开，则为空。
   */
  void on_ws_message(const beast::error_code &ec, const std::string &message);

  /**
   * @brief 启动一次新的连接尝试。
   */
  void do_connect();

  /**
   * @brief 安排一次重连。
   */
  void schedule_reconnect();

  /**
   * @brief 处理请求超时（协程模式）
   * @param echo_id 请求的echo ID
   */
  void handle_timeout(uint64_t echo_id);

  asio::io_context &ioc_;
  adapter::onebot11::ProtocolAdapter &adapter_;
  EventCallback event_callback_;

  std::shared_ptr<WebsocketClient> ws_client_;
  asio::steady_timer reconnect_timer_;

  // 用于串行化所有发送操作的strand
  asio::strand<asio::io_context::executor_type> send_strand_;

  std::string host_;
  uint16_t port_;
  std::string access_token_;
  bool is_running_ = false;

  // 用于存储等待响应的请求
  struct PendingRequest {
    // 协程模式：使用 completion handler
    std::function<void(boost::system::error_code, std::string)>
        completion_handler;
    // 轮询模式：使用 resolver/rejecter
    std::function<void(std::string)> resolver;
    std::function<void(std::exception_ptr)> rejecter;
    asio::steady_timer timeout_timer;
    std::atomic<bool> need_wait = true;

    PendingRequest(asio::io_context &ioc) : timeout_timer(ioc) {}
  };

  std::unordered_map<uint64_t, std::shared_ptr<PendingRequest>>
      pending_requests_;
  std::mutex pending_requests_mutex_;

  // 连接状态跟踪
  std::atomic_bool is_connected_ = false;
};

} // namespace obcx::network