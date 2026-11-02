#pragma once
#include "core/bot/domain_messaging.hpp"
#include "core/bot/received_observations.hpp"

namespace obcx::bot {
namespace binding {
class EvidenceFactory;
}
struct KnownUser {
  UserRef user;
  void validate() const { user.validate(); }
};
struct ConversationSender {
  ConversationRef conversation;
  void validate() const { conversation.validate(); }
};
struct UnknownSender {};
using Sender = std::variant<KnownUser, ConversationSender, UnknownSender>;
struct UnresolvedMention {
  // Owning codec supplies the same comparison spelling as SelfInfo.username.
  std::string username;
  void validate() const {
    detail::validate_identifier(username, "mention username", 1024);
  }
};
struct MentionEveryone {};
using ReceivedRichText = std::vector<
    std::variant<Text, Mention, UnresolvedMention, MentionEveryone>>;
struct ReceivedMedia {
  MediaRef media;
  MediaKind kind;
  MediaMetadata metadata;
  std::optional<MediaIdentity> identity;
  ReceivedRichText caption;
  void validate() const {
    media.validate();
    metadata.validate();
    if (kind > MediaKind::File)
      throw std::invalid_argument("invalid received media kind");
    for (const auto &span : caption)
      std::visit(
          [](const auto &v) {
            if constexpr (requires { v.validate(); })
              v.validate();
          },
          span);
    if (identity) {
      identity->validate();
      if (identity->installation != media.installation)
        throw std::invalid_argument("received identity scope mismatch");
    }
  }
};
struct UnsupportedContent {
  std::string category;
  std::optional<std::string> description;
  void validate() const {
    detail::validate_identifier(category, "unsupported category", 128);
    if (description)
      detail::validate_identifier(*description, "unsupported description",
                                  2048);
  }
};
using ReceivedBlock =
    std::variant<ReceivedRichText, ReceivedMedia, UnsupportedContent>;
using ReceivedBody = std::vector<ReceivedBlock>;
struct MediaGroupRef {
  ConversationRef conversation;
  MediaGroupId id;
  void validate() const {
    conversation.validate();
    id.validate_surface(conversation.installation.surface);
  }
  auto operator==(const MediaGroupRef &) const -> bool = default;
};
// Signed milliseconds since Unix epoch: preserves absent native time
// separately.
struct TimePoint {
  std::int64_t unix_milliseconds;
  auto operator<=>(const TimePoint &) const = default;
};
enum class ReplyRelation : std::uint8_t {
  SameConversation,
  CrossConversation,
  External,
  Unavailable
};
enum class ReplyAvailability : std::uint8_t {
  ReferenceOnly,
  AuthorKnown,
  Unavailable
};

// Values can be restored but are not lookup authority. Runtime must verify the
// source/target/observation tuple against admitted evidence before ANY lookup.
class ReplyRef {
public:
  using obcx_bot_json_factory = void;
  auto source() const -> const MessageRef & { return source_; }
  auto target() const -> const MessageRef & { return target_; }
  auto observation_id() const -> const std::string & { return observation_id_; }
  void validate() const {
    source_.validate();
    target_.validate();
    detail::validate_identifier(observation_id_, "reply observation", 1024);
    detail::validate_utf8(observation_id_);
    if (source_.conversation.installation != target_.conversation.installation)
      throw std::invalid_argument("reply evidence installation mismatch");
  }
  static auto from_json(const Json &d) -> ReplyRef {
    detail::validate_domain_wire(d);
    detail::require_only_keys(d, "ReplyRef",
                              {"schema", "source", "target", "observation_id"});
    if (detail::bounded_unsigned(d.at("schema"), 1) != 1)
      throw std::invalid_argument("reply schema mismatch");
    ReplyRef v{
        d.at("source").get<MessageRef>(), d.at("target").get<MessageRef>(),
        detail::require_identifier(d, "observation_id", "ReplyRef", 1024)};
    v.validate();
    return v;
  }
  auto operator==(const ReplyRef &) const -> bool = default;

private:
  ReplyRef(MessageRef source, MessageRef target, std::string observation)
      : source_(std::move(source)), target_(std::move(target)),
        observation_id_(std::move(observation)) {}
  MessageRef source_;
  MessageRef target_;
  std::string observation_id_;
  friend class binding::EvidenceFactory;
};
inline void to_json(Json &d, const ReplyRef &v) {
  v.validate();
  d = {{"schema", 1},
       {"source", v.source()},
       {"target", v.target()},
       {"observation_id", v.observation_id()}};
}
class ReplyInfo {
public:
  using obcx_bot_json_factory = void;
  auto source() const -> const MessageRef & { return source_; }
  auto reference() const -> const std::optional<ReplyRef> & {
    return reference_;
  }
  auto author() const -> const std::optional<Sender> & { return author_; }
  auto relation() const -> ReplyRelation { return relation_; }
  auto availability() const -> ReplyAvailability { return availability_; }
  void validate() const {
    source_.validate();
    if (relation_ > ReplyRelation::Unavailable ||
        availability_ > ReplyAvailability::Unavailable)
      throw std::invalid_argument("invalid reply evidence kind");
    if (reference_) {
      reference_->validate();
      if (reference_->source() != source_)
        throw std::invalid_argument("reply source mismatch");
      const bool same =
          source_.conversation == reference_->target().conversation;
      if ((relation_ != ReplyRelation::SameConversation &&
           relation_ != ReplyRelation::CrossConversation) ||
          (same != (relation_ == ReplyRelation::SameConversation)))
        throw std::invalid_argument("reply relation mismatch");
    } else if (relation_ == ReplyRelation::SameConversation ||
               relation_ == ReplyRelation::CrossConversation)
      throw std::invalid_argument("exact reply relation requires reference");
    if (availability_ == ReplyAvailability::ReferenceOnly && !reference_)
      throw std::invalid_argument(
          "reference-only reply requires exact reference");
    if ((availability_ == ReplyAvailability::AuthorKnown) !=
        author_.has_value())
      throw std::invalid_argument("reply author availability mismatch");
    if (author_)
      std::visit(
          [&](const auto &v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<V, UnknownSender>)
              throw std::invalid_argument(
                  "unknown reply author must be absent");
            else {
              v.validate();
              const auto &installation = [&]() -> const BotInstallationRef & {
                if constexpr (std::same_as<V, KnownUser>)
                  return v.user.installation;
                else
                  return v.conversation.installation;
              }();
              if (installation != source_.conversation.installation)
                throw std::invalid_argument(
                    "reply author installation mismatch");
            }
          },
          *author_);
  }
  static auto from_json(const Json &d) -> ReplyInfo;

private:
  ReplyInfo(MessageRef source, std::optional<ReplyRef> reference,
            std::optional<Sender> author, ReplyRelation relation,
            ReplyAvailability availability)
      : source_(std::move(source)), reference_(std::move(reference)),
        author_(std::move(author)), relation_(relation),
        availability_(availability) {}
  MessageRef source_;
  std::optional<ReplyRef> reference_;
  std::optional<Sender> author_;
  ReplyRelation relation_;
  ReplyAvailability availability_;
  friend class binding::EvidenceFactory;
};
inline void validate_received_text(const ReceivedRichText &text,
                                   const BotInstallationRef &installation) {
  for (const auto &span : text)
    std::visit(
        [&](const auto &v) {
          if constexpr (requires { v.validate(); })
            v.validate();
          if constexpr (std::same_as<std::decay_t<decltype(v)>, Mention>) {
            if (v.user.installation != installation)
              throw std::invalid_argument("received mention scope mismatch");
          }
        },
        span);
}
// Current source observations captured at ingress, not a native event archive.
// Group/topic values are configuration-selection facts, not reference
// factories.
struct SenderProfileObservation {
  std::optional<std::string> first_name, last_name, username;
  void validate() const {
    for (const auto *field : {&first_name, &last_name, &username})
      if (*field) {
        if ((*field)->size() > 1024)
          throw std::invalid_argument("sender profile exceeds bound");
        detail::validate_utf8(**field);
      }
  }
};
struct MessageContext {
  std::string kind;
  std::string group_id;
  std::optional<std::int64_t> topic_id;
  bool topic_root_reply{};
  std::optional<bool> sender_is_bot;
  std::optional<UserRef> observed_self;
  std::optional<SenderProfileObservation> sender_profile;
  std::optional<TimePoint> edited_at;
  void validate() const {
    if (!kind.empty() && kind != "group" && kind != "private" &&
        kind != "channel")
      throw std::invalid_argument("invalid message context kind");
    if (!group_id.empty())
      detail::validate_identifier(group_id, "observed group", 1024);
    if ((kind == "group") != !group_id.empty() && !kind.empty())
      throw std::invalid_argument("message group context mismatch");
    if (topic_id &&
        (kind != "group" || *topic_id <= 0 || *topic_id > INT32_MAX))
      throw std::invalid_argument("invalid observed topic");
    if (observed_self)
      observed_self->validate();
    if (sender_profile)
      sender_profile->validate();
  }
};
struct MessageReceived {
  MessageRef message;
  Sender sender;
  std::optional<TimePoint> sent_at;
  TimePoint received_at;
  ReceivedBody body;
  std::optional<ReplyInfo> reply;
  std::optional<MediaGroupRef> media_group;
  MessageContext context;
  std::vector<ReceivedObservation> observations;
  void validate() const {
    message.validate();
    context.validate();
    const auto &installation = message.conversation.installation;
    if (context.observed_self &&
        context.observed_self->installation != installation)
      throw std::invalid_argument("observed self scope mismatch");
    std::visit(
        [&](const auto &v) {
          using V = std::decay_t<decltype(v)>;
          if constexpr (!std::same_as<V, UnknownSender>) {
            v.validate();
            if constexpr (std::same_as<V, KnownUser>) {
              if (v.user.installation != installation)
                throw std::invalid_argument("sender scope mismatch");
            } else if (v.conversation.installation != installation)
              throw std::invalid_argument("conversation sender scope mismatch");
          }
        },
        sender);
    if (reply) {
      reply->validate();
      if (reply->source() != message)
        throw std::invalid_argument("received reply source mismatch");
    }
    if (media_group) {
      media_group->validate();
      if (media_group->conversation != message.conversation)
        throw std::invalid_argument("media group scope mismatch");
    }
    for (const auto &observation : observations) {
      observation.validate();
      if (observation.block_index >= body.size())
        throw std::invalid_argument("receive observation block mismatch");
    }
    for (const auto &block : body)
      std::visit(
          [&](const auto &v) {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<V, ReceivedRichText>)
              validate_received_text(v, installation);
            else {
              v.validate();
              if constexpr (std::same_as<V, ReceivedMedia>) {
                if (v.media.installation != installation)
                  throw std::invalid_argument("received media scope mismatch");
                validate_received_text(v.caption, installation);
              }
            }
          },
          block);
  }
};
struct MessageUpdated {
  MessageReceived message;
  void validate() const { message.validate(); }
};
struct MessageDeleted {
  MessageRef message;
  TimePoint received_at;
  void validate() const { message.validate(); }
};
struct SendConfirmation {
  std::string attempt_id;
  SendOutputDetails output;
  void validate() const {
    detail::validate_identifier(attempt_id, "send attempt", 1024);
    output.validate();
  }
};
template <> struct detail::DomainRecordPreflight<UnresolvedMention> {
  static void validate(const Json &d) {
    (void)detail::require_identifier(d, "username", "UnresolvedMention", 1024);
  }
};
template <> struct detail::DomainRecordPreflight<UnsupportedContent> {
  static void validate(const Json &d) {
    (void)detail::require_identifier(d, "category", "UnsupportedContent", 128);
    detail::preflight_optional_identifier(d, "description",
                                          "UnsupportedContent", 2048);
  }
};
template <> struct detail::DomainRecordPreflight<SendConfirmation> {
  static void validate(const Json &d) {
    (void)detail::require_identifier(d, "attempt_id", "SendConfirmation", 1024);
  }
};

