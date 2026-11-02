#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_sensitive.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obcx::onebot11::bot::standard {

namespace detail {
class QuerySelectorAccess; // Defined only in the private codec implementation.
}

// Selectors identify a query target, not existence, conversation or reply
// evidence.
class MessageSelector final {
public:
  auto installation() const noexcept -> const obcx::bot::BotInstallationRef &;

private:
  MessageSelector(obcx::bot::BotInstallationRef, std::int32_t);
  obcx::bot::BotInstallationRef installation_;
  std::int32_t message_id_;
  friend class QuerySelectors;
  friend class detail::QuerySelectorAccess;
};

class ForwardSelector final {
public:
  auto installation() const noexcept -> const obcx::bot::BotInstallationRef &;

private:
  ForwardSelector(obcx::bot::BotInstallationRef, std::string);
  obcx::bot::BotInstallationRef installation_;
  std::string resource_id_;
  friend class QuerySelectors;
  friend class detail::QuerySelectorAccess;
};

// Explicit native-value access is not a persistence/diagnostic codec or a
// grant. Enclosing operations must additionally enforce their aggregate
// budgets.
class QuerySelectors final {
public:
  static auto import_message(const obcx::bot::BotInstallationRef &,
                             std::int32_t, const ValueLimits &)
      -> MessageSelector;
  static auto export_message(const MessageSelector &, const ValueLimits &)
      -> std::int32_t;
  static auto import_forward(const obcx::bot::BotInstallationRef &,
                             std::string_view, const ValueLimits &)
      -> ForwardSelector;
  static auto export_forward(const ForwardSelector &, const ValueLimits &)
      -> std::string;
};

struct GetMessageParameters {
  MessageSelector message;
};
struct GetForwardMessageParameters {
  ForwardSelector forward;
};
struct GetStrangerInfoParameters {
  obcx::bot::UserRef user;
  bool no_cache;
};
struct GetFriendListParameters {
  obcx::bot::BotInstallationRef installation;
};
struct GetGroupInfoParameters {
  obcx::bot::ConversationRef group;
  bool no_cache;
};
struct GetGroupListParameters {
  obcx::bot::BotInstallationRef installation;
};
struct GetGroupMemberInfoParameters {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  bool no_cache;
};
struct GetGroupMemberListParameters {
  obcx::bot::ConversationRef group;
};

enum class HonorKind : std::uint8_t {
  Talkative,
  Performer,
  Legend,
  StrongNewbie,
  Emotion,
  All
};
struct GetGroupHonorInfoParameters {
  obcx::bot::ConversationRef group;
  HonorKind type;
};
struct CurrentTalkative {
  obcx::bot::UserRef user;
  std::string nickname;
  SensitiveText avatar;
  std::int32_t day_count;
};
struct HonorEntry {
  obcx::bot::UserRef user;
  std::string nickname;
  SensitiveText avatar;
  std::string description;
};
// Optional categories preserve omission versus a supplied empty list. Avatars
// are restricted observations, not media identity or acquisition authority.
struct GroupHonorInfo {
  obcx::bot::ConversationRef group;
  HonorKind requested_type;
  std::optional<CurrentTalkative> current_talkative;
  std::optional<std::vector<HonorEntry>> talkative_list, performer_list,
      legend_list, strong_newbie_list, emotion_list;
};

struct StrangerInfo {
  obcx::bot::UserRef user;
  std::string nickname;
  Sex sex;
  std::int32_t age;
};
struct FriendInfo {
  obcx::bot::UserRef user;
  std::string nickname;
  std::string remark;
};
struct GroupInfo {
  obcx::bot::ConversationRef group;
  std::string group_name;
  std::int32_t member_count;
  std::int32_t max_member_count;
};
struct MemberFields {
  std::string nickname;
  std::string card;
  Sex sex;
  std::int32_t age;
  std::string area;
  std::int32_t join_time;
  std::int32_t last_sent_time;
  std::string level;
  GroupRole role;
  bool unfriendly;
  std::string title;
  std::int32_t title_expire_time;
  bool card_changeable;
};
struct GroupMemberInfo {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  MemberFields fields;
};
// A list observation does not promise the detail query's profile completeness.
// Only omission is unavailable; present null/wrong-type fields are malformed.
struct ListedMemberFields {
  std::optional<std::string> nickname;
  std::optional<std::string> card;
  std::optional<Sex> sex;
  std::optional<std::int32_t> age;
  std::optional<std::string> area;
  std::optional<std::int32_t> join_time;
  std::optional<std::int32_t> last_sent_time;
  std::optional<std::string> level;
  std::optional<GroupRole> role;
  std::optional<bool> unfriendly;
  std::optional<std::string> title;
  std::optional<std::int32_t> title_expire_time;
  std::optional<bool> card_changeable;
};
struct ListedGroupMember {
  obcx::bot::ConversationRef group;
  obcx::bot::UserRef user;
  ListedMemberFields fields;
};
struct FriendList {
  obcx::bot::BotInstallationRef installation;
  std::vector<FriendInfo> entries;
};
struct GroupList {
  obcx::bot::BotInstallationRef installation;
  std::vector<GroupInfo> entries;
};
struct GroupMemberList {
  obcx::bot::ConversationRef group;
  std::vector<ListedGroupMember> entries;
};

} // namespace obcx::onebot11::bot::standard
