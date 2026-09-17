#ifndef OBCX_INCLUDE_ONEBOT11_NETWORK_HTTP_CONNECTION_MANAGER_HPP_
#define OBCX_INCLUDE_ONEBOT11_NETWORK_HTTP_CONNECTION_MANAGER_HPP_

#include "common/message_type.hpp"
#include "network/connection_config.hpp"
#include "network/http_client.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>
#include <memory>

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
  using EventCallback = std::function<void(const common::Event &)>;
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
  auto send_action_and_wait_async(std::string action_payload, uint64_t echo_id)
      -> asio::awaitable<std::string>;
  void set_event_callback(EventCallback callback);
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
  auto poll_events() -> asio::awaitable<void>;

  /**
   * @brief 处理轮询到的事件
   * @param events_json 事件JSON数组
   */
  void process_events(std::string_view events_json);
  void shutdown();

  asio::io_context &ioc_;
  adapter::onebot11::ProtocolAdapter &adapter_;
  EventCallback event_callback_;

  std::unique_ptr<HttpClient> http_client_;
  common::ConnectionConfig config_;

  // 轮询控制
  std::atomic<bool> is_polling_{false};
  std::atomic<bool> is_connected_{false};
  asio::steady_timer poll_timer_;

  // 轮询间隔（毫秒）
  std::chrono::milliseconds poll_interval_{1000};
};

} // namespace obcx::network

#endif // OBCX_INCLUDE_ONEBOT11_NETWORK_HTTP_CONNECTION_MANAGER_HPP_
