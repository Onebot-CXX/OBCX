#pragma once

#include "common/message_type.hpp"
#include "network/connection_config.hpp"
#include "network/websocket_client.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/bot/detail/file_query.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include "onebot11/network/receive_origin.hpp"
#include "onebot11/network/websocket/detail/action_request_tracker.hpp"

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace obcx::onebot11::bot::standard::detail {
enum class NativeAction : std::uint8_t;
}

namespace obcx::network {
namespace asio = boost::asio;

/**
 * @brief WebSocket连接管理器
 *
 * 实现通过WebSocket与 OneBot v11 实现的持久连接。
 * 管理 WebsocketClient 的生命周期，并实现自动重连逻辑。
 */
class WebSocketConnectionManager {
public:
  using EventCallback = onebot11::EventCallback;
  WebSocketConnectionManager(
      asio::io_context &ioc, adapter::onebot11::ProtocolAdapter &adapter,
      detail::ActionDeadlineFactory deadline_factory = {});
  ~WebSocketConnectionManager();

  WebSocketConnectionManager(const WebSocketConnectionManager &) = delete;
  auto operator=(const WebSocketConnectionManager &)
      -> WebSocketConnectionManager & = delete;
  WebSocketConnectionManager(WebSocketConnectionManager &&) = delete;
  auto operator=(WebSocketConnectionManager &&)
      -> WebSocketConnectionManager & = delete;

  void connect(const common::ConnectionConfig &config);
  void disconnect();
  [[nodiscard]] auto is_connected() const -> bool;
  auto send_action_and_wait_async(
      std::string action_payload, uint64_t echo_id,
      std::optional<std::uint64_t> expected_epoch = std::nullopt)
      -> asio::awaitable<std::string>;
  auto send_profile_action(std::string payload, std::uint64_t echo,
                           core::SelfIdentityOrigin origin,
                           onebot11::OriginValidation validate,
                           onebot11::ProfilePublication publish)
      -> asio::awaitable<core::SelfIdentityCache::Result>;
  void set_origin_capture(onebot11::OriginCapture capture);
  void set_event_callback(EventCallback callback);
  void set_standard_events(
      std::shared_ptr<
          obcx::onebot11::bot::standard::StandardEventCapabilityV1>);
  void set_connection_callback(std::function<void(bool)> callback);
  [[nodiscard]] auto get_connection_type() const -> std::string;

  /**
   * @brief 获取发送操作的strand，用于确保发送操作的线程安全
   * @return 发送操作的strand引用
   */
  auto get_send_strand() -> asio::strand<asio::io_context::executor_type> &;

  // Owning synchronous wrapper. Caller cancellation signals must be emitted
  // on the caller's serialized Asio domain; internal children use send_strand.
  auto send_standard_action(
      obcx::onebot11::bot::standard::detail::NativeAction action,
      obcx::onebot11::bot::standard::ExecutionMode mode, std::string parameters,
      std::uint64_t echo,
      std::shared_ptr<obcx::onebot11::bot::standard::detail::StandardCallState>
          call,
      std::shared_ptr<core::SelfIdentityCache> identity,
      core::SelfIdentityOrigin origin) -> asio::awaitable<void>;

  auto send_file_query(
      obcx::onebot11::bot::detail::FileQueryKind kind, std::string parameters,
      std::uint64_t echo,
      std::shared_ptr<obcx::onebot11::bot::standard::detail::StandardCallState>
          call,
      std::shared_ptr<core::SelfIdentityCache> identity,
      core::SelfIdentityOrigin origin) -> asio::awaitable<void>;

private:
  struct ConnectionState;
  struct StandardRequest;
  std::shared_ptr<ConnectionState> connection_;
};

} // namespace obcx::network
