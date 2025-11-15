#include "telegram/adapter/protocol_adapter.hpp"

#include "common/json_utils.hpp"
#include "common/logger.hpp"

#include <nlohmann/json.hpp>

namespace obcx::adapter::telegram {

auto ProtocolAdapter::parse_event(std::string_view json_str)
    -> std::optional<common::Event> {
  try {
    auto json = nlohmann::json::parse(json_str);
    OBCX_DEBUG("Parsing Telegram event: {}", json_str);

    // Check if this is an update
    if (json.contains("update_id")) {
      // Handle different types of updates
      if (json.contains("message")) {
        // Message update
        return parse_message_event(json);
      }
      if (json.contains("edited_message")) {
        // Edited message update
        return parse_edited_message_event(json);
      }
      if (json.contains("channel_post")) {
        // Channel post update
        return parse_channel_post_event(json);
      }
      if (json.contains("edited_channel_post")) {
        // Edited channel post update
        return parse_edited_channel_post_event(json);
      }
      if (json.contains("callback_query")) {
        // Callback query update
        return parse_callback_query_event(json);
      }
      OBCX_DEBUG("Unhandled update type in Telegram update");
      return std::nullopt;
    }
    OBCX_DEBUG("No update_id field in JSON");

    return std::nullopt;
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to parse Telegram event: {}", e.what());
    OBCX_ERROR("JSON string was: {}", json_str);
    return std::nullopt;
  }
}

auto ProtocolAdapter::parse_message_event(const nlohmann::json &update_json)
    -> std::optional<common::Event> {
  try {
    auto message = update_json["message"];

    // Create message event
    common::MessageEvent event{};
    event.time = std::chrono::system_clock::now();
    event.post_type = "message";
    event.type = common::EventType::message;
    event.self_id =
        "0"; // Bot ID should be set properly in a real implementation

    // Store the original message data for access to additional fields
    event.data = message;

    // Extract update ID
    if (update_json.contains("update_id")) {
      // We don't store update_id in the event, but we could if needed
    }

    // Extract message ID
    if (message.contains("message_id")) {
      event.message_id = std::to_string(message["message_id"].get<int64_t>());
      OBCX_DEBUG("Extracted message_id: {}", event.message_id);
    }

    // Extract user information
    if (message.contains("from")) {
      auto from = message["from"];
      if (from.contains("id")) {
        event.user_id = std::to_string(from["id"].get<int64_t>());
        OBCX_DEBUG("Extracted user_id: {}", event.user_id);
      }
    }

    // Extract chat information
    if (message.contains("chat")) {
      auto chat = message["chat"];
      if (chat.contains("id")) {
        std::string chat_id = std::to_string(chat["id"].get<int64_t>());
        OBCX_DEBUG("Extracted chat_id: {}", chat_id);

        // Check chat type to determine if it's a group or private chat
        if (chat.contains("type")) {
          std::string chat_type = chat["type"];
          OBCX_DEBUG("Chat type: {}", chat_type);

          if (chat_type == "supergroup" || chat_type == "group") {
            event.group_id = chat_id;
            event.message_type = "group";
            OBCX_DEBUG("Set group_id: {}", chat_id);
          } else if (chat_type == "private") {
            event.message_type = "private";
          } else if (chat_type == "channel") {
            event.message_type = "channel";
          }
        }
      }
    }

    // Extract message content
    if (message.contains("text")) {
      event.raw_message = message["text"];
      OBCX_DEBUG("Extracted message text: {}", event.raw_message);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "text";
      segment.data["text"] = message["text"];
      event.message.push_back(segment);
    } else if (message.contains("photo")) {
      // Handle photo messages
      auto photos = message["photo"];
      if (!photos.empty()) {
        // Get the largest photo (last in array)
        auto photo = photos.back();
        std::string file_id = photo["file_id"];

        event.raw_message = "[图片]";
        OBCX_DEBUG("Extracted photo file_id: {}", file_id);

        // Create message segments
        common::MessageSegment segment;
        segment.type = "image";
        segment.data["file_id"] = file_id;
        // If the photo has a caption, include it in the message
        if (message.contains("caption")) {
          segment.data["caption"] = message["caption"];
          event.raw_message += message["caption"];
        }
        event.message.push_back(segment);
      }
    } else if (message.contains("sticker")) {
      // Handle sticker messages
      auto sticker = message["sticker"];
      std::string file_id = sticker["file_id"];

      event.raw_message = "[贴纸]";
      OBCX_DEBUG("Extracted sticker file_id: {}", file_id);

      common::MessageSegment segment;

      segment.type = "sticker";
      segment.data["file_id"] = file_id;
      segment.data["is_sticker"] = true;
      segment.data["file_unique_id"] =
          sticker["file_unique_id"].get<std::string>();
      segment.data["is_animated"] = sticker["is_animated"].get<bool>();
      segment.data["is_video"] = sticker["is_video"].get<bool>();
      // If the sticker has an emoji, include it in the message
      if (sticker.contains("emoji")) {
        segment.data["emoji"] = sticker["emoji"];
        event.raw_message = "[" + sticker["emoji"].get<std::string>() + "贴纸]";
      }
      event.message.push_back(segment);
    } else if (message.contains("video")) {
      // Handle video messages
      auto video = message["video"];
      std::string file_id = video["file_id"];

      event.raw_message = "[视频]";
      OBCX_DEBUG("Extracted video file_id: {}", file_id);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "video";
      segment.data["file_id"] = file_id;
      if (video.contains("file_unique_id")) {
        segment.data["file_unique_id"] = video["file_unique_id"];
      }
      if (video.contains("width")) {
        segment.data["width"] = video["width"];
      }
      if (video.contains("height")) {
        segment.data["height"] = video["height"];
      }
      if (video.contains("duration")) {
        segment.data["duration"] = video["duration"];
      }
      // If the video has a caption, include it in the message
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("animation")) {
      // Handle animation messages (GIFs)
      auto animation = message["animation"];
      std::string file_id = animation["file_id"];

      event.raw_message = "[动画]";
      OBCX_DEBUG("Extracted animation file_id: {}", file_id);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "animation";
      segment.data["file_id"] = file_id;
      if (animation.contains("file_unique_id")) {
        segment.data["file_unique_id"] = animation["file_unique_id"];
      }
      if (animation.contains("width")) {
        segment.data["width"] = animation["width"];
      }
      if (animation.contains("height")) {
        segment.data["height"] = animation["height"];
      }
      if (animation.contains("duration")) {
        segment.data["duration"] = animation["duration"];
      }
      // If the animation has a caption, include it in the message
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("document")) {
      // Handle document messages
      auto document = message["document"];
      std::string file_id = document["file_id"];

      event.raw_message = "[文档]";
      OBCX_DEBUG("Extracted document file_id: {}", file_id);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "document";
      segment.data["file_id"] = file_id;
      if (document.contains("file_unique_id")) {
        segment.data["file_unique_id"] = document["file_unique_id"];
      }
      if (document.contains("file_name")) {
        segment.data["file_name"] = document["file_name"];
        event.raw_message =
            "[文档: " + document["file_name"].get<std::string>() + "]";
      }
      if (document.contains("mime_type")) {
        segment.data["mime_type"] = document["mime_type"];
      }
      // If the document has a caption, include it in the message
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("audio")) {
      // Handle audio messages
      auto audio = message["audio"];
      std::string file_id = audio["file_id"];

      event.raw_message = "[音频]";
      OBCX_DEBUG("Extracted audio file_id: {}", file_id);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "audio";
      segment.data["file_id"] = file_id;
      if (audio.contains("file_unique_id")) {
        segment.data["file_unique_id"] = audio["file_unique_id"];
      }
      if (audio.contains("duration")) {
        segment.data["duration"] = audio["duration"];
      }
      if (audio.contains("title")) {
        segment.data["title"] = audio["title"];
        event.raw_message = "[音频: " + audio["title"].get<std::string>() + "]";
      }
      // If the audio has a caption, include it in the message
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("voice")) {
      // Handle voice messages
      auto voice = message["voice"];
      std::string file_id = voice["file_id"];

      event.raw_message = "[语音]";
      OBCX_DEBUG("Extracted voice file_id: {}", file_id);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "voice";
      segment.data["file_id"] = file_id;
      if (voice.contains("file_unique_id")) {
        segment.data["file_unique_id"] = voice["file_unique_id"];
      }
      if (voice.contains("duration")) {
        segment.data["duration"] = voice["duration"];
      }
      event.message.push_back(segment);
    } else if (message.contains("video_note")) {
      // Handle video note messages (circular video messages)
      auto video_note = message["video_note"];
      std::string file_id = video_note["file_id"];

      event.raw_message = "[视频消息]";
      OBCX_DEBUG("Extracted video_note file_id: {}", file_id);

      // Create message segments
      common::MessageSegment segment;
      segment.type = "video_note";
      segment.data["file_id"] = file_id;
      if (video_note.contains("file_unique_id")) {
        segment.data["file_unique_id"] = video_note["file_unique_id"];
      }
      if (video_note.contains("length")) {
        segment.data["length"] = video_note["length"];
      }
      if (video_note.contains("duration")) {
        segment.data["duration"] = video_note["duration"];
      }
      event.message.push_back(segment);
    }

    event.font = 0; // Not applicable for Telegram

    OBCX_DEBUG("Successfully parsed Telegram message event");
    return event;
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to parse Telegram message event: {}", e.what());
    return std::nullopt;
  }
}

auto ProtocolAdapter::parse_edited_message_event(
    const nlohmann::json &update_json) -> std::optional<common::Event> {
  // Handle edited messages with special identification
  if (update_json.contains("edited_message")) {
    auto update_copy = update_json;
    update_copy["message"] = update_copy["edited_message"];
    update_copy.erase("edited_message");

    // Parse as regular message event but add edit flag
    auto event_opt = parse_message_event(update_copy);
    if (event_opt.has_value()) {
      // Extract MessageEvent from variant
      if (auto *msg_event =
              std::get_if<common::MessageEvent>(&event_opt.value())) {
        // Mark this as an edited message by adding edit flag to data
        msg_event->data["is_edited"] = true;
        msg_event->sub_type = "edited";
        OBCX_DEBUG("标记为编辑消息: message_id={}", msg_event->message_id);
        return event_opt;
      }
    }
  }
  return std::nullopt;
}

auto ProtocolAdapter::parse_channel_post_event(
    const nlohmann::json &update_json) -> std::optional<common::Event> {
  // For now, we'll treat channel posts similar to regular messages
  // In a full implementation, we might want to handle them differently
  if (update_json.contains("channel_post")) {
    auto update_copy = update_json;
    update_copy["message"] = update_copy["channel_post"];
    update_copy.erase("channel_post");
    return parse_message_event(update_copy);
  }
  return std::nullopt;
}

auto ProtocolAdapter::parse_edited_channel_post_event(
    const nlohmann::json &update_json) -> std::optional<common::Event> {
  // For now, we'll treat edited channel posts similar to regular messages
  // In a full implementation, we might want to handle them differently
  if (update_json.contains("edited_channel_post")) {
    auto update_copy = update_json;
    update_copy["message"] = update_copy["edited_channel_post"];
    update_copy.erase("edited_channel_post");
    return parse_message_event(update_copy);
  }
  return std::nullopt;
}

auto ProtocolAdapter::parse_callback_query_event(
    const nlohmann::json &update_json) -> std::optional<common::Event> {
  // Callback queries are a different type of event
  // For now, we'll return a basic notice event
  try {
    if (update_json.contains("callback_query")) {
      auto callback_query = update_json["callback_query"];

      // Create a notice event for callback queries
      common::NoticeEvent event{};
      event.time = std::chrono::system_clock::now();
      event.post_type = "notice";
      event.type = common::EventType::notice;
      event.self_id =
          "0"; // Bot ID should be set properly in a real implementation
      event.notice_type = "callback_query";

      // Extract user ID if available
      if (callback_query.contains("from") &&
          callback_query["from"].contains("id")) {
        event.user_id =
            std::to_string(callback_query["from"]["id"].get<int64_t>());
      }

      // Extract chat ID if available
      if (callback_query.contains("message") &&
          callback_query["message"].contains("chat") &&
          callback_query["message"]["chat"].contains("id")) {
        event.group_id = std::to_string(
            callback_query["message"]["chat"]["id"].get<int64_t>());
      }

      OBCX_DEBUG("Successfully parsed Telegram callback query event");
      return event;
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to parse Telegram callback query event: {}", e.what());
  }

  return std::nullopt;
}

// Serialization methods
auto ProtocolAdapter::serialize_send_message_request(
    std::string_view target_id, const common::Message &message,
    const std::optional<uint64_t> &echo) -> std::string {
  return serialize_send_topic_message_request(target_id, message, echo,
                                              std::nullopt);
}

auto ProtocolAdapter::serialize_send_topic_message_request(
    std::string_view target_id, const common::Message &message,
    const std::optional<uint64_t> &echo, const std::optional<int64_t> &topic_id)
    -> std::string {
  // Check if the message contains different media types
  bool has_image = false;
  bool has_animation = false;
  bool has_video = false;
  bool has_audio = false;
  bool has_voice = false;
  bool has_document = false;
  bool has_sticker = false;
  bool has_video_note = false;

  // Check for media types in priority order
  // Priority: sticker > animation > video > image > video_note > audio > voice
  // > document
  for (const auto &segment : message) {
    if (segment.type == "sticker") {
      has_sticker = true;
      break;
    }
    if (segment.type == "animation") {
      has_animation = true;
      break;
    }
    if (segment.type == "video") {
      has_video = true;
      break;
    }
    if (segment.type == "image") {
      has_image = true;
      break;
    }
    if (segment.type == "video_note") {
      has_video_note = true;
      break;
    }
    if (segment.type == "audio") {
      has_audio = true;
      break;
    }
    if (segment.type == "voice") {
      has_voice = true;
      break;
    }
    if (segment.type == "document") {
      has_document = true;
      break;
    }
  }

  // Check if the message contains reply segments
  std::optional<std::string> reply_to_message_id;
  for (const auto &segment : message) {
    if (segment.type == "reply") {
      if (segment.data.contains("id")) {
        reply_to_message_id = segment.data.at("id");
        break;
      }
    }
  }

  // Process media types in priority order
  // Priority: sticker > animation > video > image > video_note > audio > voice
  // > document

  // If message contains stickers, use sendSticker method
  if (has_sticker) {
    // Handle the first sticker in the message
    for (const auto &segment : message) {
      if (segment.type == "sticker") {
        nlohmann::json json;
        json["method"] = "sendSticker";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different sticker sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram sticker
          json["sticker"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending sticker from URL
          json["sticker"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["sticker"] = segment.data.at("file");
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("Telegram sendSticker 添加回复消息ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains animations, use sendAnimation method
  if (has_animation) {
    // Handle the first animation in the message
    for (const auto &segment : message) {
      if (segment.type == "animation") {
        nlohmann::json json;
        json["method"] = "sendAnimation";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different animation sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram animation
          json["animation"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending animation from URL
          json["animation"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["animation"] = segment.data.at("file");
        }

        // Add caption if present
        std::string caption;
        for (const auto &caption_segment : message) {
          if (caption_segment.type == "text") {
            caption += caption_segment.data.at("text");
          }
        }

        if (!caption.empty()) {
          json["caption"] = caption;
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains videos, use sendVideo method
  if (has_video) {
    // Handle the first video in the message
    for (const auto &segment : message) {
      if (segment.type == "video") {
        nlohmann::json json;
        json["method"] = "sendVideo";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different video sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram video
          json["video"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending video from URL
          json["video"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["video"] = segment.data.at("file");
        }

        // Add caption if present
        std::string caption;
        for (const auto &caption_segment : message) {
          if (caption_segment.type == "text") {
            caption += caption_segment.data.at("text");
          }
        }

        if (!caption.empty()) {
          json["caption"] = caption;
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("Telegram sendVideo 添加回复消息ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains video_note, use sendVideoNote method
  if (has_video_note) {
    // Handle the first video note in the message
    for (const auto &segment : message) {
      if (segment.type == "video_note") {
        nlohmann::json json;
        json["method"] = "sendVideoNote";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different video note sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram video note
          json["video_note"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending video note from URL
          json["video_note"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["video_note"] = segment.data.at("file");
        }

        // Add optional metadata
        if (segment.data.contains("length")) {
          json["length"] = segment.data.at("length");
        }
        if (segment.data.contains("duration")) {
          json["duration"] = segment.data.at("duration");
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("Telegram sendVideoNote 添加回复消息ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains images, we need to use sendPhoto method
  if (has_image) {
    // For now, we'll handle the first image in the message
    // In a full implementation, we might want to send multiple images
    for (const auto &segment : message) {
      if (segment.type == "image") {
        nlohmann::json json;
        json["method"] = "sendPhoto";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different image sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram image
          json["photo"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending image from URL
          json["photo"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["photo"] = segment.data.at("file");
        }

        // Add caption if present
        std::string caption;
        for (const auto &caption_segment : message) {
          if (caption_segment.type == "text") {
            caption += caption_segment.data.at("text");
          }
        }

        if (!caption.empty()) {
          json["caption"] = caption;
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains audio, use sendAudio method
  if (has_audio) {
    // Handle the first audio in the message
    for (const auto &segment : message) {
      if (segment.type == "audio") {
        nlohmann::json json;
        json["method"] = "sendAudio";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different audio sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram audio
          json["audio"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending audio from URL
          json["audio"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["audio"] = segment.data.at("file");
        }

        // Add optional metadata
        if (segment.data.contains("title")) {
          json["title"] = segment.data.at("title");
        }
        if (segment.data.contains("performer")) {
          json["performer"] = segment.data.at("performer");
        }
        if (segment.data.contains("duration")) {
          json["duration"] = segment.data.at("duration");
        }

        // Add caption if present
        std::string caption;
        for (const auto &caption_segment : message) {
          if (caption_segment.type == "text") {
            caption += caption_segment.data.at("text");
          }
        }

        if (!caption.empty()) {
          json["caption"] = caption;
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("Telegram sendAudio 添加回复消息ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains voice, use sendVoice method
  if (has_voice) {
    // Handle the first voice in the message
    for (const auto &segment : message) {
      if (segment.type == "voice") {
        nlohmann::json json;
        json["method"] = "sendVoice";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different voice sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram voice
          json["voice"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending voice from URL
          json["voice"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["voice"] = segment.data.at("file");
        }

        // Add optional metadata
        if (segment.data.contains("duration")) {
          json["duration"] = segment.data.at("duration");
        }

        // Add caption if present
        std::string caption;
        for (const auto &caption_segment : message) {
          if (caption_segment.type == "text") {
            caption += caption_segment.data.at("text");
          }
        }

        if (!caption.empty()) {
          json["caption"] = caption;
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("Telegram sendVoice 添加回复消息ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // If message contains document, use sendDocument method
  if (has_document) {
    // Handle the first document in the message
    for (const auto &segment : message) {
      if (segment.type == "document") {
        nlohmann::json json;
        json["method"] = "sendDocument";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        // Handle different document sources
        if (segment.data.contains("file_id")) {
          // Forwarding existing Telegram document
          json["document"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          // Sending document from URL
          json["document"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Sending local file - would need multipart/form-data handling
          json["document"] = segment.data.at("file");
        }

        // Add caption if present
        std::string caption;
        for (const auto &caption_segment : message) {
          if (caption_segment.type == "text") {
            caption += caption_segment.data.at("text");
          }
        }

        if (!caption.empty()) {
          json["caption"] = caption;
        }

        // Add reply_to_message_id if present
        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("Telegram sendDocument 添加回复消息ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // Default to text message
  nlohmann::json json;
  json["method"] = "sendMessage";
  json["chat_id"] = target_id;
  if (topic_id.has_value()) {
    json["message_thread_id"] = topic_id.value();
  }

  // Convert internal message format to Telegram format
  std::string text;
  for (const auto &segment : message) {
    if (segment.type == "text") {
      text += segment.data.at("text");
    }
    // For other segment types, we would need to handle them appropriately
    // For example, images might need to be sent as separate messages
  }

  json["text"] = text;

  // Add reply_to_message_id if present
  if (reply_to_message_id.has_value()) {
    json["reply_to_message_id"] = reply_to_message_id.value();
    OBCX_DEBUG("Telegram sendMessage 添加回复消息ID: {}",
               reply_to_message_id.value());
  }

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_delete_message_request(
    std::string_view chat_id, std::string_view message_id,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "deleteMessage";
  json["chat_id"] = chat_id;
  json["message_id"] = message_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_get_self_info_request(
    const std::optional<uint64_t> &echo) -> std::string {
  return serialize_get_me_request(echo);
}

auto ProtocolAdapter::serialize_get_user_info_request(
    std::string_view chat_id, std::string_view user_id, bool no_cache,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we ignore chat_id and no_cache parameters
  return serialize_get_chat_member_request(chat_id, user_id, echo);
}

auto ProtocolAdapter::serialize_get_chat_info_request(
    std::string_view chat_id, bool no_cache,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we ignore no_cache parameter
  return serialize_get_chat_request(chat_id, echo);
}

auto ProtocolAdapter::serialize_get_chat_member_info_request(
    std::string_view chat_id, std::string_view user_id, bool no_cache,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we ignore no_cache parameter
  return serialize_get_chat_member_request(chat_id, user_id, echo);
}

auto ProtocolAdapter::serialize_get_chat_admins_request(
    std::string_view chat_id, const std::optional<uint64_t> &echo)
    -> std::string {
  return serialize_get_chat_administrators_request(chat_id, echo);
}

auto ProtocolAdapter::serialize_kick_chat_member_request(
    std::string_view chat_id, std::string_view user_id, bool reject_add_request,
    bool revoke_messages, const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we ignore reject_add_request parameter
  nlohmann::json json;
  json["method"] = "banChatMember";
  json["chat_id"] = chat_id;
  json["user_id"] = user_id;
  json["revoke_messages"] = revoke_messages;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_ban_chat_member_request(
    std::string_view chat_id, std::string_view user_id, int32_t duration,
    const std::optional<uint64_t> &echo) -> std::string {
  // Convert duration to until_date timestamp
  auto until_date = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count() +
                    duration;
  return serialize_restrict_chat_member_request(chat_id, user_id, until_date,
                                                echo);
}

auto ProtocolAdapter::serialize_unban_chat_member_request(
    std::string_view chat_id, std::string_view user_id,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, unban is done by restricting with default permissions
  nlohmann::json json;
  json["method"] = "restrictChatMember";
  json["chat_id"] = chat_id;
  json["user_id"] = user_id;

  // Set default permissions (unrestricted)
  nlohmann::json permissions;
  permissions["can_send_messages"] = true;
  permissions["can_send_media_messages"] = true;
  permissions["can_send_polls"] = true;
  permissions["can_send_other_messages"] = true;
  permissions["can_add_web_page_previews"] = true;
  permissions["can_change_info"] = true;
  permissions["can_invite_users"] = true;
  permissions["can_pin_messages"] = true;

  json["permissions"] = permissions;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_ban_all_members_request(
    std::string_view chat_id, bool enable, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json json;
  json["method"] = "setChatPermissions";
  json["chat_id"] = chat_id;

  if (enable) {
    // Disable all permissions for everyone
    nlohmann::json permissions;
    permissions["can_send_messages"] = false;
    permissions["can_send_media_messages"] = false;
    permissions["can_send_polls"] = false;
    permissions["can_send_other_messages"] = false;
    permissions["can_add_web_page_previews"] = false;
    permissions["can_change_info"] = false;
    permissions["can_invite_users"] = false;
    permissions["can_pin_messages"] = false;

    json["permissions"] = permissions;
  } else {
    // Enable default permissions
    nlohmann::json permissions;
    permissions["can_send_messages"] = true;
    permissions["can_send_media_messages"] = true;
    permissions["can_send_polls"] = true;
    permissions["can_send_other_messages"] = true;
    permissions["can_add_web_page_previews"] = true;
    permissions["can_change_info"] = true;
    permissions["can_invite_users"] = true;
    permissions["can_pin_messages"] = true;

    json["permissions"] = permissions;
  }

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_set_chat_title_request(
    std::string_view chat_id, std::string_view title,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "setChatTitle";
  json["chat_id"] = chat_id;
  json["title"] = title;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_set_chat_photo_request(
    std::string_view chat_id, std::string_view file, bool cache,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we ignore cache parameter
  nlohmann::json json;
  json["method"] = "setChatPhoto";
  json["chat_id"] = chat_id;
  json["photo"] = file;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_set_chat_admin_request(
    std::string_view chat_id, std::string_view user_id, bool is_admin,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "promoteChatMember";
  json["chat_id"] = chat_id;
  json["user_id"] = user_id;

  // Set admin permissions
  json["can_change_info"] = is_admin;
  json["can_delete_messages"] = is_admin;
  json["can_invite_users"] = is_admin;
  json["can_restrict_members"] = is_admin;
  json["can_pin_messages"] = is_admin;
  json["can_promote_members"] = is_admin;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_leave_chat_request(
    std::string_view chat_id, bool is_dismiss,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we ignore is_dismiss parameter
  return serialize_leave_chat_by_id_request(chat_id, echo);
}

auto ProtocolAdapter::serialize_handle_join_request(
    const common::RequestEvent &request_event, bool approve,
    std::string_view reason, std::string_view remark,
    const std::optional<uint64_t> &echo) -> std::string {
  // For Telegram, we handle chat join requests
  nlohmann::json json;
  if (approve) {
    json["method"] = "approveChatJoinRequest";
  } else {
    json["method"] = "declineChatJoinRequest";
  }

  // Extract chat_id and user_id from request_event
  // This is a simplified implementation - in reality, you'd need to parse the
  // event properly Since RequestEvent doesn't have chat_id, we'll use a
  // placeholder
  json["chat_id"] =
      ""; // Placeholder - should be extracted from the actual event
  json["user_id"] = request_event.user_id;

  if (!approve && !reason.empty()) {
    // For decline, we could add a note, but Telegram API doesn't support it
    // directly
  }

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_download_file_request(
    std::string_view file_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json json;
  json["method"] = "getFile";
  json["file_id"] = file_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

// --- Telegram 特有接口 ---
auto ProtocolAdapter::serialize_get_user_info_by_id_request(
    std::string_view user_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json json;
  json["method"] = "getUser";
  json["user_id"] = user_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_get_chat_request(
    std::string_view chat_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json json;
  json["method"] = "getChat";
  json["chat_id"] = chat_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_get_chat_administrators_request(
    std::string_view chat_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json json;
  json["method"] = "getChatAdministrators";
  json["chat_id"] = chat_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_get_chat_member_request(
    std::string_view chat_id, std::string_view user_id,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "getChatMember";
  json["chat_id"] = chat_id;
  json["user_id"] = user_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_kick_chat_member_by_id_request(
    std::string_view chat_id, std::string_view user_id,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "kickChatMember";
  json["chat_id"] = chat_id;
  json["user_id"] = user_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_restrict_chat_member_request(
    std::string_view chat_id, std::string_view user_id, int64_t until_date,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "restrictChatMember";
  json["chat_id"] = chat_id;
  json["user_id"] = user_id;
  json["until_date"] = until_date;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_leave_chat_by_id_request(
    std::string_view chat_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json json;
  json["method"] = "leaveChat";
  json["chat_id"] = chat_id;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_get_me_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "getMe";

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_get_updates_request(
    int offset, int limit, const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "getUpdates";
  json["offset"] = offset;
  json["limit"] = limit;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

} // namespace obcx::adapter::telegram