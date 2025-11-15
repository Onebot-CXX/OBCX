#include "core/tg_bot.hpp"

#include "common/logger.hpp"
#include "interfaces/connection_manager.hpp"
#include "network/http_client.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/network/connection_manager.hpp"

#include "common/media_converter.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace obcx::core {

TGBot::TGBot(adapter::telegram::ProtocolAdapter adapter)
    : IBot{std::make_unique<adapter::telegram::ProtocolAdapter>(
          std::move(adapter))} {
  OBCX_INFO("TelegramBot 实例已创建，所有核心组件已初始化。");
}

TGBot::~TGBot() { OBCX_INFO("TelegramBot 实例已销毁。"); }

void TGBot::connect(network::ConnectionManagerFactory::ConnectionType type,
                    const common::ConnectionConfig &config) {
  conection_config_ = config;
  if (type == network::ConnectionManagerFactory::ConnectionType::TelegramHTTP) {
    connection_manager_ = network::ConnectionManagerFactory::create(
        type, *io_context_, *adapter_);
  } else {
    throw std::runtime_error("Telegram Bot only support TelegramHTTP");
  }

  connection_manager_->set_event_callback([this](const common::Event &event) {
    dispatcher_->dispatch(this, event);
  });

  connection_manager_->connect(config);

  OBCX_INFO("使用{}连接类型连接到 {}:{}",
            connection_manager_->get_connection_type(), config.host,
            config.port);
}

void TGBot::connect_ws(std::string_view host, uint16_t port,
                       std::string_view access_token) {
  common::ConnectionConfig config;
  config.host = host;
  config.port = port;
  config.access_token = access_token;

  connect(network::ConnectionManagerFactory::ConnectionType::Onebot11WebSocket,
          config);
}

void TGBot::connect_http(std::string_view host, uint16_t port,
                         std::string_view access_token) {
  common::ConnectionConfig config;
  config.host = host;
  config.port = port;
  config.access_token = access_token;

  connect(network::ConnectionManagerFactory::ConnectionType::Onebot11HTTP,
          config);
}

void TGBot::run() {
  if (io_context_->stopped()) {
    io_context_->restart();
  }

  // Start long polling for Telegram updates
  asio::co_spawn(
      *io_context_,
      [this]() -> asio::awaitable<void> { co_await this->poll_updates(); },
      asio::detached);

  OBCX_INFO("TelegramBot 开始运行事件循环...");
  io_context_->run();
  OBCX_INFO("TelegramBot 事件循环已结束。");
}

void TGBot::stop() {
  OBCX_INFO("正在请求停止 TelegramBot...");

  // 首先断开连接
  if (connection_manager_) {
    connection_manager_->disconnect();
  }

  if (task_scheduler_) {
    task_scheduler_->stop();
  }

  io_context_->stop();
}

asio::awaitable<void> TGBot::poll_updates() {
  int offset = 0;
  while (is_connected()) {
    bool success = false;
    try {
      // Get updates from Telegram
      auto updates = co_await get_updates(offset, 100);

      // Parse and dispatch events
      // In a real implementation, we would parse the updates and dispatch
      // events For now, we'll just log that we received updates
      OBCX_DEBUG("Received {} updates from Telegram", updates.length());

      // Update offset for next poll
      // In a real implementation, we would parse the actual update IDs
      offset += 1;

      success = true;
    } catch (const std::exception &e) {
      OBCX_ERROR("Error polling updates: {}", e.what());
    }

    // Wait before next poll or retry
    asio::steady_timer timer(*io_context_,
                             std::chrono::seconds(success ? 1 : 5));
    co_await timer.async_wait(asio::use_awaitable);
  }
}

void TGBot::error_notify(std::string_view target_id, std::string_view message,
                         bool is_group) {
  common::ErrorEvent error_event{.error_type = "message_error",
                                 .error_message = std::string(message),
                                 .target_id = std::string(target_id),
                                 .is_group = is_group,
                                 .time = std::chrono::system_clock::now(),
                                 .context = {{"source", "bot_error_handler"}}};

  if (dispatcher_) {
    dispatcher_->dispatch(this, error_event);
  } else {
    OBCX_WARN("事件分发器未初始化，无法分发异常事件");
  }
}

