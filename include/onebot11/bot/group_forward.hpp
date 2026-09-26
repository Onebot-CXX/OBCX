#pragma once

#include "core/bot/operation_traits.hpp"
#include "onebot11/bot/types.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>
#include <vector>

namespace obcx::onebot11::bot {

// Existing send_group_forward_msg, restricted to adjacent image/plain-text
// nodes. The supported provider supplies its authenticated bot identity when
// sender fields are omitted. Callers cannot impersonate another sender.
struct SendOneBotGroupForwardMessageRequest {
  using obcx_bot_json_factory = void;
  inline static const obcx::bot::ActionId action{"onebot11.group_forward.send"};
  inline static constexpr std::string_view wire_action =
      "send_group_forward_msg";

  GroupTarget target;
  Json messages;
  std::uint64_t maximum_payload_bytes;
  // Explicit request-local references supplied by the file owner. Not a
  // filesystem capability or proof of peer readability; never sent on wire.
  std::vector<std::string> shared_files;

  [[nodiscard]] static auto valid_shared_file_uri(std::string_view uri)
      -> bool {
    if (!uri.starts_with("file:///") || uri.size() <= 8) {
      return false;
    }
    const auto unreserved = [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
             c == '~';
    };
    const auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      return -1;
    };
    std::string path;
    for (std::size_t i = 7; i < uri.size(); ++i) {
      auto c = static_cast<unsigned char>(uri[i]);
      if (c == '%') {
        if (i + 2 >= uri.size())
          return false;
        const auto high = hex(uri[i + 1]);
        const auto low = hex(uri[i + 2]);
        if (high < 0 || low < 0)
          return false;
        c = static_cast<unsigned char>((high << 4) | low);
        if (unreserved(c) || c == '/' || c == '\\' || c < 0x20 || c == 0x7f) {
          return false;
        }
        i += 2;
      } else if (c != '/' && !unreserved(c)) {
        return false;
      }
      path += static_cast<char>(c);
    }
    for (std::size_t begin = 1; begin <= path.size();) {
      const auto end = path.find('/', begin);
      const auto component = std::string_view(path).substr(
          begin, end == std::string::npos ? end : end - begin);
      if (component.empty() || component == "." || component == "..")
        return false;
      if (end == std::string::npos)
        break;
      begin = end + 1;
    }
    try {
      (void)Json(path)
          .dump(); // Strict UTF-8 validation, without touching disk.
    } catch (const Json::exception &) {
      return false;
    }
    return true;
  }

