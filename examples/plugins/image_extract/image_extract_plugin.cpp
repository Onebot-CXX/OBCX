#include "image_extract_plugin.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <common/logger.hpp>
#include <core/qq_bot.hpp>

namespace image_extract {

ImageExtractPlugin::ImageExtractPlugin() {
  PLUGIN_DEBUG(get_name(), "ImageExtractPlugin constructor called");
}

ImageExtractPlugin::~ImageExtractPlugin() {
  shutdown();
  PLUGIN_DEBUG(get_name(), "ImageExtractPlugin destructor called");
}

auto ImageExtractPlugin::get_name() const -> std::string {
  return "image_extract";
}

auto ImageExtractPlugin::get_version() const -> std::string { return "1.0.0"; }

auto ImageExtractPlugin::get_description() const -> std::string {
  return "Extract image URLs from merged forward messages";
}

auto ImageExtractPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing Image Extract Plugin...");

    // Load configuration
    if (!load_configuration()) {
      PLUGIN_WARN(get_name(),
                  "No configuration found, plugin will work in all groups");
    }

    // Register event callbacks
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
          break;
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "Image Extract Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during initialization: {}", e.what());
    return false;
  }
}

auto ImageExtractPlugin::load_configuration() -> bool {
  // Try to get allowed_groups from config
  auto allowed_groups_opt =
      get_config_value<std::vector<std::string>>("allowed_groups");
  if (allowed_groups_opt.has_value()) {
    for (const auto &group : allowed_groups_opt.value()) {
      allowed_groups_.insert(group);
    }
    PLUGIN_INFO(get_name(), "Loaded {} allowed groups", allowed_groups_.size());
    return true;
  }
  return false;
}

auto ImageExtractPlugin::is_allowed_group(const std::string &group_id) const
    -> bool {
  // If no whitelist configured, allow all groups
  if (allowed_groups_.empty()) {
    return true;
  }
  return allowed_groups_.count(group_id) > 0;
}

void ImageExtractPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing Image Extract Plugin...");
    PLUGIN_INFO(get_name(), "Image Extract Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during deinitialization: {}", e.what());
  }
}

void ImageExtractPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down Image Extract Plugin...");
    PLUGIN_INFO(get_name(), "Image Extract Plugin shutdown complete");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during shutdown: {}", e.what());
  }
}

