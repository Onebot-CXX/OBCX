#include "sticker_export_plugin.hpp"

#include <chrono>
#include <common/logger.hpp>
#include <core/qq_bot.hpp>
#include <core/tg_bot.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <telegram/network/connection_manager.hpp>

namespace sticker_export {

StickerExportPlugin::StickerExportPlugin() {
  PLUGIN_DEBUG(get_name(), "StickerExportPlugin constructor called");
}

StickerExportPlugin::~StickerExportPlugin() {
  shutdown();
  PLUGIN_DEBUG(get_name(), "StickerExportPlugin destructor called");
}

auto StickerExportPlugin::get_name() const -> std::string {
  return "sticker_export";
}

auto StickerExportPlugin::get_version() const -> std::string { return "1.0.0"; }

auto StickerExportPlugin::get_description() const -> std::string {
  return "Download emoji/stickers from QQ and Telegram";
}

auto StickerExportPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing Sticker Export Plugin...");

    if (!load_configuration()) {
      PLUGIN_WARN(get_name(),
                  "No configuration found, using default download_dir: {}",
                  config_.download_dir);
    }

    std::filesystem::create_directories(config_.download_dir);
    PLUGIN_INFO(get_name(), "Download directory: {}", config_.download_dir);

    try {
      auto [lock, bots] = get_bots();

      for (auto &bot_ptr : bots) {
        if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
          qq_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_qq_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered QQ message callback");
        } else if (auto *tg_bot =
                       dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
          tg_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_tg_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered TG message callback");
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "Sticker Export Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during initialization: {}", e.what());
    return false;
  }
}

auto StickerExportPlugin::load_configuration() -> bool {
  auto download_dir_opt = get_config_value<std::string>("download_dir");
  if (download_dir_opt.has_value()) {
    config_.download_dir = download_dir_opt.value();
    PLUGIN_INFO(get_name(), "Loaded download_dir: {}", config_.download_dir);
    return true;
  }
  return false;
}

void StickerExportPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing Sticker Export Plugin...");
    PLUGIN_INFO(get_name(), "Sticker Export Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during deinitialization: {}", e.what());
  }
}

void StickerExportPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down Sticker Export Plugin...");
    {
      std::lock_guard<std::mutex> guard(session_mutex_);
      export_sessions_.clear();
    }
    PLUGIN_INFO(get_name(), "Sticker Export Plugin shutdown complete");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during shutdown: {}", e.what());
  }
}

// ============================================================================
// Session management
// ============================================================================

auto StickerExportPlugin::get_session_key(const std::string &platform,
                                          const std::string &user_id)
    -> std::string {
  return platform + ":" + user_id;
}

auto StickerExportPlugin::is_export_active(const std::string &session_key)
    -> bool {
  std::lock_guard<std::mutex> guard(session_mutex_);
  auto it = export_sessions_.find(session_key);
  return it != export_sessions_.end() && it->second;
}

void StickerExportPlugin::set_export_active(const std::string &session_key,
                                            bool active) {
  std::lock_guard<std::mutex> guard(session_mutex_);
  if (active) {
    export_sessions_[session_key] = true;
  } else {
    export_sessions_.erase(session_key);
  }
}

// ============================================================================
// QQ message handler
// ============================================================================

