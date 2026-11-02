#pragma once

#include "common/json_utils.hpp"
#include "core/bot/domain_events.hpp"
#include "core/bot/domain_messaging.hpp"

#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace obcx::core::events {

namespace detail {
// Check the effective output tree without copying its archive. Replaced fields
// count once; the original archive is separately preflighted before encoding.
inline void validate_archive_output(
    const common::json &payload, const common::json &message,
    std::initializer_list<std::pair<std::string_view, std::string_view>>
        source_fields) {
  if (!payload.is_object() && !payload.is_null())
    throw std::invalid_argument(
        "event archival payload must be an object or null");
  bot::detail::DomainWireBudget budget;
  budget.nodes();
  if (payload.is_object()) {
    for (auto field = payload.begin(); field != payload.end(); ++field) {
      bool replaced = field.key() == "message";
      for (const auto &[key, value] : source_fields) {
        (void)value;
        replaced = replaced || field.key() == key;
      }
      if (!replaced) {
        budget.bytes(field.key().size());
        budget.inspect(field.value(), 1);
      }
    }
  }
  budget.bytes(std::string_view{"message"}.size());
  budget.inspect(message, 1);
  for (const auto &[key, value] : source_fields) {
    budget.bytes(key.size());
    budget.nodes();
    budget.bytes(value.size());
  }
}
} // namespace detail

struct RawMessageEvent {
  bot::MessageReceived message;
  // Explicit archival envelope fields; ordinary content is in message.
  common::json payload;
  using obcx_bot_json_factory = void;
  static auto from_json(const common::json &document) -> RawMessageEvent {
    bot::detail::validate_domain_wire(document);
    if (!document.is_object())
      throw std::invalid_argument("RawMessageEvent payload must be an object");
    return {document.at("message").get<bot::MessageReceived>(), document};
  }
};

inline void to_json(common::json &document, const RawMessageEvent &message) {
  bot::detail::validate_domain_wire(message.payload);
  common::json typed_message = message.message;
  detail::validate_archive_output(message.payload, typed_message, {});
  auto encoded = message.payload;
  encoded["message"] = std::move(typed_message);
  bot::detail::validate_domain_wire(encoded);
  document = std::move(encoded);
}

struct RawNoticeEvent {
  common::json payload = common::json::object();

  // Optional observation carried alongside, never instead of, the archive.
  // Absence is not proof that a message still exists; this grants no authority.
  [[nodiscard]] auto message_deleted() const
      -> std::optional<bot::MessageDeleted> {
    bot::detail::validate_domain_wire(payload);
    bot::detail::require_object(payload, "RawNoticeEvent");
    const auto value = payload.find("message_deleted");
    if (value == payload.end())
      return std::nullopt;
    return value->get<bot::MessageDeleted>();
  }
};

inline void from_json(const common::json &document, RawNoticeEvent &notice) {
  bot::detail::validate_domain_wire(document);
  if (!document.is_object()) {
    throw std::invalid_argument("RawNoticeEvent payload must be an object");
  }
  // Validate the optional typed value before copying the archival payload.
  if (document.contains("message_deleted"))
    (void)document.at("message_deleted").get<bot::MessageDeleted>();
  notice.payload = document;
}

inline void to_json(common::json &document, const RawNoticeEvent &notice) {
  (void)notice.message_deleted();
  document = notice.payload;
}

struct BotMessageSentEvent {
  common::json payload = common::json::object();
};

inline void from_json(const common::json &document,
                      BotMessageSentEvent &message_sent) {
  if (!document.is_object()) {
    throw std::invalid_argument(
        "BotMessageSentEvent payload must be an object");
  }
  message_sent.payload = document;
}

inline void to_json(common::json &document,
                    const BotMessageSentEvent &message_sent) {
  document = message_sent.payload;
}