auto ImageExtractPlugin::handle_qq_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  const std::string &group_id = event.group_id.value();

  // Check if message contains the command (check text segments)
  bool is_command = false;
  std::string replied_message_id;

  for (const auto &segment : event.message) {
    if (segment.type == "reply") {
      if (segment.data.contains("id")) {
        replied_message_id = segment.data["id"].get<std::string>();
      }
    } else if (segment.type == "text") {
      std::string text = segment.data.value("text", "");
      if (text == "/ie") {
        is_command = true;
      }
    }
  }

  if (!is_command) {
    co_return;
  }

  // Check if group is in whitelist
  if (!is_allowed_group(group_id)) {
    PLUGIN_DEBUG(get_name(), "Group {} is not in whitelist, ignoring",
                 group_id);
    co_return;
  }

  PLUGIN_DEBUG(get_name(), "Received image extract command in group {}",
               group_id);

  if (replied_message_id.empty()) {
    co_await send_text_response(bot, group_id,
                                "Please reply to a merged forward message");
    co_return;
  }

  // Get the replied message to find the forward segment
  auto &qq_bot = static_cast<obcx::core::QQBot &>(bot);

  try {
    std::string msg_response = co_await qq_bot.get_message(replied_message_id);
    nlohmann::json msg_json = nlohmann::json::parse(msg_response);

    if (msg_json.value("status", "") != "ok") {
      co_await send_text_response(bot, group_id,
                                  "Failed to get the replied message");
      co_return;
    }

    // Find the forward segment in the replied message
    std::string forward_id;
    auto msg_data = msg_json["data"];

    if (msg_data.contains("message") && msg_data["message"].is_array()) {
      for (const auto &segment : msg_data["message"]) {
        if (segment.value("type", "") == "forward") {
          if (segment.contains("data") && segment["data"].contains("id")) {
            forward_id = segment["data"]["id"].get<std::string>();
          }
          break;
        }
      }
    }

    if (forward_id.empty()) {
      co_await send_text_response(
          bot, group_id,
          "The replied message does not contain a merged forward message");
      co_return;
    }

    // Get the forward message content
    std::string forward_response = co_await qq_bot.get_forward_msg(forward_id);
    nlohmann::json forward_json = nlohmann::json::parse(forward_response);

    if (forward_json.value("status", "") != "ok") {
      co_await send_text_response(bot, group_id,
                                  "Failed to get the forward message content");
      co_return;
    }

    // Extract all image URLs (including from reply references)
    auto image_urls = co_await extract_image_urls(qq_bot, forward_json["data"]);

    if (image_urls.empty()) {
      co_await send_text_response(
          bot, group_id, "No images found in the merged forward message");
      co_return;
    }

    PLUGIN_INFO(get_name(), "Found {} images in forward message",
                image_urls.size());

    // Get self info for the forward message sender
    std::string login_response = co_await qq_bot.get_login_info();
    nlohmann::json login_json = nlohmann::json::parse(login_response);
    std::string self_id = "10000";
    if (login_json.value("status", "") == "ok" &&
        login_json["data"].contains("user_id")) {
      self_id = std::to_string(login_json["data"]["user_id"].get<int64_t>());
    }

    // Send the forward response with image URLs
    co_await send_forward_response(bot, group_id, image_urls, self_id);

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Error processing image extract: {}", e.what());
    // Note: Cannot use co_await in catch block, just log the error
  }
}

auto ImageExtractPlugin::extract_image_urls(obcx::core::IBot &bot,
                                            const nlohmann::json &forward_data)
    -> boost::asio::awaitable<std::vector<std::string>> {
  std::vector<std::string> urls;
  std::vector<std::string> reply_ids;

  auto &qq_bot = static_cast<obcx::core::QQBot &>(bot);

  // First pass: extract direct images and collect reply IDs
  if (forward_data.contains("messages") &&
      forward_data["messages"].is_array()) {
    for (const auto &msg_node : forward_data["messages"]) {
      if (msg_node.contains("content")) {
        extract_images_and_replies(msg_node["content"], urls, reply_ids);
      }
    }
  } else if (forward_data.contains("message") &&
             forward_data["message"].is_array()) {
    for (const auto &msg_node : forward_data["message"]) {
      if (msg_node.contains("content")) {
        extract_images_and_replies(msg_node["content"], urls, reply_ids);
      }
    }
  }

  // Second pass: fetch messages referenced by reply IDs and extract their
  // images
  for (const auto &reply_id : reply_ids) {
    try {
      std::string msg_response = co_await qq_bot.get_message(reply_id);

      nlohmann::json msg_json = nlohmann::json::parse(msg_response);
      if (msg_json.value("status", "") == "ok" && msg_json.contains("data") &&
          msg_json["data"].contains("message")) {
        std::vector<std::string> dummy_replies; // Don't recurse further
        extract_images_and_replies(msg_json["data"]["message"], urls,
                                   dummy_replies);
      }
    } catch (const std::exception &e) {
      PLUGIN_WARN(get_name(), "Failed to fetch reply message {}: {}", reply_id,
                  e.what());
    }
  }

  co_return urls;
}

