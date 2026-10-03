#ifndef OBCX_INCLUDE_CORE_BOT_MESSAGING_HPP_
#define OBCX_INCLUDE_CORE_BOT_MESSAGING_HPP_

#include "common/message_type.hpp"
#include "core/bot/operation_result.hpp"
#include "core/bot/operation_traits.hpp"
#include "core/bot/references.hpp"

#include <vector>

namespace obcx::bot {

namespace detail {

inline void validate_message(const common::Message &message,
                             const std::string_view field = "message") {
  if (message.empty()) {
    throw std::invalid_argument(std::string{field} + " cannot be empty");
  }
  for (const auto &segment : message) {
    validate_identifier(segment.type, "message segment type", 128);
    if (!segment.data.is_object()) {
      throw std::invalid_argument("message segment data must be an object");
    }
  }
}

inline auto require_message(const Json &document, const std::string_view key,
                            const std::string_view type) -> common::Message {
  const auto field = std::string{key};
  if (!document.contains(field) || !document.at(field).is_array()) {
    throw std::invalid_argument(std::string{type} + " requires array " + field);
  }
  auto message = document.at(field).get<common::Message>();
  validate_message(message, key);
  return message;
}

} // namespace detail

struct SendGroupMessageRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> SendGroupMessageRequest;

  inline static const ActionId action{"message.send_group"};

  GroupTarget target;
  common::Message message;

  void validate() const {
    target.validate();
    detail::validate_message(message);
  }
};

inline void to_json(Json &document, const SendGroupMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"message", request.message}};
}

inline void from_json(const Json &document, SendGroupMessageRequest &request) {
  detail::require_object(document, "SendGroupMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<ActionId>() != request.action) {
    throw std::invalid_argument("SendGroupMessageRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("SendGroupMessageRequest requires target");
  }
  request.target = document.at("target").get<GroupTarget>();
  request.message =
      detail::require_message(document, "message", "SendGroupMessageRequest");
  request.validate();
}

inline auto SendGroupMessageRequest::from_json(const Json &document)
    -> SendGroupMessageRequest {
  detail::require_object(document, "SendGroupMessageRequest");
  if (!document.contains("target")) {
    throw std::invalid_argument("SendGroupMessageRequest requires target");
  }
  SendGroupMessageRequest result{.target =
                                     document.at("target").get<GroupTarget>()};
  obcx::bot::from_json(document, result);
  return result;
}

struct SendPrivateMessageRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> SendPrivateMessageRequest;

  inline static const ActionId action{"message.send_private"};

  PrivateTarget target;
  common::Message message;

  void validate() const {
    target.validate();
    detail::validate_message(message);
  }
};

inline void to_json(Json &document, const SendPrivateMessageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"target", request.target},
              {"message", request.message}};
}

inline void from_json(const Json &document,
                      SendPrivateMessageRequest &request) {
  detail::require_only_keys(document, "SendPrivateMessageRequest",
                            {"action", "target", "message"});
  if (document.contains("action") &&
      document.at("action").get<ActionId>() != request.action) {
    throw std::invalid_argument("SendPrivateMessageRequest action mismatch");
  }
  if (!document.contains("target")) {
    throw std::invalid_argument("SendPrivateMessageRequest requires target");
  }
  request.target = document.at("target").get<PrivateTarget>();
  request.message =
      detail::require_message(document, "message", "SendPrivateMessageRequest");
  request.validate();
}

inline auto SendPrivateMessageRequest::from_json(const Json &document)
    -> SendPrivateMessageRequest {
  detail::require_only_keys(document, "SendPrivateMessageRequest",
                            {"action", "target", "message"});
  if (!document.contains("target")) {
    throw std::invalid_argument("SendPrivateMessageRequest requires target");
  }
  SendPrivateMessageRequest result{
      .target = document.at("target").get<PrivateTarget>()};
  obcx::bot::from_json(document, result);
  return result;
}

struct DeleteMessageRequest {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> DeleteMessageRequest;

  inline static const ActionId action{"message.delete"};

  BotMessageRef message;

  void validate() const { message.validate(); }
};

inline void to_json(Json &document, const DeleteMessageRequest &request) {
  request.validate();
  document = {{"action", request.action}, {"message", request.message}};
}

inline void from_json(const Json &document, DeleteMessageRequest &request) {
  detail::require_object(document, "DeleteMessageRequest");
  if (document.contains("action") &&
      document.at("action").get<ActionId>() != request.action) {
    throw std::invalid_argument("DeleteMessageRequest action mismatch");
  }
  if (!document.contains("message")) {
    throw std::invalid_argument("DeleteMessageRequest requires message");
  }
  request.message = document.at("message").get<BotMessageRef>();
  request.validate();
}

