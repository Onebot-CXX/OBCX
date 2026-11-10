#include "telegram/adapter/protocol_adapter.hpp"
#include "common/json_utils.hpp"
#include "common/logger.hpp"

#include <nlohmann/json.hpp>

namespace obcx::adapter::telegram {

namespace {

struct FormattedTelegramText {
  std::string text;
  std::vector<core::TelegramTextEntity> entities;
};

auto format_telegram_text(const common::Message &message)
    -> FormattedTelegramText {
  FormattedTelegramText result;
  std::size_t offset = 0;
  for (const auto &segment : message) {
    if (segment.type != "text" || !segment.data.contains("text") ||
        !segment.data["text"].is_string()) {
      continue;
    }
    const auto text = segment.data["text"].get<std::string>();
    const auto length = core::telegram_utf16_code_units(text);
    if (length != 0 &&
        segment.data.value("telegram_style", std::string{}) == "italic") {
      result.entities.push_back(core::TelegramTextEntity{
          .type = "italic", .offset = offset, .length = length});
    }
    result.text += text;
    offset += length;
  }
  return result;
}

void apply_telegram_caption(nlohmann::json &json,
                            const FormattedTelegramText &caption) {
  if (caption.text.empty()) {
    return;
  }
  json["caption"] = caption.text;
  if (!caption.entities.empty()) {
    json["caption_entities"] = caption.entities;
  }
}

} // namespace

auto ProtocolAdapter::parse_event(std::string_view json_str)
    -> std::optional<common::Event> {
  try {
    auto json = nlohmann::json::parse(json_str);
    OBCX_DEBUG("Parsing Telegram update ({} bytes)", json_str.size());

    if (json.contains("update_id")) {
      if (json.contains("message")) {
        return parse_message_event(json);
      }
      if (json.contains("edited_message")) {
        return parse_edited_message_event(json);
      }
      if (json.contains("channel_post")) {
        return parse_channel_post_event(json);
      }
      if (json.contains("edited_channel_post")) {
        return parse_edited_channel_post_event(json);
      }
      if (json.contains("callback_query")) {
        return parse_callback_query_event(json);
      }
      OBCX_DEBUG("Unhandled update type in update");
      return std::nullopt;
    }
    OBCX_DEBUG("No update_id field in JSON");

    return std::nullopt;
  } catch (const std::exception &) {
    // Parser diagnostics may quote the input. Neither raw updates nor native
    // exception text belong in logs: either can carry sensitive locators.
    OBCX_ERROR("Failed to parse Telegram update");
    return std::nullopt;
  }
}

auto ProtocolAdapter::parse_message_event(const nlohmann::json &update_json)
    -> std::optional<common::Event> {
  try {
    auto message = update_json["message"];

    common::MessageEvent event{};
    event.time = std::chrono::system_clock::now();
    event.post_type = "message";
    event.type = common::EventType::message;
    // Bot API updates do not carry the receiving bot identity. The platform
    // adapter deliberately does not admit this legacy placeholder as a user.
    event.self_id = "0";

    // Keep the raw payload around for handlers that need fields beyond the
    // canonical extracted set (entities, reply chain, etc.).
    event.data = message;

    if (update_json.contains("update_id")) {
      // update_id is intentionally not stored on the event; if needed it can be
      // recovered from event.data.
    }

    if (message.contains("message_id")) {
      event.message_id = std::to_string(message["message_id"].get<int64_t>());
      OBCX_DEBUG("Extracted message_id: {}", event.message_id);
    }

    if (message.contains("from")) {
      auto from = message["from"];
      if (from.contains("id")) {
        event.user_id = std::to_string(from["id"].get<int64_t>());
        OBCX_DEBUG("Extracted user_id: {}", event.user_id);
      }
    }

    if (message.contains("chat")) {
      auto chat = message["chat"];
      if (chat.contains("id")) {
        std::string chat_id = std::to_string(chat["id"].get<int64_t>());
        OBCX_DEBUG("Extracted chat_id: {}", chat_id);

        if (chat.contains("type")) {
          std::string chat_type = chat["type"];
          OBCX_DEBUG("Extracted Telegram chat type");

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

    if (message.contains("text")) {
      event.raw_message = message["text"];
      OBCX_DEBUG("Extracted Telegram text ({} bytes)",
                 event.raw_message.size());

      common::MessageSegment segment;
      segment.type = "text";
      segment.data["text"] = message["text"];
      event.message.push_back(segment);
    } else if (message.contains("photo")) {
      auto photos = message["photo"];
      if (!photos.empty()) {
        // Telegram returns multiple sizes sorted ascending; the last entry is
        // the largest (highest-resolution) variant.
        auto photo = photos.back();
        std::string file_id = photo["file_id"];

        event.raw_message = std::string("[Photo]");
        OBCX_DEBUG("Extracted Telegram photo attachment");

        common::MessageSegment segment;
        segment.type = "image";
        segment.data["file_id"] = file_id;
        if (message.contains("caption")) {
          segment.data["caption"] = message["caption"];
          event.raw_message += message["caption"];
        }
        event.message.push_back(segment);
      }
    } else if (message.contains("sticker")) {
      auto sticker = message["sticker"];
      std::string file_id = sticker["file_id"];

      event.raw_message = std::string("[Sticker]");
      OBCX_DEBUG("Extracted Telegram sticker attachment");

      common::MessageSegment segment;

      segment.type = "sticker";
      segment.data["file_id"] = file_id;
      segment.data["is_sticker"] = true;
      segment.data["file_unique_id"] =
          sticker["file_unique_id"].get<std::string>();
      segment.data["is_animated"] = sticker["is_animated"].get<bool>();
      segment.data["is_video"] = sticker["is_video"].get<bool>();
      if (sticker.contains("emoji")) {
        segment.data["emoji"] = sticker["emoji"];
        event.raw_message =
            fmt::format("[{} Sticker]", sticker["emoji"].get<std::string>());
      }
      event.message.push_back(segment);
    } else if (message.contains("video")) {
      auto video = message["video"];
      std::string file_id = video["file_id"];

      event.raw_message = std::string("[Video]");
      OBCX_DEBUG("Extracted Telegram video attachment");

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
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("animation")) {
      auto animation = message["animation"];
      std::string file_id = animation["file_id"];

      event.raw_message = std::string("[Animation]");
      OBCX_DEBUG("Extracted Telegram animation attachment");

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
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("document")) {
      auto document = message["document"];
      std::string file_id = document["file_id"];

      event.raw_message = std::string("[Document]");
      OBCX_DEBUG("Extracted Telegram document attachment");

      common::MessageSegment segment;
      segment.type = "document";
      segment.data["file_id"] = file_id;
      if (document.contains("file_unique_id")) {
        segment.data["file_unique_id"] = document["file_unique_id"];
      }
      if (document.contains("file_name")) {
        segment.data["file_name"] = document["file_name"];
        event.raw_message = fmt::format(
            "[Document: {}]", document["file_name"].get<std::string>());
      }
      if (document.contains("mime_type")) {
        segment.data["mime_type"] = document["mime_type"];
      }
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("audio")) {
      auto audio = message["audio"];
      std::string file_id = audio["file_id"];

      event.raw_message = std::string("[Audio]");
      OBCX_DEBUG("Extracted Telegram audio attachment");

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
        event.raw_message =
            fmt::format("[Audio: {}]", audio["title"].get<std::string>());
      }
      if (message.contains("caption")) {
        segment.data["caption"] = message["caption"];
        event.raw_message += ": " + message["caption"].get<std::string>();
      }
      event.message.push_back(segment);
    } else if (message.contains("voice")) {
      auto voice = message["voice"];
      std::string file_id = voice["file_id"];

      event.raw_message = std::string("[Voice]");
      OBCX_DEBUG("Extracted Telegram voice attachment");

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
      auto video_note = message["video_note"];
      std::string file_id = video_note["file_id"];

      event.raw_message = std::string("[Video Note]");
      OBCX_DEBUG("Extracted Telegram video-note attachment");

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

    OBCX_DEBUG("Successfully parsed event");
    return event;
  } catch (const std::exception &) {
    OBCX_ERROR("Failed to parse Telegram message");
    return std::nullopt;
  }
}

auto ProtocolAdapter::parse_edited_message_event(
    const nlohmann::json &update_json) -> std::optional<common::Event> {
  if (update_json.contains("edited_message")) {
    auto update_copy = update_json;
    update_copy["message"] = update_copy["edited_message"];
    update_copy.erase("edited_message");

    auto event_opt = parse_message_event(update_copy);
    if (event_opt.has_value()) {
      if (auto *msg_event =
              std::get_if<common::MessageEvent>(&event_opt.value())) {
        msg_event->data["is_edited"] = true;
        msg_event->sub_type = "edited";
        OBCX_DEBUG("Marked as edit message: message_id={}",
                   msg_event->message_id);
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
  // Preserve the observed update kind even if edit_date is unavailable. The
  // ordinary edit parser adds the marker without inventing an edit timestamp.
  if (update_json.contains("edited_channel_post")) {
    auto update_copy = update_json;
    update_copy["edited_message"] = update_copy["edited_channel_post"];
    update_copy.erase("edited_channel_post");
    return parse_edited_message_event(update_copy);
  }
  return std::nullopt;
}

auto ProtocolAdapter::parse_callback_query_event(
    const nlohmann::json &update_json) -> std::optional<common::Event> {
  // TODO: callback_query is currently surfaced as a generic notice; a richer
  // CallbackQueryEvent could be modelled later.
  try {
    if (update_json.contains("callback_query")) {
      auto callback_query = update_json["callback_query"];

      common::NoticeEvent event{};
      event.time = std::chrono::system_clock::now();
      event.post_type = "notice";
      event.type = common::EventType::notice;
      // Bot API updates do not carry the receiving bot identity. The platform
      // adapter deliberately does not admit this legacy placeholder as a user.
      event.self_id = "0";
      event.notice_type = "callback_query";

      if (callback_query.contains("from") &&
          callback_query["from"].contains("id")) {
        event.user_id =
            std::to_string(callback_query["from"]["id"].get<int64_t>());
      }

      if (callback_query.contains("message") &&
          callback_query["message"].contains("chat") &&
          callback_query["message"]["chat"].contains("id")) {
        event.group_id = std::to_string(
            callback_query["message"]["chat"]["id"].get<int64_t>());
      }

      OBCX_DEBUG("Successfully parsed callback query event");
      return event;
    }
  } catch (const std::exception &) {
    OBCX_ERROR("Failed to parse Telegram callback query");
  }

  return std::nullopt;
}

auto ProtocolAdapter::serialize_send_message_request(
    std::string_view target_id, const common::Message &message,
    const std::optional<uint64_t> &echo,
    const std::optional<uint8_t> &message_type) -> std::string {
  // Telegram sendMessage works for all chat types (private/group/supergroup/
  // channel); message_type is kept only for cross-protocol interface parity.
  (void)message_type;
  return serialize_send_topic_message_request(target_id, message, echo,
                                              std::nullopt);
}

auto ProtocolAdapter::serialize_send_topic_message_request(
    std::string_view target_id, const common::Message &message,
    const std::optional<uint64_t> &echo, const std::optional<int64_t> &topic_id)
    -> std::string {
  bool has_image = false;
  bool has_animation = false;
  bool has_video = false;
  bool has_audio = false;
  bool has_voice = false;
  bool has_document = false;
  bool has_sticker = false;
  bool has_video_note = false;

  // Media-type priority (first match wins per outgoing message):
  //   sticker > animation > video > image > video_note > audio > voice
  //   > document
  // Mixing media kinds in one Telegram send is not supported, so we pick a
  // single bearer method below and fold any text segments in as the caption.
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

  std::optional<std::string> reply_to_message_id;
  for (const auto &segment : message) {
    if (segment.type == "reply") {
      reply_to_message_id =
          common::JsonUtils::get_optional_id_as_string(segment.data, "id");
      if (reply_to_message_id.has_value()) {
        break;
      }
    }
  }

  if (has_sticker) {
    for (const auto &segment : message) {
      if (segment.type == "sticker") {
        nlohmann::json json;
        json["method"] = "sendSticker";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["sticker"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["sticker"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Local file path: caller is expected to upload via multipart/
          // form-data; here we pass the path through verbatim.
          json["sticker"] = segment.data.at("file");
        }

        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("sendSticker added reply message ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  if (has_animation) {
    for (const auto &segment : message) {
      if (segment.type == "animation") {
        nlohmann::json json;
        json["method"] = "sendAnimation";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["animation"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["animation"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          json["animation"] = segment.data.at("file");
        }

        apply_telegram_caption(json, format_telegram_text(message));

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

  if (has_video) {
    for (const auto &segment : message) {
      if (segment.type == "video") {
        nlohmann::json json;
        json["method"] = "sendVideo";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["video"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["video"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          json["video"] = segment.data.at("file");
        }

        apply_telegram_caption(json, format_telegram_text(message));

        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("sendVideo added reply message ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  if (has_video_note) {
    for (const auto &segment : message) {
      if (segment.type == "video_note") {
        nlohmann::json json;
        json["method"] = "sendVideoNote";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["video_note"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["video_note"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          json["video_note"] = segment.data.at("file");
        }

        if (segment.data.contains("length")) {
          json["length"] = segment.data.at("length");
        }
        if (segment.data.contains("duration")) {
          json["duration"] = segment.data.at("duration");
        }

        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("sendVideoNote added reply message ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  if (has_image) {
    // TODO: handle multiple images per message via sendMediaGroup; current
    // path picks only the first image segment.
    for (const auto &segment : message) {
      if (segment.type == "image") {
        nlohmann::json json;
        json["method"] = "sendPhoto";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          // Forwarding an existing Telegram photo by file_id
          json["photo"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["photo"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          // Local file path: caller must upload via multipart/form-data.
          json["photo"] = segment.data.at("file");
        }

        apply_telegram_caption(json, format_telegram_text(message));

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

  if (has_audio) {
    for (const auto &segment : message) {
      if (segment.type == "audio") {
        nlohmann::json json;
        json["method"] = "sendAudio";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["audio"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["audio"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          json["audio"] = segment.data.at("file");
        }

        if (segment.data.contains("title")) {
          json["title"] = segment.data.at("title");
        }
        if (segment.data.contains("performer")) {
          json["performer"] = segment.data.at("performer");
        }
        if (segment.data.contains("duration")) {
          json["duration"] = segment.data.at("duration");
        }

        apply_telegram_caption(json, format_telegram_text(message));

        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("sendAudio added reply message ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  if (has_voice) {
    for (const auto &segment : message) {
      if (segment.type == "voice") {
        nlohmann::json json;
        json["method"] = "sendVoice";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["voice"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["voice"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          json["voice"] = segment.data.at("file");
        }

        if (segment.data.contains("duration")) {
          json["duration"] = segment.data.at("duration");
        }

        apply_telegram_caption(json, format_telegram_text(message));

        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("sendVoice added reply message ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  if (has_document) {
    for (const auto &segment : message) {
      if (segment.type == "document") {
        nlohmann::json json;
        json["method"] = "sendDocument";
        json["chat_id"] = target_id;
        if (topic_id.has_value()) {
          json["message_thread_id"] = topic_id.value();
        }

        if (segment.data.contains("file_id")) {
          json["document"] = segment.data.at("file_id");
        } else if (segment.data.contains("url")) {
          json["document"] = segment.data.at("url");
        } else if (segment.data.contains("file")) {
          json["document"] = segment.data.at("file");
        }

        apply_telegram_caption(json, format_telegram_text(message));

        if (reply_to_message_id.has_value()) {
          json["reply_to_message_id"] = reply_to_message_id.value();
          OBCX_DEBUG("sendDocument added reply message ID: {}",
                     reply_to_message_id.value());
        }

        if (echo.has_value()) {
          json["echo"] = std::to_string(echo.value());
        }

        return json.dump();
      }
    }
  }

  // Default sendMessage path: text-only or text remainder.
  nlohmann::json json;
  json["method"] = "sendMessage";
  json["chat_id"] = target_id;
  if (topic_id.has_value()) {
    json["message_thread_id"] = topic_id.value();
  }

  const auto formatted = format_telegram_text(message);
  json["text"] = formatted.text;
  if (!formatted.entities.empty()) {
    json["entities"] = formatted.entities;
  }

  if (reply_to_message_id.has_value()) {
    json["reply_to_message_id"] = reply_to_message_id.value();
    OBCX_DEBUG("Added reply message ID: {}", reply_to_message_id.value());
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
  nlohmann::json json;
  if (approve) {
    json["method"] = "approveChatJoinRequest";
  } else {
    json["method"] = "declineChatJoinRequest";
  }

  // FIXME: RequestEvent does not currently carry chat_id; populate once the
  // event model exposes it. Sending an empty chat_id will fail at Telegram.
  json["chat_id"] = "";
  json["user_id"] = request_event.user_id;

  if (!approve && !reason.empty()) {
    // Telegram's declineChatJoinRequest does not accept a reason field, so
    // `reason` is intentionally dropped here.
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

auto ProtocolAdapter::serialize_send_media_group_request(
    std::string_view chat_id,
    const std::vector<std::pair<std::string, std::string>> &media,
    std::string_view caption, const std::optional<int64_t> &topic_id,
    const std::optional<std::string> &reply_to_message_id,
    const std::optional<uint64_t> &echo) -> std::string {
  return serialize_send_media_group_request_with_entities(
      chat_id, media, caption, topic_id, reply_to_message_id, echo, {});
}

auto ProtocolAdapter::serialize_send_media_group_request_with_entities(
    std::string_view chat_id,
    const std::vector<std::pair<std::string, std::string>> &media,
    std::string_view caption, const std::optional<int64_t> &topic_id,
    const std::optional<std::string> &reply_to_message_id,
    const std::optional<uint64_t> &echo,
    const std::vector<core::TelegramTextEntity> &caption_entities)
    -> std::string {
  nlohmann::json json;
  json["method"] = "sendMediaGroup";
  json["chat_id"] = chat_id;

  if (topic_id.has_value()) {
    json["message_thread_id"] = topic_id.value();
  }

  if (reply_to_message_id.has_value()) {
    json["reply_to_message_id"] = reply_to_message_id.value();
  }

  // Telegram requires the caption on the first media item only; subsequent
  // items must omit the caption field entirely.
  nlohmann::json media_array = nlohmann::json::array();
  bool first = true;
  for (const auto &[type, url] : media) {
    nlohmann::json media_item;
    media_item["type"] = type;
    media_item["media"] = url;

    if (first && !caption.empty()) {
      media_item["caption"] = caption;
      if (!caption_entities.empty()) {
        media_item["caption_entities"] = caption_entities;
      }
      first = false;
    } else {
      first = false;
    }

    media_array.push_back(media_item);
  }

  json["media"] = media_array;

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

auto ProtocolAdapter::serialize_edit_message_text_request(
    std::string_view chat_id, std::string_view message_id,
    std::string_view text, std::string_view parse_mode,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json json;
  json["method"] = "editMessageText";
  json["chat_id"] = chat_id;
  json["message_id"] = message_id;
  json["text"] = text;

  if (!parse_mode.empty()) {
    json["parse_mode"] = parse_mode;
  }

  if (echo.has_value()) {
    json["echo"] = std::to_string(echo.value());
  }

  return json.dump();
}

} // namespace obcx::adapter::telegram
