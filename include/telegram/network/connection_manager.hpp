#pragma once

#include "common/message_type.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "network/connection_config.hpp"
#include "network/http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/provider_types.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace obcx::network {
namespace asio = boost::asio;
namespace detail {
class TelegramMediaReadAccess;
}

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

// Process-only originating request ownership; never an actor reference or a
// mutable latest-origin slot. Routing credentials belong to this client only.
struct TelegramReceiveOrigin {
  const std::uint64_t epoch;
  const std::shared_ptr<HttpClient> client;
  const core::SelfIdentityOrigin authority;
  const std::string access_token;
};

/**
 * @brief Telegram Bot API连接管理器
 *
 * 实现通过HTTP轮询的方式与Telegram Bot API通信。
 * 定期轮询获取更新，通过HTTP POST发送API请求。
 */
class TelegramConnectionManager
    : public std::enable_shared_from_this<TelegramConnectionManager> {
public:
  using EventCallback = std::function<core::SelfObservationResult(
      const common::Event &, const core::SelfIdentityOrigin &)>;
  TelegramConnectionManager(
      asio::io_context &ioc,
      std::shared_ptr<adapter::telegram::ProtocolAdapter> adapter);
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
  [[nodiscard]] std::uint64_t epoch() const { return epoch_.load(); }
  void configure_identity(std::shared_ptr<core::SelfIdentityCache> cache);
  auto capture_receive(const core::SelfIdentityOrigin &)
      -> std::optional<TelegramReceiveOrigin>;
  auto fetch_self(TelegramReceiveOrigin)
      -> asio::awaitable<std::optional<std::string>>;
  auto publish_self(const TelegramReceiveOrigin &, const obcx::bot::SelfInfo &)
      -> core::SelfObservationResult;
  auto send_action_and_wait_async(std::string action_payload, uint64_t echo_id)
      -> asio::awaitable<std::string>;
  auto send_domain_request(
      nlohmann::json parameters,
      const std::vector<std::pair<std::string, core::TelegramMediaUpload>>
          &files) -> asio::awaitable<std::string>;
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
  friend class detail::TelegramMediaReadAccess;
  struct Poll;
  // All helpers below run under event_mutex_, except the coroutine itself.
  bool receive_current(const TelegramReceiveOrigin &);
  void retire_account() noexcept;
  void start_polling();
  void launch_pending_poll();
  void stop_polling();
  void cancel_poll();
  void poll_finished(const std::shared_ptr<Poll> &, std::exception_ptr);
  auto poll_updates(std::shared_ptr<Poll>) -> asio::awaitable<void>;
  void process_updates(std::string_view, const std::shared_ptr<Poll> &);
  void shutdown();

  asio::io_context &ioc_;
  std::shared_ptr<adapter::telegram::ProtocolAdapter> adapter_;
  EventCallback event_callback_;
  std::recursive_mutex event_mutex_;

  std::shared_ptr<HttpClient> http_client_;
  std::shared_ptr<core::SelfIdentityCache> identity_;
  std::shared_ptr<Poll> active_poll_;
  std::shared_ptr<Poll> pending_poll_;
  common::ConnectionConfig config_;

  // 轮询控制
  std::atomic_uint64_t epoch_{0};
  std::atomic<bool> is_polling_{false};
  std::atomic<bool> is_connected_{false};
  asio::any_io_executor poll_executor_;

  // 更新偏移量
  std::int64_t update_offset_{0};
};

} // namespace obcx::network
