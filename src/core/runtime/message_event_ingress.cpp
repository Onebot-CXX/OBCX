#include "core/runtime/message_event_ingress.hpp"

#include "core/actor/actor_messages.hpp"
#include "core/actor/reflected_actor_impl.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/message_archive.hpp"
#include "core/infrastructure/process_staging_uuid.hpp"

#include <atomic>
#include <chrono>
#include <limits>

namespace obcx::core {
namespace {

auto ingress_json_scalar_to_string(const common::json &value) -> std::string {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  return {};
}

auto conversation_id_from_event(const common::MessageEvent &event)
    -> std::string {
  if (event.data.is_object() && event.data.contains("chat") &&
      event.data.at("chat").is_object() &&
      event.data.at("chat").contains("id")) {
    auto chat_id =
        ingress_json_scalar_to_string(event.data.at("chat").at("id"));
    if (!chat_id.empty()) {
      return "chat:" + chat_id;
    }
  }

  if (event.guild_id.has_value() && event.channel_id.has_value()) {
    return "guild:" + *event.guild_id + ":channel:" + *event.channel_id;
  }
  if (event.group_id.has_value() && !event.group_id->empty()) {
    return "group:" + *event.group_id;
  }
  if (event.channel_id.has_value() && !event.channel_id->empty()) {
    return "channel:" + *event.channel_id;
  }
  if (!event.user_id.empty()) {
    return "private:" + event.user_id;
  }
  return "global";
}

auto conversation_id_from_event(const common::NoticeEvent &event)
    -> std::string {
  if (event.group_id.has_value() && !event.group_id->empty()) {
    return "group:" + *event.group_id;
  }
  if (!event.user_id.empty()) {
    return "private:" + event.user_id;
  }
  return "global";
}

auto event_raw_json(const common::NoticeEvent &event) -> common::json {
  auto raw = event.data.is_object() ? event.data : common::json::object();
  raw["time"] = std::chrono::duration_cast<std::chrono::duration<double>>(
                    event.time.time_since_epoch())
                    .count();
  raw["self_id"] = event.self_id;
  raw["post_type"] = event.post_type.empty() ? "notice" : event.post_type;
  raw["notice_type"] = event.notice_type;
  raw["user_id"] = event.user_id;
  if (event.group_id.has_value()) {
    raw["group_id"] = event.group_id.value();
  }
  return raw;
}

} // namespace

auto message_envelope_from_event(const BotEventContext &context,
                                 const common::MessageEvent &event,
                                 const PlatformAdapter &binding,
                                 BotOperationDispatcher &dispatcher)
    -> MessageEnvelope {
  static std::atomic_uint64_t sequence{0};
  const bot::BotInstallationRef installation{context.installation_id,
                                             context.surface};
  binding.codec().validate(installation);
  if (!context.message_admission)
    throw std::invalid_argument("message has no pre-queue evidence admission");
  const auto received_at = context.message_admission->received_at;
  const auto observation =
      std::string{detail::process_staging_uuid()} + ":ingress:" +
      std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
  auto message = dispatcher.normalize_live_message(
      installation, event, received_at, observation, context.source_origin);
  bot::binding::validate_native_event(binding.codec(), message);
  if (message.message.conversation.installation != installation ||
      message.received_at.unix_milliseconds != received_at.unix_milliseconds ||
      (message.reply && message.reply->reference() &&
       message.reply->reference()->observation_id() != observation))
    throw std::invalid_argument(
        "normalized event scope or observation mismatch");
  auto envelope = raw_message_envelope_from_event(
      context.platform, context.installation_id, event);
  envelope.payload["message"] = message;
  dispatcher.admit_message(message,
                           context.message_admission->evidence_generation);
  return envelope;
}

auto raw_message_envelope_from_event(const std::string &source_platform,
                                     const std::string &source_bot,
                                     const common::MessageEvent &event)
    -> MessageEnvelope {
  const auto bot_id = source_bot.empty() ? event.self_id : source_bot;
  const auto conversation_id = conversation_id_from_event(event);
  auto raw = message_archive_from_event(event);
  std::string chat_id;
  if (event.data.is_object() && event.data.contains("chat") &&
      event.data.at("chat").is_object() &&
      event.data.at("chat").contains("id")) {
    chat_id = ingress_json_scalar_to_string(event.data.at("chat").at("id"));
  }

  MessageEnvelope envelope;
  envelope.id = "raw:" + source_platform + ":" + bot_id + ":" +
                conversation_id + ":" + event.message_id;
  envelope.type = canonical_message_type_name<events::RawMessageEvent>();
  envelope.source_platform = source_platform;
  envelope.source_bot = bot_id;
  envelope.conversation_id = conversation_id;
  envelope.correlation_id = envelope.id;
  envelope.timestamp = event.time;
  envelope.payload = {
      {"message_id", event.message_id},
      {"conversation_id", conversation_id},
      {"source_bot_configured", !source_bot.empty()},
      {"sender", event.user_id},
      {"group_id", event.group_id.value_or(std::string{})},
      {"chat_id", std::move(chat_id)},
      {"message_type", event.message_type},
      {"payload", raw},
  };
  if (event.data.is_object() && event.data.contains("message_thread_id")) {
    const auto &topic = event.data.at("message_thread_id");
    if (topic.is_number_integer()) {
      envelope.payload["topic_id"] = topic.get<std::int64_t>();
    } else if (topic.is_number_unsigned() &&
               topic.get<std::uint64_t>() <=
                   static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
      envelope.payload["topic_id"] =
          static_cast<std::int64_t>(topic.get<std::uint64_t>());
    } else {
      // Preserve invalid topic presence so command policy/reply validation
      // fails closed instead of degrading a topic event to a group event.
      envelope.payload["topic_id"] = 0;
    }
  }
  envelope.raw = std::move(raw);
  return envelope;
}

auto notice_envelope_from_event(const BotEventContext &context,
                                const common::NoticeEvent &event,
                                const PlatformAdapter &binding)
    -> MessageEnvelope {
  const bot::BotInstallationRef installation{context.installation_id,
                                             context.surface};
  binding.codec().validate(installation);
  if (!context.notice_received_at)
    throw std::invalid_argument("notice has no pre-queue observation time");
  bot::detail::validate_bot_wire(event.data);
  auto deleted = binding.normalize_deleted_notice(installation, event,
                                                  *context.notice_received_at);
  if (deleted) {
    deleted->validate();
    binding.codec().validate(deleted->message);
    if (deleted->message.conversation.installation != installation ||
        deleted->received_at != *context.notice_received_at)
      throw std::invalid_argument("deleted notice scope or time mismatch");
  }
  auto envelope = raw_notice_envelope_from_event(
      context.platform, context.installation_id, event);
  if (deleted)
    envelope.payload["message_deleted"] = *deleted;
  bot::detail::validate_bot_wire(envelope.payload);
  // Keep the same single envelope, identity, raw archive and correlation.
  // Unlike a message, this observation does not enter reply/media admission.
  return envelope;
}

auto raw_notice_envelope_from_event(const std::string &source_platform,
                                    const std::string &source_bot,
                                    const common::NoticeEvent &event)
    -> MessageEnvelope {
  static std::atomic_uint64_t next_notice_id{1};

  const auto bot_id = source_bot.empty() ? event.self_id : source_bot;
  const auto conversation_id = conversation_id_from_event(event);
  auto raw = event_raw_json(event);
  const auto sequence = next_notice_id.fetch_add(1, std::memory_order_relaxed);

  MessageEnvelope envelope;
  envelope.id = "notice:" + source_platform + ":" + bot_id + ":" +
                conversation_id + ":" + event.notice_type + ":" +
                std::to_string(sequence);
  envelope.type = canonical_message_type_name<events::RawNoticeEvent>();
  envelope.source_platform = source_platform;
  envelope.source_bot = bot_id;
  envelope.conversation_id = conversation_id;
  envelope.correlation_id = envelope.id;
  envelope.timestamp = event.time;
  envelope.payload = {
      {"notice_type", event.notice_type},
      {"conversation_id", conversation_id},
      {"sender", event.user_id},
      {"group_id", event.group_id.value_or(std::string{})},
      {"payload", raw},
  };
  envelope.raw = std::move(raw);
  return envelope;
}

auto raw_heartbeat_envelope_from_event(const std::string &source_platform,
                                       const std::string &source_bot,
                                       const common::HeartbeatEvent &event)
    -> MessageEnvelope {
  static std::atomic_uint64_t next_heartbeat_id{1};

  const auto bot_id = source_bot.empty() ? event.self_id : source_bot;
  const auto sequence =
      next_heartbeat_id.fetch_add(1, std::memory_order_relaxed);

  MessageEnvelope envelope;
  envelope.id = "heartbeat:" + source_platform + ":" + bot_id + ":" +
                std::to_string(sequence);
  envelope.type = canonical_message_type_name<events::RawHeartbeatEvent>();
  envelope.source_platform = source_platform;
  envelope.source_bot = bot_id;
  envelope.conversation_id = "global";
  envelope.correlation_id = envelope.id;
  envelope.timestamp = event.time;
  envelope.payload = {
      {"interval_ms", event.interval},
      {"source_bot_configured", !source_bot.empty()},
  };
  return envelope;
}

auto bot_send_confirmed_envelope(const std::string &source_platform,
                                 const bot::SendConfirmation &confirmation)
    -> MessageEnvelope {
  confirmation.validate();
  MessageEnvelope envelope;
  const auto &message = confirmation.output.message;
  envelope.id = "confirmed:" + confirmation.attempt_id + ":" +
                message.conversation.id.serialized() + ":" +
                message.id.serialized();
  envelope.type = canonical_message_type_name<events::BotSendConfirmedEvent>();
  envelope.source_platform = source_platform;
  envelope.source_bot = message.conversation.installation.installation_id;
  envelope.conversation_id = "global";
  envelope.correlation_id = confirmation.attempt_id;
  envelope.timestamp = std::chrono::system_clock::now();
  envelope.payload = events::BotSendConfirmedEvent{confirmation};
  return envelope;
}

auto bot_send_succeeded_envelope(const std::string &source_platform,
                                 const SuccessfulBotOperation &operation)
    -> MessageEnvelope {
  if (!operation.send_receipt)
    throw std::invalid_argument("send success requires a complete receipt");
  auto envelope = bot_message_sent_envelope(
      source_platform, operation.installation.installation_id,
      operation.action);
  envelope.correlation_id = operation.attempt_id;
  envelope.payload["attempt_id"] = operation.attempt_id;
  envelope.payload["receipt"] = *operation.send_receipt;
  return envelope;
}

auto bot_message_sent_envelope(const std::string &source_platform,
                               const std::string &source_bot,
                               const bot::ActionId &action) -> MessageEnvelope {
  static std::atomic_uint64_t next_message_sent_id{1};

  const auto sequence =
      next_message_sent_id.fetch_add(1, std::memory_order_relaxed);
  MessageEnvelope envelope;
  envelope.id = "message-sent:" + source_platform + ":" + source_bot + ":" +
                std::to_string(sequence);
  envelope.type = canonical_message_type_name<events::BotMessageSentEvent>();
  envelope.source_platform = source_platform;
  envelope.source_bot = source_bot;
  envelope.conversation_id = "global";
  envelope.correlation_id = envelope.id;
  envelope.timestamp = std::chrono::system_clock::now();
  envelope.payload = {{"action", action.value()}};
  return envelope;
}

} // namespace obcx::core
