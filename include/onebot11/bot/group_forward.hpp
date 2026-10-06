#pragma once

#include "core/bot/messaging.hpp"
#include "core/bot/operation_traits.hpp"
#include "onebot11/bot/types.hpp"

#include <cstdint>
#include <string_view>

namespace obcx::onebot11::bot {

// General send_group_forward_msg transport contract. Node content, sender
// fields and provider extensions are preserved; application ordering, limits,
// file ownership and staging policies belong to the calling actor.
struct SendOneBotGroupForwardMessageRequest {
  using obcx_bot_json_factory = void;
  inline static const obcx::bot::ActionId action{"onebot11.group_forward.send"};
  inline static constexpr std::string_view wire_action =
      "send_group_forward_msg";

  GroupTarget target;
  Json messages;

  [[nodiscard]] auto wire_document(std::uint64_t echo) const -> Json {
    return {{"action", wire_action},
            {"params",
             {{"group_id", target.native_group_id}, {"messages", messages}}},
            {"echo", echo}};
  }

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "group forward");
    if (!messages.is_array() || messages.empty()) {
      throw std::invalid_argument(
          "Group forward requires a nonempty node array");
    }
    for (const auto &node : messages) {
      detail::require_object(node, "forward node");
      if (node.at("type") != "node") {
        throw std::invalid_argument("Group forward requires node type");
      }
      const auto &data = node.at("data");
      detail::require_object(data, "forward node data");
      if (data.contains("id")) {
        const auto &id = data.at("id");
        if (id.is_string()) {
          detail::validate_identifier(id.get_ref<const std::string &>(),
                                      "forward message ID");
        } else if (!id.is_number_integer()) {
          throw std::invalid_argument(
              "Forward message ID must be a string or integer");
        }
      } else {
        const auto &content = data.at("content");
        // OneBot content can be a CQ string or an ordinary segment array.
        // Reuse common structural validation without a segment-type allowlist.
        if (!content.is_string()) {
          (void)obcx::bot::detail::require_message(data, "content",
                                                   "forward node");
        }
      }
    }
  }

  static auto from_json(const Json &document)
      -> SendOneBotGroupForwardMessageRequest {
    obcx::bot::detail::require_only_keys(document, "group forward request",
                                         {"action", "target", "messages"});
    if (document.contains("action") &&
        document.at("action").get<obcx::bot::ActionId>() != action) {
      throw std::invalid_argument("Group forward action mismatch");
    }
    SendOneBotGroupForwardMessageRequest result{
        .target = document.at("target").get<GroupTarget>(),
        .messages = document.at("messages")};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document,
                    const SendOneBotGroupForwardMessageRequest &value) {
  value.validate();
  document = {{"action", value.action},
              {"target", value.target},
              {"messages", value.messages}};
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