auto StickerExportPlugin::handle_qq_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {

  bool is_group = event.message_type == "group" && event.group_id.has_value();
  std::string target_id = is_group ? event.group_id.value() : event.user_id;

  std::string command;
  std::string replied_message_id;

  for (const auto &segment : event.message) {
    if (segment.type == "reply") {
      if (segment.data.contains("id")) {
        replied_message_id = segment.data["id"].get<std::string>();
      }
    } else if (segment.type == "text") {
      std::string text = segment.data.value("text", "");
      auto start = text.find_first_not_of(" \t\n\r");
      if (start != std::string::npos) {
        text = text.substr(start);
      }
      if (text == "/dl_sticker" || text.starts_with("/dl_sticker ")) {
        command = "/dl_sticker";
      } else if (text == "/start_sticker_export" ||
                 text.starts_with("/start_sticker_export ")) {
        command = "/start_sticker_export";
      } else if (text == "/end_sticker_export" ||
                 text.starts_with("/end_sticker_export ")) {
        command = "/end_sticker_export";
      }
    }
  }

  std::string session_key = get_session_key("qq", event.user_id);

  if (command == "/dl_sticker") {
    PLUGIN_INFO(get_name(), "Received /dl_sticker command from QQ user {}",
                event.user_id);

    if (!replied_message_id.empty()) {
      auto &qq_bot = static_cast<obcx::core::QQBot &>(bot);
      try {
        std::string msg_response =
            co_await qq_bot.get_message(replied_message_id);
        nlohmann::json msg_json = nlohmann::json::parse(msg_response);

        if (msg_json.value("status", "") != "ok") {
          co_await send_text_qq(bot, "Failed to get the replied message",
                                target_id, is_group);
          co_return;
        }

        auto msg_data = msg_json["data"];
        if (msg_data.contains("message") && msg_data["message"].is_array()) {
          bool found_emoji = false;
          for (const auto &seg_json : msg_data["message"]) {
            obcx::common::MessageSegment seg;
            seg.type = seg_json.value("type", "");
            if (seg_json.contains("data")) {
              seg.data = seg_json["data"];
            }
            if (is_qq_emoji(seg)) {
              found_emoji = true;
              co_await download_and_send_qq_emoji(bot, seg, target_id,
                                                  is_group);
            }
          }
          if (!found_emoji) {
            co_await send_text_qq(
                bot, "No emoji/sticker found in the replied message", target_id,
                is_group);
          }
        }
      } catch (const std::exception &e) {
        PLUGIN_ERROR(get_name(), "Error processing /dl_sticker on QQ: {}",
                     e.what());
      }
    } else {
      auto emoji_segs = extract_qq_emoji_segments(event.message);
      if (!emoji_segs.empty()) {
        for (const auto &seg : emoji_segs) {
          co_await download_and_send_qq_emoji(bot, seg, target_id, is_group);
        }
      } else {
        co_await send_text_qq(bot,
                              "Usage: Reply to a message containing "
                              "emoji/sticker with /dl_sticker",
                              target_id, is_group);
      }
    }
  } else if (command == "/start_sticker_export") {
    set_export_active(session_key, true);
    co_await send_text_qq(
        bot,
        "Sticker export mode activated. Send emoji/stickers and I'll download "
        "them. Use /end_sticker_export to stop.",
        target_id, is_group);
    PLUGIN_INFO(get_name(), "Export mode activated for QQ user {}",
                event.user_id);
  } else if (command == "/end_sticker_export") {
    set_export_active(session_key, false);
    co_await send_text_qq(bot, "Sticker export mode deactivated.", target_id,
                          is_group);
    PLUGIN_INFO(get_name(), "Export mode deactivated for QQ user {}",
                event.user_id);
  } else if (is_export_active(session_key)) {
    auto emoji_segs = extract_qq_emoji_segments(event.message);
    for (const auto &seg : emoji_segs) {
      co_await download_and_send_qq_emoji(bot, seg, target_id, is_group);
    }
  }
}

// ============================================================================
// Telegram message handler
// ============================================================================