OBCX_DOMAIN_FIELDS(KnownUser, OBCX_DOMAIN_FIELD(user));
OBCX_DOMAIN_FIELDS(ConversationSender, OBCX_DOMAIN_FIELD(conversation));
OBCX_DOMAIN_FIELDS(UnknownSender);
OBCX_DOMAIN_FIELDS(UnresolvedMention, OBCX_DOMAIN_FIELD(username));
OBCX_DOMAIN_FIELDS(MentionEveryone);
OBCX_DOMAIN_FIELDS(ReceivedMedia, OBCX_DOMAIN_FIELD(media),
                   OBCX_DOMAIN_FIELD(kind), OBCX_DOMAIN_FIELD(metadata),
                   OBCX_DOMAIN_FIELD(identity), OBCX_DOMAIN_FIELD(caption));
OBCX_DOMAIN_FIELDS(UnsupportedContent, OBCX_DOMAIN_FIELD(category),
                   OBCX_DOMAIN_FIELD(description));
OBCX_DOMAIN_FIELDS(MediaGroupRef, OBCX_DOMAIN_FIELD(conversation),
                   OBCX_DOMAIN_FIELD(id));
OBCX_DOMAIN_FIELDS(TimePoint, OBCX_DOMAIN_FIELD(unix_milliseconds));
OBCX_DOMAIN_FIELDS(SenderProfileObservation, OBCX_DOMAIN_FIELD(first_name),
                   OBCX_DOMAIN_FIELD(last_name), OBCX_DOMAIN_FIELD(username));
