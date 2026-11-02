#pragma once

#include "onebot11/bot/standard_composition.hpp"

namespace obcx::onebot11::bot::standard {
enum class SendMessageType : std::uint8_t { Private, Group };
struct SendPrivateMsgParameters {
  obcx::bot::ConversationRef target;
  MessageInput message;
  bool auto_escape;
};
struct SendGroupMsgParameters {
  obcx::bot::ConversationRef target;
  MessageInput message;
  bool auto_escape;
};
struct SendMsgParameters {
  obcx::bot::ConversationRef target;
  // Absence deliberately selects standard inference from the single target.
  std::optional<SendMessageType> message_type;
  MessageInput message;
  bool auto_escape;
};
// A completed provider result scoped to the actual original destination, not a
// delivery receipt or ingestion/reply-evidence publication.
struct SendMessageData {
  std::int32_t message_id;
  obcx::bot::MessageRef message;
};
} // namespace obcx::onebot11::bot::standard
