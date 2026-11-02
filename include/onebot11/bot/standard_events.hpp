#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_service.hpp"

namespace obcx::onebot11::bot::standard {

// Reported native numbers, not routable references or evidence of identity,
// membership, message existence, origin admission or authorization. In
// particular, non-user sentinel values must not be promoted to UserRef.
struct NoticeHeader {
  std::int64_t time; // Native Unix seconds, not local receive time.
  std::int64_t self_id;
};

enum class GroupAdminChange : std::uint8_t { Set, Unset };
enum class GroupDecreaseKind : std::uint8_t { Leave, Kick, KickMe };
enum class GroupIncreaseKind : std::uint8_t { Approve, Invite };
enum class GroupBanChange : std::uint8_t { Ban, LiftBan };
enum class GroupHonorKind : std::uint8_t { Talkative, Performer, Emotion };

struct GroupAdminNotice {
  GroupAdminChange sub_type;
  std::int64_t group_id;
  std::int64_t user_id;
};

struct GroupDecreaseNotice {
  GroupDecreaseKind sub_type;
  std::int64_t group_id;
  std::int64_t operator_id;
  std::int64_t user_id;
};

struct GroupIncreaseNotice {
  GroupIncreaseKind sub_type;
  std::int64_t group_id;
  std::int64_t operator_id;
  std::int64_t user_id;
};

struct GroupBanNotice {
  GroupBanChange sub_type;
  std::int64_t group_id;
  std::int64_t operator_id;
  std::int64_t user_id;
  std::int64_t duration; // Native seconds; no inferred or default duration.
};

struct FriendAddNotice {
  std::int64_t user_id;
};

struct GroupRecallNotice {
  std::int64_t group_id;
  std::int64_t user_id;
  std::int64_t operator_id;
  std::int64_t message_id;
};

struct FriendRecallNotice {
  std::int64_t user_id;
  std::int64_t message_id;
};

struct GroupPokeNotice {
  std::int64_t group_id;
  std::int64_t user_id;
  std::int64_t target_id;
};

struct GroupLuckyKingNotice {
  std::int64_t group_id;
  std::int64_t user_id;
  std::int64_t target_id;
};

struct GroupHonorNotice {
  std::int64_t group_id;
  GroupHonorKind honor_type;
  std::int64_t user_id;
};

// Only the non-sensitive notice projection; not a complete standard event
// carrier or an actor input. Do not persist variant ordinals as wire tags.
using NoticePayload =
    std::variant<GroupAdminNotice, GroupDecreaseNotice, GroupIncreaseNotice,
                 GroupBanNotice, FriendAddNotice, GroupRecallNotice,
                 FriendRecallNotice, GroupPokeNotice, GroupLuckyKingNotice,
                 GroupHonorNotice>;

struct NoticeObservation {
  NoticeHeader header;
  NoticePayload payload;
};

// Sibling pure observations, not registered carriers or live action handles.
// Reported numbers retain their signed native values without identity,
// membership, source, receipt or authorization claims.
struct EventHeader {
  std::int64_t time; // Native Unix seconds, not local receive time.
  std::int64_t self_id;
};

enum class LifecycleKind : std::uint8_t { Enable, Disable, Connect };
struct LifecycleObservation {
  EventHeader header;
  LifecycleKind sub_type;
};

struct UploadedFileObservation {
  std::string id;
  std::string name;
  std::int64_t size;
  std::int64_t busid;
};
struct GroupUploadObservation {
  EventHeader header;
  std::int64_t group_id;
  std::int64_t user_id;
  UploadedFileObservation file; // No path, readable media or acquisition proof.
};

struct FriendRequestObservation {
  EventHeader header;
  std::int64_t user_id;
  std::string comment;
  SensitiveText
      flag; // Explicit restricted access is not live-action authority.
};
enum class GroupRequestKind : std::uint8_t { Add, Invite };
struct GroupRequestObservation {
  EventHeader header;
  GroupRequestKind sub_type;
  std::int64_t group_id;
  std::int64_t user_id;
  std::string comment;
  SensitiveText flag;
};

struct HeartbeatObservation {
  EventHeader header;
  StatusData status;
  std::int64_t interval; // Native milliseconds until the next heartbeat.
};

// Missing and explicit null are distinct observations, not invented defaults.
template <typename T> struct EventField {
  bool present;
  std::optional<T> value;
};
struct EventText {
  std::string text;
};
struct EventFace {
  std::string id;
};
struct EventImage {
  SensitiveText file;
  EventField<std::string> type;
  EventField<SensitiveText> url;
};
struct EventRecord {
  SensitiveText file;
  EventField<std::string> magic;
  EventField<SensitiveText> url;
};
struct EventVideo {
  SensitiveText file;
  EventField<SensitiveText> url;
};
struct EventAt {
  std::string qq;
};
struct EventRps {};
struct EventDice {};
struct EventPoke {
  std::string type;
  std::string id;
  EventField<std::string> name;
};
struct EventShare {
  SensitiveText url;
  std::string title;
  EventField<std::string> content;
  EventField<SensitiveText> image;
};
struct EventContact {
  std::string type;
  std::string id;
};
struct EventLocation {
  std::string latitude;
  std::string longitude;
  EventField<std::string> title;
  EventField<std::string> content;
};
struct EventReply {
  std::string id;
}; // No reply provenance.
struct EventForward {
  SensitiveText id;
};
struct EventXml {
  SensitiveText document;
};
struct EventJson {
  SensitiveText document;
};
using EventSegment =
    std::variant<EventText, EventFace, EventImage, EventRecord, EventVideo,
                 EventAt, EventRps, EventDice, EventPoke, EventShare,
                 EventContact, EventLocation, EventReply, EventForward,
                 EventXml, EventJson>;
enum class EventMessageEncoding : std::uint8_t { CqString, SegmentArray };
struct EventMessageContent {
  EventMessageEncoding encoding;
  std::vector<EventSegment> segments;
};
struct EventSender {
  EventField<std::int64_t> user_id;
  EventField<std::string> nickname;
  EventField<Sex> sex;
  EventField<std::int32_t> age;
  EventField<std::string> card;
  EventField<std::string> area;
  EventField<std::string> level;
  EventField<GroupRole> role;
  EventField<std::string> title;
};
enum class PrivateMessageSubtype : std::uint8_t { Friend, Group, Other };
enum class GroupMessageSubtype : std::uint8_t { Normal, Anonymous, Notice };
struct EventAnonymous {
  std::int64_t id;
  std::string name;
  // The authentic flag is retained only by the private trusted context.
};
struct PrivateMessageObservation {
  EventHeader header;
  PrivateMessageSubtype sub_type;
  std::int32_t message_id;
  std::int64_t user_id;
  EventMessageContent message;
  SensitiveText raw_message;
  std::int32_t font;
  EventSender sender;
};
struct GroupMessageObservation {
  EventHeader header;
  GroupMessageSubtype sub_type;
  std::int32_t message_id;
  std::int64_t group_id;
  std::int64_t user_id;
  std::optional<EventAnonymous> anonymous;
  EventMessageContent message;
  SensitiveText raw_message;
  std::int32_t font;
  EventSender sender;
};
// Flags never enter the admitted public projection, even as restricted text.
struct ReceivedFriendRequest {
  EventHeader header;
  std::int64_t user_id;
  std::string comment;
};
struct ReceivedGroupRequest {
  EventHeader header;
  GroupRequestKind sub_type;
  std::int64_t group_id;
  std::int64_t user_id;
  std::string comment;
};
using StandardEventObservation =
    std::variant<PrivateMessageObservation, GroupMessageObservation,
                 NoticeObservation, GroupUploadObservation,
                 ReceivedFriendRequest, ReceivedGroupRequest,
                 LifecycleObservation, HeartbeatObservation>;

} // namespace obcx::onebot11::bot::standard