inline auto DeleteMessageRequest::from_json(const Json &document)
    -> DeleteMessageRequest {
  detail::require_object(document, "DeleteMessageRequest");
  if (!document.contains("message")) {
    throw std::invalid_argument("DeleteMessageRequest requires message");
  }
  DeleteMessageRequest result{.message =
                                  document.at("message").get<BotMessageRef>()};
  obcx::bot::from_json(document, result);
  return result;
}

struct SendMessageResult {
  std::vector<BotMessageRef> messages;

  void validate() const {
    if (messages.empty()) {
      throw std::invalid_argument(
          "SendMessageResult requires at least one message");
    }
    for (const auto &message : messages) {
      message.validate();
      if (message.group != messages.front().group) {
        throw std::invalid_argument(
            "SendMessageResult messages must share one target group");
      }
    }
  }

  [[nodiscard]] auto primary() const -> const BotMessageRef & {
    validate();
    return messages.front();
  }

  auto operator==(const SendMessageResult &) const -> bool = default;
};

inline void to_json(Json &document, const SendMessageResult &result) {
  result.validate();
  document = {{"messages", result.messages}};
}

inline void from_json(const Json &document, SendMessageResult &result) {
  detail::require_object(document, "SendMessageResult");
  if (!document.contains("messages") || !document.at("messages").is_array()) {
    throw std::invalid_argument("SendMessageResult requires messages array");
  }
  result.messages = document.at("messages").get<std::vector<BotMessageRef>>();
  result.validate();
}

struct SendPrivateMessageResult {
  std::vector<PrivateMessageRef> messages;

  void validate() const {
    if (messages.empty()) {
      throw std::invalid_argument(
          "SendPrivateMessageResult requires at least one message");
    }
    for (const auto &message : messages) {
      message.validate();
      if (message.target != messages.front().target) {
        throw std::invalid_argument(
            "SendPrivateMessageResult messages must share one private target");
      }
    }
  }

  [[nodiscard]] auto primary() const -> const PrivateMessageRef & {
    validate();
    return messages.front();
  }

  auto operator==(const SendPrivateMessageResult &) const -> bool = default;
};

inline void to_json(Json &document, const SendPrivateMessageResult &result) {
  result.validate();
  document = {{"messages", result.messages}};
}

inline void from_json(const Json &document, SendPrivateMessageResult &result) {
  detail::require_only_keys(document, "SendPrivateMessageResult", {"messages"});
  if (!document.contains("messages") || !document.at("messages").is_array()) {
    throw std::invalid_argument(
        "SendPrivateMessageResult requires messages array");
  }
  result.messages =
      document.at("messages").get<std::vector<PrivateMessageRef>>();
  result.validate();
}

struct DeleteMessageResult {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> DeleteMessageResult;

  BotMessageRef message;

  void validate() const { message.validate(); }

  auto operator==(const DeleteMessageResult &) const -> bool = default;
};

inline void to_json(Json &document, const DeleteMessageResult &result) {
  result.validate();
  document = {{"message", result.message}};
}

inline void from_json(const Json &document, DeleteMessageResult &result) {
  detail::require_object(document, "DeleteMessageResult");
  if (!document.contains("message")) {
    throw std::invalid_argument("DeleteMessageResult requires message");
  }
  result.message = document.at("message").get<BotMessageRef>();
  result.validate();
}

inline auto DeleteMessageResult::from_json(const Json &document)
    -> DeleteMessageResult {
  detail::require_object(document, "DeleteMessageResult");
  if (!document.contains("message")) {
    throw std::invalid_argument("DeleteMessageResult requires message");
  }
  DeleteMessageResult result{.message =
                                 document.at("message").get<BotMessageRef>()};
  obcx::bot::from_json(document, result);
  return result;
}

template <>
struct OperationTraits<SendGroupMessageRequest>
    : OperationContract<SendGroupMessageRequest, SendMessageResult, true> {
  static auto supports_surface(const SurfaceId &) -> bool { return true; }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != 1U ||
        result.primary().group != request.target) {
      throw std::invalid_argument(
          "group-send result does not match its requested scope");
    }
  }
};

template <>
struct OperationTraits<SendPrivateMessageRequest>
    : OperationContract<SendPrivateMessageRequest, SendPrivateMessageResult,
                        true> {
  static auto supports_surface(const SurfaceId &) -> bool { return true; }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.messages.size() != 1U ||
        result.primary().target != request.target) {
      throw std::invalid_argument(
          "private-send result does not match its requested scope");
    }
  }
};

template <>
struct OperationTraits<DeleteMessageRequest>
    : OperationContract<DeleteMessageRequest, DeleteMessageResult, true> {
  static auto supports_surface(const SurfaceId &) -> bool { return true; }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.message.group.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.message != request.message) {
      throw std::invalid_argument(
          "delete result does not match its requested message");
    }
  }
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_MESSAGING_HPP_