void ImageExtractPlugin::extract_images_and_replies(
    const nlohmann::json &content, std::vector<std::string> &urls,
    std::vector<std::string> &reply_ids) {
  // Content can be a string (CQ code) or an array of segments
  if (content.is_string()) {
    // Parse CQ code format: [CQ:image,file=xxx,url=xxx]
    std::string content_str = content.get<std::string>();
    size_t pos = 0;
    while ((pos = content_str.find("[CQ:image,", pos)) != std::string::npos) {
      size_t end = content_str.find("]", pos);
      if (end == std::string::npos)
        break;

      std::string cq_code = content_str.substr(pos, end - pos + 1);

      // Extract URL from CQ code
      size_t url_pos = cq_code.find("url=");
      if (url_pos != std::string::npos) {
        url_pos += 4; // Skip "url="
        size_t url_end = cq_code.find_first_of(",]", url_pos);
        if (url_end != std::string::npos) {
          std::string url = cq_code.substr(url_pos, url_end - url_pos);
          if (!url.empty()) {
            urls.push_back(url);
          }
        }
      }

      pos = end + 1;
    }

    // Also check for reply CQ codes: [CQ:reply,id=xxx]
    pos = 0;
    while ((pos = content_str.find("[CQ:reply,", pos)) != std::string::npos) {
      size_t end = content_str.find("]", pos);
      if (end == std::string::npos)
        break;

      std::string cq_code = content_str.substr(pos, end - pos + 1);
      size_t id_pos = cq_code.find("id=");
      if (id_pos != std::string::npos) {
        id_pos += 3;
        size_t id_end = cq_code.find_first_of(",]", id_pos);
        if (id_end != std::string::npos) {
          std::string id = cq_code.substr(id_pos, id_end - id_pos);
          if (!id.empty()) {
            reply_ids.push_back(id);
          }
        }
      }

      pos = end + 1;
    }
  } else if (content.is_array()) {
    for (const auto &segment : content) {
      std::string seg_type = segment.value("type", "");

      if (seg_type == "image") {
        if (segment.contains("data")) {
          const auto &data = segment["data"];
          // Try different fields for URL
          if (data.contains("url") && !data["url"].get<std::string>().empty()) {
            urls.push_back(data["url"].get<std::string>());
          } else if (data.contains("file") &&
                     data["file"].get<std::string>().starts_with("http")) {
            urls.push_back(data["file"].get<std::string>());
          }
        }
      } else if (seg_type == "reply") {
        if (segment.contains("data") && segment["data"].contains("id")) {
          std::string reply_id = segment["data"]["id"].get<std::string>();
          if (!reply_id.empty()) {
            reply_ids.push_back(reply_id);
          }
        }
      }
    }
  }
}

auto ImageExtractPlugin::send_forward_response(
    obcx::core::IBot &bot, const std::string &group_id,
    const std::vector<std::string> &image_urls, const std::string &self_id)
    -> boost::asio::awaitable<void> {
  try {
    auto &qq_bot = static_cast<obcx::core::QQBot &>(bot);

    // Build forward message nodes
    nlohmann::json messages = nlohmann::json::array();

    for (size_t i = 0; i < image_urls.size(); ++i) {
      nlohmann::json node;
      node["type"] = "node";
      node["data"]["name"] = "Image " + std::to_string(i + 1);
      node["data"]["uin"] = self_id;
      node["data"]["content"] = image_urls[i];
      messages.push_back(node);
    }

    co_await qq_bot.send_group_forward_msg(group_id, messages);
    PLUGIN_INFO(get_name(), "Sent forward message with {} image URLs",
                image_urls.size());

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send forward response: {}", e.what());
    // Note: Cannot use co_await in catch block, just log the error
  }
}

auto ImageExtractPlugin::send_text_response(obcx::core::IBot &bot,
                                            const std::string &group_id,
                                            const std::string &text)
    -> boost::asio::awaitable<void> {
  try {
    obcx::common::Message message;
    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    message.push_back(text_segment);

    co_await bot.send_group_message(group_id, message);
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send text response: {}", e.what());
  }
}

} // namespace image_extract

// Export the plugin
OBCX_PLUGIN_EXPORT(image_extract::ImageExtractPlugin)