auto StickerExportPlugin::handle_tg_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {

  bool is_group = event.message_type == "group" && event.group_id.has_value();
  std::string chat_id = is_group ? event.group_id.value() : event.user_id;

  std::optional<int64_t> topic_id;
  if (event.data.contains("message_thread_id")) {
    topic_id = event.data["message_thread_id"].get<int64_t>();
  }

  std::string command;
  for (const auto &segment : event.message) {
    if (segment.type == "text") {
      std::string text = segment.data.value("text", "");
      auto start = text.find_first_not_of(" \t\n\r");
      if (start != std::string::npos) {
        text = text.substr(start);
      }
      if (text == "/dl_sticker" || text.starts_with("/dl_sticker ") ||
          text.starts_with("/dl_sticker@")) {
        command = "/dl_sticker";
      } else if (text == "/start_sticker_export" ||
                 text.starts_with("/start_sticker_export ") ||
                 text.starts_with("/start_sticker_export@")) {
        command = "/start_sticker_export";
      } else if (text == "/end_sticker_export" ||
                 text.starts_with("/end_sticker_export ") ||
                 text.starts_with("/end_sticker_export@")) {
        command = "/end_sticker_export";
      }
    }
  }

  std::string session_key = get_session_key("tg", event.user_id);

  if (command == "/dl_sticker") {
    PLUGIN_INFO(get_name(), "Received /dl_sticker command from TG user {}",
                event.user_id);

    if (event.data.contains("reply_to_message")) {
      const auto &reply_msg = event.data["reply_to_message"];
      if (reply_msg.contains("sticker") || reply_msg.contains("animation")) {
        // Create a temporary event with the reply message data
        obcx::common::MessageEvent reply_event = event;
        reply_event.data = reply_msg;
        co_await download_and_send_tg_sticker(bot, reply_event);
      } else {
        co_await send_text_tg(
            bot, "The replied message does not contain a sticker or animation.",
            chat_id, topic_id);
      }
    } else if (has_tg_sticker(event)) {
      co_await download_and_send_tg_sticker(bot, event);
    } else {
      co_await send_text_tg(
          bot, "Usage: Reply to a sticker/animation message with /dl_sticker",
          chat_id, topic_id);
    }
  } else if (command == "/start_sticker_export") {
    set_export_active(session_key, true);
    co_await send_text_tg(
        bot,
        "Sticker export mode activated. Send stickers and I'll download them. "
        "Use /end_sticker_export to stop.",
        chat_id, topic_id);
    PLUGIN_INFO(get_name(), "Export mode activated for TG user {}",
                event.user_id);
  } else if (command == "/end_sticker_export") {
    set_export_active(session_key, false);
    co_await send_text_tg(bot, "Sticker export mode deactivated.", chat_id,
                          topic_id);
    PLUGIN_INFO(get_name(), "Export mode deactivated for TG user {}",
                event.user_id);
  } else if (is_export_active(session_key) && has_tg_sticker(event)) {
    co_await download_and_send_tg_sticker(bot, event);
  }
}

// ============================================================================
// QQ emoji download and send
// ============================================================================

auto StickerExportPlugin::download_and_send_qq_emoji(
    obcx::core::IBot &bot, const obcx::common::MessageSegment &segment,
    const std::string &target_id, bool is_group)
    -> boost::asio::awaitable<void> {
  try {
    std::string url;
    std::string filename;

    if (segment.type == "mface") {
      url = segment.data.value("url", "");
      auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
      filename = "mface_" + std::to_string(ts) + ".gif";
    } else {
      url = segment.data.value("url", "");
      if (url.empty()) {
        url = segment.data.value("file", "");
      }
      std::string file_name = segment.data.value("file", "");
      auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
      if (!file_name.empty()) {
        filename = std::to_string(ts) + "_" + file_name;
      } else {
        filename = "emoji_" + std::to_string(ts) + ".png";
      }
    }

    if (url.empty()) {
      PLUGIN_WARN(get_name(), "No URL found for QQ emoji segment");
      co_await send_text_qq(bot, "Could not find download URL for this emoji.",
                            target_id, is_group);
      co_return;
    }

    PLUGIN_INFO(get_name(), "Downloading QQ emoji from: {}", url);
    co_await send_file_qq(bot, url, target_id, is_group);
    PLUGIN_INFO(get_name(), "Sent QQ emoji back: {}", filename);

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Error downloading QQ emoji: {}", e.what());
  }
}

// ============================================================================
// Telegram sticker download and send
// ============================================================================

