#ifndef OBCX_INCLUDE_TELEGRAM_NETWORK_CONNECTION_MANAGER_HPP_
#define OBCX_INCLUDE_TELEGRAM_NETWORK_CONNECTION_MANAGER_HPP_

#include "common/message_type.hpp"
#include "network/connection_config.hpp"
#include "network/http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/provider_types.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>
#include <memory>
#include <optional>

namespace obcx::network {
namespace asio = boost::asio;

struct TelegramMultipartRequest {
  std::string body;
  std::string content_type;
};

[[nodiscard]] auto telegram_api_response_body(const HttpResponse &response)
    -> std::string;

[[nodiscard]] auto build_telegram_media_group_multipart(
    std::string_view chat_id,
    const std::vector<core::TelegramMediaUpload> &media,
    std::string_view caption,
    std::optional<int64_t> message_thread_id = std::nullopt,
    std::optional<std::string> reply_to_message_id = std::nullopt)
    -> TelegramMultipartRequest;

[[nodiscard]] auto build_telegram_media_group_multipart_with_entities(
    std::string_view chat_id,
    const std::vector<core::TelegramMediaUpload> &media,
    std::string_view caption, std::optional<int64_t> message_thread_id,
    std::optional<std::string> reply_to_message_id,
    const std::vector<core::TelegramTextEntity> &caption_entities)
    -> TelegramMultipartRequest;

/**
 * @brief Telegram Bot API连接管理器
 *
 * 实现通过HTTP轮询的方式与Telegram Bot API通信。
 * 定期轮询获取更新，通过HTTP POST发送API请求。
 */
class TelegramConnectionManager {
public:
  using EventCallback = std::function<void(const common::Event &)>;
  TelegramConnectionManager(asio::io_context &ioc,
                            adapter::telegram::ProtocolAdapter &adapter);
  TelegramConnectionManager(const TelegramConnectionManager &) = delete;
  auto operator=(const TelegramConnectionManager &)
      -> TelegramConnectionManager & = delete;

  TelegramConnectionManager(TelegramConnectionManager &&) = delete;
  auto operator=(TelegramConnectionManager &&)
      -> TelegramConnectionManager & = delete;
  ~TelegramConnectionManager();

  // Telegram HTTP transport operations.
  void connect(const common::ConnectionConfig &config);
  void disconnect();
  [[nodiscard]] auto is_connected() const -> bool;
  auto send_action_and_wait_async(std::string action_payload, uint64_t echo_id)
      -> asio::awaitable<std::string>;
  void set_event_callback(EventCallback callback);
  [[nodiscard]] auto get_connection_type() const -> std::string;

  /**
   * @brief 下载Telegram文件
   * @param file_id 文件ID
   * @return 文件下载URL和文件信息
   */
  auto download_file(std::string file_id) -> asio::awaitable<std::string>;

  /**
   * @brief 直接下载文件内容到内存
   * @param download_url 文件下载URL
   * @return 文件内容的二进制数据
   */
  auto download_file_content(std::string_view download_url,
                             std::size_t maximum_bytes)
      -> asio::awaitable<std::string>;

  /**
   * @brief 通过multipart/form-data上传图片到Telegram Bot API
   * @param chat_id 目标聊天ID
   * @param image_data 图片二进制数据
   * @param filename 文件名
   * @param mime_type MIME类型
   * @param caption 图片说明
   * @param message_thread_id 可选的话题ID
   * @return API响应JSON字符串
   */
  auto upload_photo_multipart(
      std::string_view chat_id, const std::string &image_data,
      std::string_view filename, std::string_view mime_type,
      std::string_view caption,
      std::optional<int64_t> message_thread_id = std::nullopt)
      -> asio::awaitable<std::string>;

  /** Upload a complete Telegram media group with attach:// multipart parts. */
  auto upload_media_group_multipart(
      std::string_view chat_id,
      const std::vector<core::TelegramMediaUpload> &media,
      std::string_view caption,
      std::optional<int64_t> message_thread_id = std::nullopt,
      std::optional<std::string> reply_to_message_id = std::nullopt)
      -> asio::awaitable<std::string>;

  auto upload_media_group_multipart_with_entities(
      std::string_view chat_id,
      const std::vector<core::TelegramMediaUpload> &media,
      std::string_view caption, std::optional<int64_t> message_thread_id,
      std::optional<std::string> reply_to_message_id,
      const std::vector<core::TelegramTextEntity> &caption_entities)
      -> asio::awaitable<std::string>;

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
  auto poll_updates() -> asio::awaitable<void>;

  /**
   * @brief 处理轮询到的更新
   * @param updates_json 更新JSON数组
   */
  void process_updates(std::string_view updates_json);
  void shutdown();

  asio::io_context &ioc_;
  adapter::telegram::ProtocolAdapter &adapter_;
  EventCallback event_callback_;

  std::unique_ptr<HttpClient> http_client_;
  common::ConnectionConfig config_;

  // 轮询控制
  std::atomic<bool> is_polling_{false};
  std::atomic<bool> is_connected_{false};
  asio::steady_timer poll_timer_;

  // 更新偏移量
  int update_offset_{0};
};

} // namespace obcx::network

#endif // OBCX_INCLUDE_TELEGRAM_NETWORK_CONNECTION_MANAGER_HPP_