OBCX_DOMAIN_FIELDS(MessageContext, OBCX_DOMAIN_FIELD(kind),
                   OBCX_DOMAIN_FIELD(group_id), OBCX_DOMAIN_FIELD(topic_id),
                   OBCX_DOMAIN_FIELD(topic_root_reply),
                   OBCX_DOMAIN_FIELD(sender_is_bot),
                   OBCX_DOMAIN_FIELD(observed_self),
                   OBCX_DOMAIN_FIELD(sender_profile),
                   OBCX_DOMAIN_FIELD(edited_at));
OBCX_DOMAIN_FIELDS(MessageReceived, OBCX_DOMAIN_FIELD(message),
                   OBCX_DOMAIN_FIELD(sender), OBCX_DOMAIN_FIELD(sent_at),
                   OBCX_DOMAIN_FIELD(received_at), OBCX_DOMAIN_FIELD(body),
                   OBCX_DOMAIN_FIELD(reply), OBCX_DOMAIN_FIELD(media_group),
                   OBCX_DOMAIN_FIELD(context), OBCX_DOMAIN_FIELD(observations));
OBCX_DOMAIN_FIELDS(MessageUpdated, OBCX_DOMAIN_FIELD(message));
OBCX_DOMAIN_FIELDS(MessageDeleted, OBCX_DOMAIN_FIELD(message),
                   OBCX_DOMAIN_FIELD(received_at));
