#ifndef OBCX_INCLUDE_CORE_ACTOR_MESSAGES_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_MESSAGES_HPP_

#include "common/json_utils.hpp"

#include <stdexcept>

namespace obcx::core::events {

struct RawMessageEvent {
  common::json payload = common::json::object();
};

inline void from_json(const common::json &document, RawMessageEvent &message) {
  if (!document.is_object()) {
    throw std::invalid_argument("RawMessageEvent payload must be an object");
  }
  message.payload = document;
}

inline void to_json(common::json &document, const RawMessageEvent &message) {
  document = message.payload;
}

struct RawNoticeEvent {
  common::json payload = common::json::object();
};

inline void from_json(const common::json &document, RawNoticeEvent &notice) {
  if (!document.is_object()) {
    throw std::invalid_argument("RawNoticeEvent payload must be an object");
  }
  notice.payload = document;
}

inline void to_json(common::json &document, const RawNoticeEvent &notice) {
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

struct MessageStored {
  common::json payload = common::json::object();
};

inline void from_json(const common::json &document, MessageStored &message) {
  if (!document.is_object()) {
    throw std::invalid_argument("MessageStored payload must be an object");
  }
  message.payload = document;
}

inline void to_json(common::json &document, const MessageStored &message) {
  document = message.payload;
}

} // namespace obcx::message_store::events

#endif // OBCX_INCLUDE_CORE_ACTOR_MESSAGES_HPP_
