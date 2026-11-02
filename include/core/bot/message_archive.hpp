#pragma once

#include "common/message_type.hpp"

#include <chrono>

namespace obcx::core {

// Process-private canonical archive construction. Ingress and received-media
// matching must use the same representation, without changing stored payloads.
inline auto message_archive_from_event(const common::MessageEvent &event)
    -> common::json {
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

} // namespace obcx::core
