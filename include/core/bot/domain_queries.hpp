#pragma once
#include "core/bot/domain_events.hpp"
#include "core/bot/self_info.hpp"

namespace obcx::bot {
enum class MemberRole : std::uint8_t { Owner, Administrator, Member };
enum class Membership : std::uint8_t { Present, Restricted, Left, Banned };
struct MemberInfo {
  ConversationRef conversation;
  UserRef user;
  std::optional<std::string> display_name;
  std::optional<std::string> username;
  std::optional<bool> is_bot;
  std::optional<MemberRole> role;
  std::optional<Membership> membership;
  void validate() const {
    conversation.validate();
    user.validate();
    if (conversation.installation != user.installation)
      throw std::invalid_argument("member installation mismatch");
    if (display_name)
      detail::validate_identifier(*display_name, "member display name", 2048);
    if (username)
      detail::validate_identifier(*username, "member username", 1024);
    if ((role && *role > MemberRole::Member) ||
        (membership && *membership > Membership::Banned))
      throw std::invalid_argument("invalid member evidence");
  }
};
struct GetMemberRequest {
  inline static const ActionId action{"member.get"};
  ConversationRef conversation;
  UserRef user;
  void validate() const {
    conversation.validate();
    user.validate();
    if (conversation.installation != user.installation)
      throw std::invalid_argument("member request installation mismatch");
  }
};
enum class ReplyLookupPolicy : std::uint8_t { LocalOnly, AllowProviderLookup };
struct ResolveReplyRequest {
  inline static const ActionId action{"message.reply.resolve"};
  ReplyRef reply;
  ReplyLookupPolicy policy;
  void validate() const {
    reply.validate();
    if (policy > ReplyLookupPolicy::AllowProviderLookup)
      throw std::invalid_argument("invalid reply lookup policy");
  }
};
struct ReadMediaRequest {
  inline static const ActionId action{"media.read"};
  MediaRef media;
  std::size_t maximum_bytes;
  void validate() const {
    media.validate();
    if (!maximum_bytes || maximum_bytes > detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid explicit media read bound");
  }
};
enum class CommonOperation : std::uint8_t {
  Send,
  Delete,
  GetSelf,
  GetMember,
  ResolveReply,
  ReadMedia,
  Capabilities
};
struct MediaCapability {
  MediaKind kind;
  MediaSourceKind source;
  // Missing MIME set is unknown, never an assertion of arbitrary format
  // support.
  std::optional<std::vector<std::string>> mime_types;
  bool captions;
  std::optional<std::size_t> maximum_bytes;
  std::optional<std::uint32_t> maximum_width;
  std::optional<std::uint32_t> maximum_height;
  void validate() const {
    if (kind > MediaKind::File || source > MediaSourceKind::Http)
      throw std::invalid_argument("invalid media capability");
    if (mime_types)
      for (const auto &mime : *mime_types)
        detail::validate_identifier(mime, "capability MIME", 256);
    if ((maximum_bytes && !*maximum_bytes) ||
        (maximum_width && !*maximum_width) ||
        (maximum_height && !*maximum_height))
      throw std::invalid_argument("invalid media capability bound");
  }
};
struct DeliveryCombination {
  DeliveryPolicy delivery;
  std::vector<MediaKind> media_kinds;
  bool text_blocks;
  bool captions;
  bool mixed_media;
  std::optional<std::size_t> maximum_messages;
  std::optional<std::size_t> minimum_group_size;
  std::optional<std::size_t> maximum_group_size;
  void validate() const {
    if (delivery > DeliveryPolicy::AllowSplit)
      throw std::invalid_argument("invalid capability delivery");
    for (auto kind : media_kinds)
      if (kind > MediaKind::File)
        throw std::invalid_argument("invalid capability media kind");
    if ((maximum_messages && !*maximum_messages) ||
        (minimum_group_size && !*minimum_group_size) ||
        (maximum_group_size && !*maximum_group_size) ||
        (minimum_group_size && maximum_group_size &&
         *minimum_group_size > *maximum_group_size))
      throw std::invalid_argument("invalid capability grouping bounds");
  }
};
struct ConversationCapabilities {
  ConversationRef conversation;
  std::vector<CommonOperation> operations;
  std::vector<TextStyle> text_styles;
  std::vector<MediaCapability> media;
  std::vector<DeliveryCombination> combinations;
  std::optional<std::size_t> maximum_text_utf8_bytes;
  std::optional<std::size_t> maximum_caption_utf8_bytes;
  std::optional<std::size_t> maximum_media_total_bytes;
  std::optional<bool> may_send;
  std::optional<bool> may_delete;
  void validate() const {
    conversation.validate();
    for (auto op : operations)
      if (op > CommonOperation::Capabilities)
        throw std::invalid_argument("invalid common operation capability");
    for (auto style : text_styles)
      if (style > TextStyle::Code)
        throw std::invalid_argument("invalid text style capability");
    for (const auto &item : media)
      item.validate();
    for (const auto &item : combinations)
      item.validate();
  }
};
struct GetCapabilitiesRequest {
  inline static const ActionId action{"bot.capabilities.get"};
  ConversationRef conversation;
  void validate() const { conversation.validate(); }
};
template <> struct detail::DomainRecordPreflight<MemberInfo> {
  static void validate(const Json &d) {
    detail::preflight_optional_identifier(d, "display_name", "MemberInfo",
                                          2048);
    detail::preflight_optional_identifier(d, "username", "MemberInfo", 1024);
  }
};
template <> struct detail::DomainRecordPreflight<MediaCapability> {
  static void validate(const Json &d) {
    const auto &mime_types = d.at("mime_types");
    if (mime_types.is_null())
      return;
    if (!mime_types.is_array() ||
        mime_types.size() > detail::domain_maximum_items)
      throw std::invalid_argument("expected bounded capability MIME array");
    for (const auto &mime : mime_types) {
      if (!mime.is_string())
        throw std::invalid_argument("capability MIME must be a string");
      detail::validate_identifier(mime.get_ref<const std::string &>(),
                                  "capability MIME", 256);
    }
  }
};

OBCX_DOMAIN_FIELDS(MemberInfo, OBCX_DOMAIN_FIELD(conversation),
                   OBCX_DOMAIN_FIELD(user), OBCX_DOMAIN_FIELD(display_name),
                   OBCX_DOMAIN_FIELD(username), OBCX_DOMAIN_FIELD(is_bot),
                   OBCX_DOMAIN_FIELD(role), OBCX_DOMAIN_FIELD(membership));
OBCX_DOMAIN_FIELDS(GetMemberRequest, OBCX_DOMAIN_FIELD(conversation),
                   OBCX_DOMAIN_FIELD(user));
OBCX_DOMAIN_FIELDS(ResolveReplyRequest, OBCX_DOMAIN_FIELD(reply),
                   OBCX_DOMAIN_FIELD(policy));
OBCX_DOMAIN_FIELDS(ReadMediaRequest, OBCX_DOMAIN_FIELD(media),
                   OBCX_DOMAIN_FIELD(maximum_bytes));
OBCX_DOMAIN_FIELDS(MediaCapability, OBCX_DOMAIN_FIELD(kind),
                   OBCX_DOMAIN_FIELD(source), OBCX_DOMAIN_FIELD(mime_types),
                   OBCX_DOMAIN_FIELD(captions),
                   OBCX_DOMAIN_FIELD(maximum_bytes),
                   OBCX_DOMAIN_FIELD(maximum_width),
                   OBCX_DOMAIN_FIELD(maximum_height));
OBCX_DOMAIN_FIELDS(DeliveryCombination, OBCX_DOMAIN_FIELD(delivery),
                   OBCX_DOMAIN_FIELD(media_kinds),
                   OBCX_DOMAIN_FIELD(text_blocks), OBCX_DOMAIN_FIELD(captions),
                   OBCX_DOMAIN_FIELD(mixed_media),
                   OBCX_DOMAIN_FIELD(maximum_messages),
                   OBCX_DOMAIN_FIELD(minimum_group_size),
                   OBCX_DOMAIN_FIELD(maximum_group_size));
OBCX_DOMAIN_FIELDS(ConversationCapabilities, OBCX_DOMAIN_FIELD(conversation),
                   OBCX_DOMAIN_FIELD(operations),
                   OBCX_DOMAIN_FIELD(text_styles), OBCX_DOMAIN_FIELD(media),
                   OBCX_DOMAIN_FIELD(combinations),
                   OBCX_DOMAIN_FIELD(maximum_text_utf8_bytes),
                   OBCX_DOMAIN_FIELD(maximum_caption_utf8_bytes),
                   OBCX_DOMAIN_FIELD(maximum_media_total_bytes),
                   OBCX_DOMAIN_FIELD(may_send), OBCX_DOMAIN_FIELD(may_delete));
OBCX_DOMAIN_FIELDS(GetCapabilitiesRequest, OBCX_DOMAIN_FIELD(conversation));
template <>
struct OperationTraits<GetMemberRequest>
    : OperationContract<GetMemberRequest, MemberInfo, false> {
  static auto supports_surface(const SurfaceId &s) -> bool {
    return domain_supports_surface(s);
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.conversation != r.conversation || v.user != r.user)
      throw std::invalid_argument("member result scope mismatch");
  }
};
template <>
struct OperationTraits<ResolveReplyRequest>
    : OperationContract<ResolveReplyRequest, ReplyInfo, false> {
  static auto supports_surface(const SurfaceId &s) -> bool {
    return domain_supports_surface(s);
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.reply.source().conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (!v.reference() || *v.reference() != r.reply)
      throw std::invalid_argument("reply result evidence mismatch");
  }
};
template <>
struct OperationTraits<ReadMediaRequest>
    : OperationContract<ReadMediaRequest, MediaBlob, false> {
  static auto supports_surface(const SurfaceId &s) -> bool {
    return domain_supports_surface(s);
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.media.installation;
  }
  static auto decode_result(const request_type &r, const Json &payload)
      -> result_type {
    detail::validate_binary_budget(payload, r.maximum_bytes, r.maximum_bytes);
    return GatewayCodec<MediaBlob>::decode(payload);
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.source.bytes.size() > r.maximum_bytes ||
        (v.identity && v.identity->installation != r.media.installation))
      throw std::invalid_argument("media result scope or byte bound mismatch");
  }
};
template <>
struct OperationTraits<GetCapabilitiesRequest>
    : OperationContract<GetCapabilitiesRequest, ConversationCapabilities,
                        false> {
  static auto supports_surface(const SurfaceId &s) -> bool {
    return domain_supports_surface(s);
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.conversation != r.conversation)
      throw std::invalid_argument("capability scope mismatch");
  }
};
} // namespace obcx::bot
