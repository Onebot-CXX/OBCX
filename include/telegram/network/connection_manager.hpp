#pragma once

#include "common/message_type.hpp"
#include "interfaces/connection_manager.hpp"
#include "network/http_client.hpp"
#include "network/proxy_http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>

namespace obcx::network {

/**
 * @brief Telegram Bot API连接管理器
 *
 * 实现通过HTTP轮询的方式与Telegram Bot API通信。
 * 定期轮询获取更新，通过HTTP POST发送API请求。
 */
class TelegramConnectionManager : public IConnectionManager {
public:
  TelegramConnectionManager(asio::io_context &ioc,
                            adapter::telegram::ProtocolAdapter &adapter);
  ~TelegramConnectionManager() override = default;

  // 实现IConnectionManager接口
  void connect(const common::ConnectionConfig &config) override;
  void disconnect() override;
  bool is_connected() const override;
  asio::awaitable<std::string> send_action_and_wait_async(
      std::string action_payload, uint64_t echo_id) override;
  void set_event_callback(EventCallback callback) override;
  std::string get_connection_type() const override;

  /**
   * @brief 下载Telegram文件
   * @param file_id 文件ID
   * @return 文件下载URL和文件信息
   */
  asio::awaitable<std::string> download_file(std::string file_id);

  /**
   * @brief 直接下载文件内容到内存
   * @param download_url 文件下载URL
   * @return 文件内容的二进制数据
   */
  asio::awaitable<std::string> download_file_content(
      std::string_view download_url);

private:
  /**
   * @brief 开始更新轮询
   */
  void start_polling();

  /**
   * @brief 停止更新轮询
   */
  void stop_polling();

  /**
   * @brief 轮询更新的协程
   */
  asio::awaitable<void> poll_updates();

  /**
   * @brief 处理轮询到的更新
   * @param updates_json 更新JSON数组
   */
  void process_updates(std::string_view updates_json);

  asio::io_context &ioc_;
  adapter::telegram::ProtocolAdapter &adapter_;
  EventCallback event_callback_;

  std::unique_ptr<HttpClient> http_client_;
  common::ConnectionConfig config_;

  // 轮询控制
  std::atomic<bool> is_polling_{false};
  std::atomic<bool> is_connected_{false};
  asio::steady_timer poll_timer_;

  // 轮询间隔（毫秒）
  std::chrono::milliseconds poll_interval_{1000};

  // 更新偏移量
  int update_offset_{0};
};

} // namespace obcx::network