auto StickerExportPlugin::download_and_send_tg_sticker(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  try {
    auto &tg_bot = static_cast<obcx::core::TGBot &>(bot);

    bool is_group = event.message_type == "group" && event.group_id.has_value();
    std::string chat_id = is_group ? event.group_id.value() : event.user_id;

    std::optional<int64_t> topic_id;
    if (event.data.contains("message_thread_id")) {
      topic_id = event.data["message_thread_id"].get<int64_t>();
    }

    // Determine if sticker or animation
    std::string file_type;
    nlohmann::json media_json;

    if (event.data.contains("sticker")) {
      file_type = "sticker";
      media_json = event.data["sticker"];
    } else if (event.data.contains("animation")) {
      file_type = "animation";
      media_json = event.data["animation"];
    } else {
      PLUGIN_WARN(get_name(), "No sticker/animation found in TG message");
      co_return;
    }

    // Build MediaFileInfo
    obcx::core::MediaFileInfo media_info;
    media_info.file_id = media_json.value("file_id", "");
    media_info.file_unique_id = media_json.value("file_unique_id", "");
    media_info.file_type = file_type;

    if (media_json.contains("file_size")) {
      media_info.file_size = media_json["file_size"].get<int64_t>();
    }

    // Determine mime_type
    if (file_type == "sticker") {
      if (media_json.contains("is_animated") &&
          media_json["is_animated"].get<bool>()) {
        media_info.mime_type = "application/tgs";
      } else if (media_json.contains("is_video") &&
                 media_json["is_video"].get<bool>()) {
        media_info.mime_type = "video/webm";
      } else {
        media_info.mime_type = "image/webp";
      }
    } else if (file_type == "animation") {
      media_info.mime_type = media_json.value("mime_type", "video/mp4");
    }

    if (media_info.file_id.empty()) {
      PLUGIN_WARN(get_name(), "Empty file_id for TG sticker");
      co_return;
    }

    PLUGIN_INFO(get_name(), "Downloading TG {} (file_id: {})", file_type,
                media_info.file_id);

    // Get download URL
    auto download_url_opt = co_await tg_bot.get_media_download_url(media_info);
    if (!download_url_opt.has_value()) {
      co_await send_text_tg(bot, "Failed to get download URL for this sticker.",
                            chat_id, topic_id);
      co_return;
    }

    std::string download_url = download_url_opt.value();

    // Download file content via connection manager
    auto *conn_manager =
        dynamic_cast<obcx::network::TelegramConnectionManager *>(
            tg_bot.get_connection_manager());
    if (!conn_manager) {
      PLUGIN_ERROR(get_name(), "Cannot get TelegramConnectionManager");
      co_return;
    }

    auto file_content =
        co_await conn_manager->download_file_content(download_url);
    if (file_content.empty()) {
      co_await send_text_tg(bot, "Failed to download sticker file.", chat_id,
                            topic_id);
      co_return;
    }

    // Determine file extension
    std::string extension = ".webp";
    std::string mime = media_info.mime_type.value_or("image/webp");
    if (mime == "video/webm") {
      extension = ".webm";
    } else if (mime == "application/tgs") {
      extension = ".tgs";
    } else if (mime == "video/mp4") {
      extension = ".mp4";
    } else if (mime == "image/gif") {
      extension = ".gif";
    }

    // Save to download_dir
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    std::string filename = file_type + "_" + std::to_string(ts) + extension;
    std::string file_path = config_.download_dir + "/" + filename;

    {
      std::ofstream file(file_path, std::ios::binary);
      if (!file) {
        PLUGIN_ERROR(get_name(), "Cannot create file: {}", file_path);
        co_return;
      }
      file.write(file_content.data(),
                 static_cast<std::streamsize>(file_content.size()));
    }

    PLUGIN_INFO(get_name(), "Saved TG {} to: {} ({} bytes)", file_type,
                file_path, file_content.size());

    co_await send_file_tg(bot, file_path, chat_id, topic_id);

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Error downloading TG sticker: {}", e.what());
  }
}

// ============================================================================
// Send helpers
// ============================================================================

auto StickerExportPlugin::send_file_qq(obcx::core::IBot &bot,
                                       const std::string &file_path,
                                       const std::string &target_id,
                                       bool is_group)
    -> boost::asio::awaitable<void> {
  try {
    obcx::common::Message msg;
    obcx::common::MessageSegment seg;
    seg.type = "image";
    if (file_path.starts_with("http://") || file_path.starts_with("https://")) {
      seg.data["file"] = file_path;
    } else {
      seg.data["file"] = "file:///" + file_path;
    }
    msg.push_back(seg);

    if (is_group) {
      co_await bot.send_group_message(target_id, msg);
    } else {
      co_await bot.send_private_message(target_id, msg);
    }
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send file on QQ: {}", e.what());
  }
}