  [[nodiscard]] auto wire_document(std::uint64_t echo) const -> Json {
    return {{"action", wire_action},
            {"params",
             {{"group_id", target.native_group_id}, {"messages", messages}}},
            {"echo", echo}};
  }

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "group forward");
    if (target.native_group_id.size() > 20 ||
        target.native_group_id.front() == '0' ||
        !std::ranges::all_of(target.native_group_id,
                             [](char c) { return c >= '0' && c <= '9'; }) ||
        maximum_payload_bytes == 0 || !messages.is_array() ||
        messages.empty() || messages.size() > 99) {
      throw std::invalid_argument("Invalid group forward target or bounds");
    }
    const auto only_keys = [](const Json &value,
                              std::initializer_list<std::string_view> keys) {
      if (!value.is_object()) {
        throw std::invalid_argument("Group forward requires objects");
      }
      for (const auto &[key, unused] : value.items()) {
        if (std::ranges::find(keys, key) == keys.end()) {
          throw std::invalid_argument("Unsupported group forward field");
        }
      }
    };
    if (shared_files.size() > 49) {
      throw std::invalid_argument("Too many shared forward files");
    }
    std::set<std::string_view> registered;
    for (const auto &uri : shared_files) {
      if (uri.size() > maximum_payload_bytes || !valid_shared_file_uri(uri) ||
          !registered.insert(uri).second) {
        throw std::invalid_argument("Invalid shared forward file registration");
      }
    }
    std::set<std::string_view> used;
    bool needs_caption = false;
    for (const auto &node : messages) {
      only_keys(node, {"type", "data"});
      if (node.at("type") != "node") {
        throw std::invalid_argument("Group forward requires node type");
      }
      const auto &data = node.at("data");
      only_keys(data, {"content"});
      const auto &content = data.at("content");
      if (!content.is_array() || content.size() != 1) {
        throw std::invalid_argument("Group forward requires single segments");
      }
      const auto &segment = content[0];
      only_keys(segment, {"type", "data"});
      const auto &body = segment.at("data");
      if (segment.at("type") == "image") {
        only_keys(body, {"file"});
        const auto &file = body.at("file").get_ref<const std::string &>();
        if (needs_caption) {
          throw std::invalid_argument("Invalid group forward image");
        }
        if (file.starts_with("file://")) {
          if (!registered.contains(file)) {
            throw std::invalid_argument("Unregistered shared forward file");
          }
          used.insert(file);
          needs_caption = true;
          continue;
        }
        if (!file.starts_with("base64://")) {
          throw std::invalid_argument("Invalid group forward image");
        }
        const std::string_view encoded{file.data() + 9, file.size() - 9};
        const auto padding = encoded.ends_with("==")  ? 2U
                             : encoded.ends_with("=") ? 1U
                                                      : 0U;
        if (encoded.empty() || encoded.size() % 4 != 0 ||
            !std::ranges::all_of(
                encoded.substr(0, encoded.size() - padding), [](char c) {
                  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '+' || c == '/';
                })) {
          throw std::invalid_argument("Invalid group forward base64");
        }
        needs_caption = true;
      } else if (segment.at("type") == "text") {
        only_keys(body, {"text"});
        if (body.at("text").get_ref<const std::string &>().empty()) {
          throw std::invalid_argument("Empty group forward description");
        }
        needs_caption = false;
      } else {
        throw std::invalid_argument("Unsupported group forward segment");
      }
    }
    if (needs_caption || used.size() != registered.size() ||
        wire_document(std::numeric_limits<std::uint64_t>::max()).dump().size() >
            maximum_payload_bytes) {
      throw std::invalid_argument("Unpaired or oversized group forward");
    }
  }

  static auto from_json(const Json &document)
      -> SendOneBotGroupForwardMessageRequest {
    detail::require_object(document, "group forward request");
    if (document.contains("action") &&
        document.at("action").get<obcx::bot::ActionId>() != action) {
      throw std::invalid_argument("Group forward action mismatch");
    }
    const auto &bytes = document.at("maximum_payload_bytes");
    if (!bytes.is_number_integer() || bytes <= 0) {
      throw std::invalid_argument("Group forward requires explicit byte bound");
    }
    SendOneBotGroupForwardMessageRequest result{
        .target = document.at("target").get<GroupTarget>(),
        .messages = document.at("messages"),
        .maximum_payload_bytes = bytes.get<std::uint64_t>(),
        .shared_files =
            document.at("shared_files").get<std::vector<std::string>>()};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document,
                    const SendOneBotGroupForwardMessageRequest &value) {
  value.validate();
  document = {{"action", value.action},
              {"target", value.target},
              {"messages", value.messages},
              {"maximum_payload_bytes", value.maximum_payload_bytes},
              {"shared_files", value.shared_files}};
}

struct OneBotGroupForwardMessageResult {
  using obcx_bot_json_factory = void;
  GroupTarget target;
  std::string message_id;
  std::string forward_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "group forward result");
    detail::validate_identifier(message_id, "message_id");
    detail::validate_identifier(forward_id, "forward_id");
  }
  static auto from_json(const Json &document)
      -> OneBotGroupForwardMessageResult {
    OneBotGroupForwardMessageResult result{
        .target = document.at("target").get<GroupTarget>(),
        .message_id = document.at("message_id").get<std::string>(),
        .forward_id = document.at("forward_id").get<std::string>()};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document,
                    const OneBotGroupForwardMessageResult &value) {
  value.validate();
  document = {{"target", value.target},
              {"message_id", value.message_id},
              {"forward_id", value.forward_id}};
}

} // namespace obcx::onebot11::bot

namespace obcx::bot {
template <>
struct OperationTraits<onebot11::bot::SendOneBotGroupForwardMessageRequest>
    : OperationContract<onebot11::bot::SendOneBotGroupForwardMessageRequest,
                        onebot11::bot::OneBotGroupForwardMessageResult, true> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (request.target != result.target) {
      throw std::invalid_argument("Group forward result target mismatch");
    }
  }
};
} // namespace obcx::bot
