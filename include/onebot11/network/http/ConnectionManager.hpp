#pragma once

#include "common/MessageType.hpp"
#include "network/HttpClient.hpp"
#include "network/IConnectionManager.hpp"
#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>

// 前向声明
namespace obcx::adapter {
class ProtocolAdapter;
}

namespace obcx::network {

/**
 * @brief HTTP连接管理器
 *
 * 实现通过HTTP轮询的方式与OneBot11实现通信。
 * 定期轮询获取事件，通过HTTP POST发送API请求。
 */
class HttpConnectionManager : public IConnectionManager {
public:
  HttpConnectionManager(asio::io_context &ioc,
                        adapter::onebot11::ProtocolAdapter &adapter);
  ~HttpConnectionManager() override = default;

  // 实现IConnectionManager接口
  void connect(const common::ConnectionConfig &config) override;
  void disconnect() override;
  bool is_connected() const override;
  asio::awaitable<std::string> send_action_and_wait_async(
      std::string action_payload, uint64_t echo_id) override;
  void set_event_callback(EventCallback callback) override;
  std::string get_connection_type() const override;

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
  asio::awaitable<void> poll_events();

  /**
   * @brief 处理轮询到的事件
   * @param events_json 事件JSON数组
   */
  void process_events(std::string_view events_json);

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