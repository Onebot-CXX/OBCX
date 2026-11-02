#pragma once

#include "common/message_type.hpp"
#include "network/connection_config.hpp"
#include "network/http_client.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/bot/detail/file_query.hpp"
#include "onebot11/bot/detail/standard_call_state.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include "onebot11/network/receive_origin.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>
#include <memory>
#include <mutex>

namespace obcx::onebot11::bot::standard::detail {
enum class NativeAction : std::uint8_t;
}

namespace obcx::network {
namespace asio = boost::asio;

/**
 * @brief HTTP连接管理器
 *
 * 实现通过HTTP轮询的方式与OneBot11实现通信。
 * 定期轮询获取事件，通过HTTP POST发送API请求。
 */
class HttpConnectionManager {
public:
  using EventCallback = onebot11::EventCallback;
  HttpConnectionManager(asio::io_context &ioc,
                        adapter::onebot11::ProtocolAdapter &adapter);
  ~HttpConnectionManager();

  HttpConnectionManager(const HttpConnectionManager &) = delete;
  auto operator=(const HttpConnectionManager &)
      -> HttpConnectionManager & = delete;
  HttpConnectionManager(HttpConnectionManager &&) = delete;
  auto operator=(HttpConnectionManager &&) -> HttpConnectionManager & = delete;

  // OneBot HTTP transport operations.
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
  // Process-only: parameters is an owned, typed-prepared native params object.
  // A call state belongs to exactly one invocation; no replay or rebasing.
  auto send_standard_action(
      obcx::onebot11::bot::standard::detail::NativeAction action,
      obcx::onebot11::bot::standard::ExecutionMode mode, std::string parameters,
      std::shared_ptr<obcx::onebot11::bot::standard::detail::StandardCallState>
          call,
      std::shared_ptr<core::SelfIdentityCache> identity,
      core::SelfIdentityOrigin origin) -> asio::awaitable<void>;
  auto send_file_query(
      obcx::onebot11::bot::detail::FileQueryKind kind, std::string parameters,
      std::shared_ptr<obcx::onebot11::bot::standard::detail::StandardCallState>
          call,
      std::shared_ptr<core::SelfIdentityCache> identity,
      core::SelfIdentityOrigin origin) -> asio::awaitable<void>;
  void set_origin_capture(onebot11::OriginCapture capture);
  void set_event_callback(EventCallback callback);
  void set_standard_events(
      std::shared_ptr<
          obcx::onebot11::bot::standard::StandardEventCapabilityV1>);
  void set_connection_callback(std::function<void(bool)> callback);
  [[nodiscard]] auto get_connection_type() const -> std::string;
  void set_poll_interval(std::chrono::milliseconds interval);

private:
  /**
   * @brief 开始事件轮询
   */
  void start_polling();

  /**
   * @brief 停止事件轮询
   */
  void stop_polling();

  /**
   * @brief 轮询事件的协程
   */
  auto poll_events(std::uint64_t epoch, std::shared_ptr<HttpClient> client,
                   std::optional<core::SelfIdentityOrigin> first_origin)
      -> asio::awaitable<void>;

  /**
   * @brief 处理轮询到的事件
   * @param events_json 事件JSON数组
   */
  void process_events(std::string_view events_json, std::uint64_t epoch,
                      const std::optional<core::SelfIdentityOrigin> &origin);
  void shutdown();

  asio::io_context &ioc_;
  adapter::onebot11::ProtocolAdapter &adapter_;
  EventCallback event_callback_;
  std::shared_ptr<obcx::onebot11::bot::standard::StandardEventCapabilityV1>
      standard_events_;
  onebot11::OriginCapture origin_capture_;
  std::function<void(bool)> connection_callback_;
  struct ConnectionState;
  struct StandardRequest;
  std::shared_ptr<ConnectionState> connection_;

  // 轮询控制
  std::atomic<bool> is_polling_{false};
  asio::steady_timer poll_timer_;

  // 轮询间隔（毫秒）
  std::chrono::milliseconds poll_interval_{1000};
};

} // namespace obcx::network