struct BotSendConfirmedEvent {
  bot::SendConfirmation confirmation;
  using obcx_bot_json_factory = void;
  static auto from_json(const common::json &document) -> BotSendConfirmedEvent {
    bot::detail::validate_domain_wire(document);
    return {document.at("confirmation").get<bot::SendConfirmation>()};
  }
};

inline void to_json(common::json &document,
                    const BotSendConfirmedEvent &event) {
  common::json confirmation = event.confirmation;
  bot::detail::DomainWireBudget budget;
  budget.nodes();
  budget.bytes(std::string_view{"confirmation"}.size());
  budget.inspect(confirmation, 1);
  auto encoded = common::json::object();
  encoded["confirmation"] = std::move(confirmation);
  bot::detail::validate_domain_wire(encoded);
  document = std::move(encoded);
}

struct RawHeartbeatEvent {
  common::json payload = common::json::object();
};

inline void from_json(const common::json &document,
                      RawHeartbeatEvent &heartbeat) {
  if (!document.is_object()) {
    throw std::invalid_argument("RawHeartbeatEvent payload must be an object");
  }
  heartbeat.payload = document;
}

inline void to_json(common::json &document,
                    const RawHeartbeatEvent &heartbeat) {
  document = heartbeat.payload;
}

} // namespace obcx::core::events

namespace obcx::message_store::events {

struct SourceMessageIdentity {
  std::string source_platform;
  std::string source_bot;
  std::string conversation_id;
  std::string message_id;

  void validate(const bot::MessageReceived &message) const {
    if (source_platform.empty() || source_bot.empty() ||
        conversation_id.empty() || message_id.empty() ||
        source_bot != message.message.conversation.installation.installation_id)
      throw std::invalid_argument("MessageStored source identity mismatch");
  }
};

struct MessageStored {
  SourceMessageIdentity source;
  bot::MessageReceived message;
  // Current typed notification wire snapshot, never a native event archive.
  common::json payload;

  using obcx_bot_json_factory = void;
  static auto from_json(const common::json &document) -> MessageStored {
    bot::detail::validate_domain_wire(document);
    bot::detail::require_only_keys(document, "MessageStored",
                                   {"schema", "source_platform", "source_bot",
                                    "conversation_id", "message_id",
                                    "message"});
    if (bot::detail::bounded_unsigned(document.at("schema"), 2) != 2)
      throw std::invalid_argument("unsupported MessageStored schema");
    auto message = document.at("message").get<bot::MessageReceived>();
    if (message.context.kind.empty())
      throw std::invalid_argument(
          "MessageStored requires current source context");
    const auto &source_platform =
        document.at("source_platform").get_ref<const std::string &>();
    const auto &source_bot =
        document.at("source_bot").get_ref<const std::string &>();
    const auto &conversation_id =
        document.at("conversation_id").get_ref<const std::string &>();
    const auto &message_id =
        document.at("message_id").get_ref<const std::string &>();
    // The source bot must equal the already bounded installation name. Check
    // borrowed strings before materializing any owned source identity.
    if (source_platform.empty() || source_bot.empty() ||
        conversation_id.empty() || message_id.empty() ||
        source_bot != message.message.conversation.installation.installation_id)
      throw std::invalid_argument("MessageStored source identity mismatch");
    return {{source_platform, source_bot, conversation_id, message_id},
            std::move(message),
            document};
  }
};

inline void to_json(common::json &document, const MessageStored &message) {
  message.source.validate(message.message);
  message.message.validate();
  if (message.message.context.kind.empty())
    throw std::invalid_argument(
        "MessageStored requires current source context");
  common::json encoded = {{"schema", 2},
                          {"source_platform", message.source.source_platform},
                          {"source_bot", message.source.source_bot},
                          {"conversation_id", message.source.conversation_id},
                          {"message_id", message.source.message_id},
                          {"message", message.message}};
  bot::detail::validate_domain_wire(encoded);
  document = std::move(encoded);
}

} // namespace obcx::message_store::events