auto StickerExportPlugin::send_file_tg(obcx::core::IBot &bot,
                                       const std::string &file_path,
                                       const std::string &chat_id,
                                       std::optional<int64_t> topic_id)
    -> boost::asio::awaitable<void> {
  try {
    auto &tg_bot = static_cast<obcx::core::TGBot &>(bot);

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
      PLUGIN_ERROR(get_name(), "Cannot read file: {}", file_path);
      co_return;
    }
    std::string file_content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    file.close();

    std::filesystem::path p(file_path);
    std::string filename = p.filename().string();
    std::string ext = p.extension().string();

    std::string mime_type = "application/octet-stream";
    if (ext == ".webp") {
      mime_type = "image/webp";
    } else if (ext == ".webm") {
      mime_type = "video/webm";
    } else if (ext == ".tgs") {
      mime_type = "application/x-tgsticker";
    } else if (ext == ".mp4") {
      mime_type = "video/mp4";
    } else if (ext == ".gif") {
      mime_type = "image/gif";
    } else if (ext == ".png") {
      mime_type = "image/png";
    } else if (ext == ".jpg" || ext == ".jpeg") {
      mime_type = "image/jpeg";
    }

    if (mime_type.starts_with("image/")) {
      co_await tg_bot.send_photo_bytes(chat_id, file_content, filename,
                                       mime_type, "", topic_id);
    } else {
      obcx::common::Message msg;
      obcx::common::MessageSegment seg;
      seg.type = "document";
      seg.data["file"] = "file:///" + file_path;
      seg.data["name"] = filename;
      msg.push_back(seg);

      if (topic_id.has_value()) {
        co_await tg_bot.send_topic_message(chat_id, topic_id.value(), msg);
      } else {
        co_await tg_bot.send_group_message(chat_id, msg);
      }
    }

    PLUGIN_INFO(get_name(), "Sent file back on TG: {}", filename);
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send file on TG: {}", e.what());
  }
}

auto StickerExportPlugin::send_text_qq(obcx::core::IBot &bot,
                                       const std::string &text,
                                       const std::string &target_id,
                                       bool is_group)
    -> boost::asio::awaitable<void> {
  try {
    obcx::common::Message message;
    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    message.push_back(text_segment);

    if (is_group) {
      co_await bot.send_group_message(target_id, message);
    } else {
      co_await bot.send_private_message(target_id, message);
    }
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send text on QQ: {}", e.what());
  }
}

auto StickerExportPlugin::send_text_tg(obcx::core::IBot &bot,
                                       const std::string &text,
                                       const std::string &chat_id,
                                       std::optional<int64_t> topic_id)
    -> boost::asio::awaitable<void> {
  try {
    auto &tg_bot = static_cast<obcx::core::TGBot &>(bot);

    obcx::common::Message message;
    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    message.push_back(text_segment);

    if (topic_id.has_value()) {
      co_await tg_bot.send_topic_message(chat_id, topic_id.value(), message);
    } else {
      co_await tg_bot.send_group_message(chat_id, message);
    }
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send text on TG: {}", e.what());
  }
}

// ============================================================================
// Emoji detection helpers
// ============================================================================

auto StickerExportPlugin::is_qq_emoji(
    const obcx::common::MessageSegment &segment) -> bool {
  if (segment.type == "mface") {
    return true;
  }

  if (segment.type != "image") {
    return false;
  }

  std::string file_name = segment.data.value("file", "");
  std::string url = segment.data.value("url", "");

  if (!file_name.empty() && (file_name.find("sticker") != std::string::npos ||
                             file_name.find("emoji") != std::string::npos)) {
    return true;
  }

  if (segment.data.contains("subType") && segment.data.at("subType") == 1) {
    return true;
  }

  if (!url.empty() && (url.find("emoticon") != std::string::npos ||
                       url.find("sticker") != std::string::npos ||
                       url.find("emoji") != std::string::npos)) {
    return true;
  }

  return false;
}

auto StickerExportPlugin::extract_qq_emoji_segments(
    const obcx::common::Message &message)
    -> std::vector<obcx::common::MessageSegment> {
  std::vector<obcx::common::MessageSegment> result;
  for (const auto &segment : message) {
    if (is_qq_emoji(segment)) {
      result.push_back(segment);
    }
  }
  return result;
}

auto StickerExportPlugin::has_tg_sticker(
    const obcx::common::MessageEvent &event) -> bool {
  return event.data.contains("sticker") || event.data.contains("animation");
}

} // namespace sticker_export

// Export the plugin
OBCX_PLUGIN_EXPORT(sticker_export::StickerExportPlugin)
