#pragma once

#include "core/bot/references.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace obcx::onebot11::bot::standard {

// Native duration fields are JSON numbers, not int32 fields. Keep the chosen
// integer or floating representation without rounding integer seconds to
// double.
struct DurationSeconds {
  std::variant<std::int64_t, double> value;
};

// Parameter values only: no invocation, execution mode or completion claim.
// References supply routing scope, not proof of provider permissions.
struct SendLikeParameters {
  obcx::bot::UserRef user;
  // The standard documents ten likes per friend per day. This is not a local
  // quota check: zero is serialized, and no provider allowance is inferred.
  std::int64_t times;

  void validate() const;
};

struct DeleteMsgParameters {
  obcx::bot::MessageRef message;

  void validate() const;
};

struct SetGroupKickParameters {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  bool reject_add_request;

  void validate() const;
};

struct SetGroupBanParameters {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  DurationSeconds duration; // Nonnegative; zero means unban.

  void validate() const;
};

struct SetGroupWholeBanParameters {
  obcx::bot::ConversationRef group;
  bool enable;

  void validate() const;
};

struct SetGroupAdminParameters {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  bool enable;

  void validate() const;
};

struct SetGroupAnonymousParameters {
  obcx::bot::ConversationRef group;
  bool enable;

  void validate() const;
};

struct SetGroupCardParameters {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  std::string card; // Explicit empty string removes the card.

  void validate() const;
};

struct SetGroupNameParameters {
  obcx::bot::ConversationRef group;
  std::string group_name;

  void validate() const;
};

struct SetGroupLeaveParameters {
  obcx::bot::ConversationRef group;
  bool is_dismiss;

  void validate() const;
};

struct SetGroupSpecialTitleParameters {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  std::string special_title; // Explicit empty string clears the title.
  // Exactly -1 means permanent; otherwise nonnegative seconds. The standard
  // warns that providers may not enforce this duration.
  DurationSeconds duration;

  void validate() const;
};

} // namespace obcx::onebot11::bot::standard
