#include "core/runtime/message_event_ingress.hpp"

#include "core/actor/actor_messages.hpp"
#include "core/actor/reflected_actor.hpp"

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

auto event_raw_json(const common::MessageEvent &event) -> common::json {
  auto raw = event.data.is_object() ? event.data : common::json::object();
  raw["time"] = std::chrono::duration_cast<std::chrono::duration<double>>(
                    event.time.time_since_epoch())
                    .count();
  raw["self_id"] = event.self_id;
  raw["post_type"] = event.post_type.empty() ? "message" : event.post_type;
  raw["message_type"] = event.message_type;
  raw["sub_type"] = event.sub_type;
  raw["message_id"] = event.message_id;
  raw["user_id"] = event.user_id;
  raw["message"] = common::json::array();
  for (const auto &segment : event.message) {
    raw["message"].push_back({{"type", segment.type}, {"data", segment.data}});
  }
  raw["raw_message"] = event.raw_message;
  raw["font"] = event.font;
  if (event.group_id.has_value()) {
    raw["group_id"] = event.group_id.value();
  }
  if (event.anonymous.has_value()) {
    raw["anonymous"] = event.anonymous.value();
  }
  if (event.guild_id.has_value()) {
    raw["guild_id"] = event.guild_id.value();
  }
  if (event.channel_id.has_value()) {
    raw["channel_id"] = event.channel_id.value();
  }
  return raw;
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

auto raw_message_envelope_from_event(const std::string &source_platform,
                                     const std::string &source_bot,
                                     const common::MessageEvent &event)
    -> MessageEnvelope {
  const auto bot_id = source_bot.empty() ? event.self_id : source_bot;
  const auto conversation_id = conversation_id_from_event(event);
  auto raw = event_raw_json(event);
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