OBCX_DOMAIN_FIELDS(SendConfirmation, OBCX_DOMAIN_FIELD(attempt_id),
                   OBCX_DOMAIN_FIELD(output));
inline auto ReplyInfo::from_json(const Json &d) -> ReplyInfo {
  detail::validate_domain_wire(d);
  detail::require_only_keys(
      d, "ReplyInfo",
      {"schema", "source", "reference", "author", "relation", "availability"});
  if (detail::bounded_unsigned(d.at("schema"), 1) != 1)
    throw std::invalid_argument("reply schema mismatch");
  ReplyInfo v{d.at("source").get<MessageRef>(),
              detail::domain_decode<std::optional<ReplyRef>>(d.at("reference")),
              detail::domain_decode<std::optional<Sender>>(d.at("author")),
              detail::domain_decode<ReplyRelation>(d.at("relation")),
              detail::domain_decode<ReplyAvailability>(d.at("availability"))};
  v.validate();
  return v;
}
inline void to_json(Json &d, const ReplyInfo &v) {
  v.validate();
  d = {{"schema", 1},
       {"source", v.source()},
       {"reference", detail::domain_encode(v.reference())},
       {"author", detail::domain_encode(v.author())},
       {"relation", detail::domain_encode(v.relation())},
       {"availability", detail::domain_encode(v.availability())}};
}
} // namespace obcx::bot