auto TGBot::send_private_message(std::string_view user_id,
                                 const common::Message &message)
    -> asio::awaitable<std::string> {
  ensure_connection_manager();
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_send_message_request(
      user_id, message, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_group_message(std::string_view group_id,
                               const common::Message &message)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_send_message_request(
      group_id, message, echo_id);

  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_topic_message(std::string_view group_id, int64_t topic_id,
                               const common::Message &message)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_send_topic_message_request(
      group_id, message, echo_id, topic_id);

  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::send_group_photo(std::string_view group_id,
                             std::string_view photo_data,
                             std::string_view caption)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();

  // 构造sendPhoto请求的JSON
  nlohmann::json request;
  request["method"] = "sendPhoto";
  request["chat_id"] = group_id;
  request["photo"] = photo_data;
  if (!caption.empty()) {
    request["caption"] = caption;
  }
  request["echo"] = echo_id;

  std::string payload = request.dump();
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 消息管理 API ---

auto TGBot::delete_message(std::string_view message_id)
    -> asio::awaitable<std::string> {
  // In Telegram, we need both chat_id and message_id
  // For simplicity, we'll assume message_id contains both in format
  // "chat_id:message_id"
  std::string msg_id(message_id);
  size_t pos = msg_id.find(':');
  if (pos == std::string::npos) {
    throw std::invalid_argument("Invalid message ID format for Telegram");
  }

  std::string chat_id = msg_id.substr(0, pos);
  std::string actual_message_id = msg_id.substr(pos + 1);

  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_delete_message_request(
      chat_id, actual_message_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_message(std::string_view message_id)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have a direct get message API
  // We would need to store messages locally or use other mechanisms
  OBCX_WARN("TelegramBot::get_message 尚未实现完整的功能");
  co_return "{}";
}

// --- 好友管理 API ---

auto TGBot::get_friend_list() -> asio::awaitable<std::string> {
  // Telegram doesn't have a direct friend list API
  // We could return the list of users we've interacted with
  OBCX_WARN("TelegramBot::get_friend_list 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::get_stranger_info(std::string_view user_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  // For Telegram, we need both chat_id and user_id, but for this API we only
  // have user_id We'll use a dummy chat_id, but in a real implementation you
  // might need to track the chat context
  auto payload = get_telegram_adapter().serialize_get_user_info_request(
      "", user_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 群组管理 API ---

auto TGBot::get_group_list() -> asio::awaitable<std::string> {
  // Telegram doesn't have a direct group list API
  // We could return the list of chats we're in
  OBCX_WARN("TelegramBot::get_group_list 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::get_group_info(std::string_view group_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_chat_info_request(
      group_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_member_list(std::string_view group_id)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_chat_admins_request(
      group_id, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_member_info(std::string_view group_id,
                                  std::string_view user_id, bool no_cache)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_chat_member_info_request(
      group_id, user_id, no_cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_kick(std::string_view group_id, std::string_view user_id,
                           bool reject_add_request)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_kick_chat_member_request(
      group_id, user_id, reject_add_request, false, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_ban(std::string_view group_id, std::string_view user_id,
                          int32_t duration) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_ban_chat_member_request(
      group_id, user_id, duration, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_whole_ban(std::string_view group_id, bool enable)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_ban_all_members_request(
      group_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_card(std::string_view group_id, std::string_view user_id,
                           std::string_view card)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have a direct group card API
  // This would need to be implemented differently
  OBCX_WARN("TelegramBot::set_group_card 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::set_group_leave(std::string_view group_id, bool is_dismiss)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_leave_chat_request(
      group_id, is_dismiss, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_name(std::string_view group_id,
                           std::string_view group_name)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_set_chat_title_request(
      group_id, group_name, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_admin(std::string_view group_id, std::string_view user_id,
                            bool enable) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_set_chat_admin_request(
      group_id, user_id, enable, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::set_group_anonymous_ban(std::string_view group_id,
                                    const std::string &anonymous,
                                    int32_t duration)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have anonymous ban functionality in the same way
  OBCX_WARN("TelegramBot::set_group_anonymous_ban 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::set_group_anonymous(std::string_view group_id, bool enable)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have anonymous chat functionality in the same way
  OBCX_WARN("TelegramBot::set_group_anonymous 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::set_group_portrait(std::string_view group_id, std::string_view file,
                               bool cache) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_set_chat_photo_request(
      group_id, file, cache, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_group_honor_info(std::string_view group_id,
                                 std::string_view type)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have honor info functionality
  OBCX_WARN("TelegramBot::get_group_honor_info 尚未实现完整的功能");
  co_return "{}";
}

// --- 状态获取 API ---

auto TGBot::get_login_info() -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_get_self_info_request(echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_status() -> asio::awaitable<std::string> {
  // Telegram doesn't have a direct status API
  OBCX_WARN("TelegramBot::get_status 尚未实现完整的功能");
  co_return R"({"retcode": 0, "status": "ok", "data": {"online": true}})";
}

auto TGBot::get_version_info() -> asio::awaitable<std::string> {
  // Return version info for Telegram bot
  OBCX_WARN("TelegramBot::get_version_info 尚未实现完整的功能");
  co_return R"({"retcode": 0, "data": {"version": "TelegramBot/1.0.0"}})";
}

// --- 资源管理 API ---

auto TGBot::get_image(std::string_view file) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_download_file_request(file, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_record(std::string_view file, std::string_view out_format)
    -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload =
      get_telegram_adapter().serialize_download_file_request(file, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

// --- 能力检查 API ---

auto TGBot::can_send_image() -> asio::awaitable<std::string> {
  // Telegram bots can send images
  co_return R"({"retcode": 0, "data": {"yes": true}})";
}

auto TGBot::can_send_record() -> asio::awaitable<std::string> {
  // Telegram bots can send voice messages
  co_return R"({"retcode": 0, "data": {"yes": true}})";
}

// --- Telegram相关接口凭证 API ---

auto TGBot::get_cookies(std::string_view domain)
    -> asio::awaitable<std::string> {
  // Telegram bots don't use cookies in the same way
  OBCX_WARN("TelegramBot::get_cookies 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::get_csrf_token() -> asio::awaitable<std::string> {
  // Telegram bots don't use CSRF tokens
  OBCX_WARN("TelegramBot::get_csrf_token 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::get_credentials(std::string_view domain)
    -> asio::awaitable<std::string> {
  // Telegram bots use bot tokens instead
  OBCX_WARN("TelegramBot::get_credentials 尚未实现完整的功能");
  co_return "{}";
}

// --- 请求处理 API ---

auto TGBot::set_friend_add_request(std::string_view flag, bool approve,
                                   std::string_view remark)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have friend requests in the same way
  OBCX_WARN("TelegramBot::set_friend_add_request 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::set_group_add_request(std::string_view flag,
                                  std::string_view sub_type, bool approve,
                                  std::string_view reason)
    -> asio::awaitable<std::string> {
  // Telegram doesn't have group add requests in the same way
  OBCX_WARN("TelegramBot::set_group_add_request 尚未实现完整的功能");
  co_return "{}";
}

auto TGBot::is_connected() const -> bool {
  return connection_manager_ && connection_manager_->is_connected();
}

auto TGBot::get_updates(int offset, int limit) -> asio::awaitable<std::string> {
  auto echo_id = generate_echo_id();
  auto payload = get_telegram_adapter().serialize_get_updates_request(
      offset, limit, echo_id);
  co_return co_await connection_manager_->send_action_and_wait_async(payload,
                                                                     echo_id);
}

auto TGBot::get_task_scheduler() -> TaskScheduler & {
  ensure_connection_manager(); // 确保task_scheduler_已初始化
  return *task_scheduler_;
}

auto TGBot::generate_echo_id() -> uint64_t {
  static std::atomic<uint64_t> counter{0};
  return counter.fetch_add(1);
}

void TGBot::ensure_connection_manager() const {
  if (!connection_manager_) {
    throw std::runtime_error("Bot未连接，请先调用connect*方法");
  }
}

auto TGBot::get_telegram_adapter() const
    -> adapter::telegram::ProtocolAdapter & {
  return *static_cast<adapter::telegram::ProtocolAdapter *>(&*adapter_);
}

// --- 媒体文件处理 API 实现 ---

auto TGBot::extract_media_files(const nlohmann::json &message_data)
    -> std::vector<MediaFileInfo> {
  std::vector<MediaFileInfo> media_files;

  try {
    // 处理photo数组 - 选择最大尺寸的图片
    if (message_data.contains("photo") && message_data["photo"].is_array() &&
        !message_data["photo"].empty()) {
      auto photos = message_data["photo"];
      nlohmann::json largest_photo;
      int max_size = 0;

      for (const auto &photo : photos) {
        if (photo.contains("file_size")) {
          int size = photo["file_size"].get<int>();
          if (size > max_size) {
            max_size = size;
            largest_photo = photo;
          }
        } else if (largest_photo.is_null()) {
          largest_photo = photo;
        }
      }

      if (!largest_photo.is_null() && largest_photo.contains("file_id")) {
        MediaFileInfo info;
        info.file_id = largest_photo["file_id"].get<std::string>();

        // 调试：打印photo对象结构
        OBCX_DEBUG("Photo对象内容: {}", largest_photo.dump());

        info.file_unique_id =
            largest_photo.contains("file_unique_id")
                ? largest_photo["file_unique_id"].get<std::string>()
                : "";

        OBCX_DEBUG("提取到的file_unique_id: '{}' (是否为空: {})",
                   info.file_unique_id, info.file_unique_id.empty());

        info.file_type = "photo";
        if (largest_photo.contains("file_size")) {
          info.file_size = largest_photo["file_size"].get<int64_t>();
        }
        media_files.push_back(info);
      }
    }

    // 处理其他单个媒体文件类型
    std::vector<std::string> media_types = {"video",     "audio",   "voice",
                                            "document",  "sticker", "animation",
                                            "video_note"};

    for (const auto &media_type : media_types) {
      if (message_data.contains(media_type) &&
          message_data[media_type].is_object()) {
        auto media_obj = message_data[media_type];
        if (media_obj.contains("file_id")) {
          MediaFileInfo info;
          info.file_id = media_obj["file_id"].get<std::string>();

          // 调试：打印媒体对象结构
          OBCX_DEBUG("{}对象内容: {}", media_type, media_obj.dump());

          info.file_unique_id =
              media_obj.contains("file_unique_id")
                  ? media_obj["file_unique_id"].get<std::string>()
                  : "";

          OBCX_DEBUG("{}提取到的file_unique_id: '{}' (是否为空: {})",
                     media_type, info.file_unique_id,
                     info.file_unique_id.empty());

          info.file_type = media_type;

          if (media_obj.contains("file_size")) {
            info.file_size = media_obj["file_size"].get<int64_t>();
          }

          if (media_obj.contains("mime_type")) {
            info.mime_type = media_obj["mime_type"].get<std::string>();
          }

          // document类型特殊处理文件名
          if (media_type == "document" && media_obj.contains("file_name")) {
            info.file_name = media_obj["file_name"].get<std::string>();
          }

          media_files.push_back(info);
        }
      }
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("提取媒体文件信息时出错: {}", e.what());
  }

  return media_files;
}

auto TGBot::get_media_download_url(const MediaFileInfo &media_info)
    -> asio::awaitable<std::optional<std::string>> {
  try {
    ensure_connection_manager();

    // 使用现有的ConnectionManager的download_file方法
    auto *tg_conn_mgr =
        dynamic_cast<obcx::network::TelegramConnectionManager *>(
            connection_manager_.get());
    if (!tg_conn_mgr) {
      OBCX_ERROR("ConnectionManager不是TelegramConnectionManager类型");
      co_return std::nullopt;
    }

    std::string download_url =
        co_await tg_conn_mgr->download_file(media_info.file_id);
    co_return download_url;

  } catch (const std::exception &e) {
    OBCX_ERROR("获取媒体文件下载链接失败 (file_id: {}, type: {}): {}",
               media_info.file_id, media_info.file_type, e.what());
    co_return std::nullopt;
  }
}

auto TGBot::get_media_download_urls(
    const std::vector<MediaFileInfo> &media_list)
    -> asio::awaitable<std::vector<std::optional<std::string>>> {
  std::vector<std::optional<std::string>> results;
  results.reserve(media_list.size());

  // 并发获取所有下载链接
  std::vector<asio::awaitable<std::optional<std::string>>> tasks;
  tasks.reserve(media_list.size());

  for (const auto &media_info : media_list) {
    tasks.push_back(get_media_download_url(media_info));
  }

  // 等待所有任务完成
  for (auto &task : tasks) {
    results.push_back(co_await std::move(task));
  }

  co_return results;
}

auto TGBot::get_connection_manager() const -> network::IConnectionManager * {
  return connection_manager_.get();
}

} // namespace obcx::core
