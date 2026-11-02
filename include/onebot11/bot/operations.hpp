#pragma once
#include "onebot11/bot/group_forward.hpp"
#include "onebot11/bot/types.hpp"
#include "onebot11/bot/user_target.hpp"
namespace obcx::onebot11::bot {
struct SetOneBotMessageEmojiLikeRequest {
  inline static const auto &action = actions::set_message_emoji_like;
  MessageRef message;
  std::string emoji_id;
  bool set;
  void validate() const { detail::validate_emoji_like(message, emoji_id); }
};
struct GetOneBotUserInfoRequest {
  inline static const auto &action = actions::get_user_info;
  UserTarget user;
  bool no_cache;
  void validate() const { validate_user_target(user); }
};
struct GetOneBotGroupMemberRequest {
  inline static const auto &action = actions::get_group_member;
  ConversationRef target;
  UserTarget user;
  bool no_cache;
  void validate() const {
    target.validate();
    validate_user_target(user);
    if (target.installation != user_target_installation(user))
      throw std::invalid_argument("QQ member query installation mismatch");
  }
};
// Read-only QQ file-ID acquisition. The native ID is an opaque provider value,
// not a path or URL to open. Bridge/caller retains filename/cache/path policy.
struct ReadOneBotFileRequest {
  inline static const obcx::bot::ActionId action{"onebot11.file.read"};
  ConversationRef conversation;
  std::string file_id;
  std::size_t maximum_bytes;
  void validate() const {
    conversation.validate();
    detail::require_onebot(conversation.installation, "QQ file conversation");
    detail::validate_identifier(file_id, "QQ file identifier", 1024);
    obcx::bot::detail::validate_utf8(file_id);
    if (file_id.find("://") != std::string::npos || file_id.starts_with("//"))
      throw std::invalid_argument("QQ file identifier is not a URL");
    if (!maximum_bytes ||
        maximum_bytes > obcx::bot::detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid QQ file read byte limit");
  }
};
struct ResolveOneBotGroupFileRequest {
  inline static const auto &action = actions::resolve_group_file;
  MediaRef media;
  void validate() const {
    media.validate();
    detail::require_onebot(media.installation, "QQ group file");
  }
};
struct ResolveOneBotPrivateFileRequest {
  inline static const auto &action = actions::resolve_private_file;
  MediaRef media;
  void validate() const {
    media.validate();
    detail::require_onebot(media.installation, "QQ private file");
  }
};
struct PokeOneBotGroupRequest {
  inline static const auto &action = actions::poke_group;
  ConversationRef target;
  UserRef user;
  void validate() const { detail::user_scope(target, user); }
};
} // namespace obcx::onebot11::bot
namespace obcx::bot {
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ReadOneBotFileRequest> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "file_id", 1024, false);
    (void)detail::bounded_unsigned(d.at("maximum_bytes"),
                                   detail::domain_maximum_bytes);
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::ReadOneBotFileRequest,
                   OBCX_DOMAIN_FIELD(conversation), OBCX_DOMAIN_FIELD(file_id),
                   OBCX_DOMAIN_FIELD(maximum_bytes));
template <>
struct OperationTraits<onebot11::bot::ReadOneBotFileRequest>
    : OperationContract<onebot11::bot::ReadOneBotFileRequest, MediaBlob,
                        false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.kind != MediaKind::File || v.identity ||
        v.source.bytes.size() > r.maximum_bytes ||
        v.source.metadata.size_bytes != v.source.bytes.size())
      throw std::invalid_argument("QQ file read result mismatch");
  }
};
template <>
struct detail::DomainRecordPreflight<
    onebot11::bot::SetOneBotMessageEmojiLikeRequest> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "emoji_id", 20, false);
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::SetOneBotMessageEmojiLikeRequest,
                   OBCX_DOMAIN_FIELD(message), OBCX_DOMAIN_FIELD(emoji_id),
                   OBCX_DOMAIN_FIELD(set));
template <>
struct OperationTraits<onebot11::bot::SetOneBotMessageEmojiLikeRequest>
    : OperationContract<onebot11::bot::SetOneBotMessageEmojiLikeRequest,
                        onebot11::bot::OneBotMessageEmojiLikeResult, true> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.message.conversation.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.message != request.message ||
        result.emoji_id != request.emoji_id || result.set != request.set)
      throw std::invalid_argument("QQ result scope mismatch");
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::GetOneBotUserInfoRequest,
                   OBCX_DOMAIN_FIELD(user), OBCX_DOMAIN_FIELD(no_cache));
template <>
struct OperationTraits<onebot11::bot::GetOneBotUserInfoRequest>
    : OperationContract<onebot11::bot::GetOneBotUserInfoRequest,
                        onebot11::bot::OneBotUserInfo, false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return onebot11::bot::user_target_installation(request.user);
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (!onebot11::bot::user_target_matches(request.user, result.user))
      throw std::invalid_argument("QQ result scope mismatch");
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::GetOneBotGroupMemberRequest,
                   OBCX_DOMAIN_FIELD(target), OBCX_DOMAIN_FIELD(user),
                   OBCX_DOMAIN_FIELD(no_cache));
template <>
struct OperationTraits<onebot11::bot::GetOneBotGroupMemberRequest>
    : OperationContract<onebot11::bot::GetOneBotGroupMemberRequest,
                        onebot11::bot::OneBotGroupMember, false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.target != request.target ||
        !onebot11::bot::user_target_matches(request.user, result.user))
      throw std::invalid_argument("QQ result scope mismatch");
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::ResolveOneBotGroupFileRequest,
                   OBCX_DOMAIN_FIELD(media));
template <>
struct OperationTraits<onebot11::bot::ResolveOneBotGroupFileRequest>
    : OperationContract<onebot11::bot::ResolveOneBotGroupFileRequest,
                        onebot11::bot::ResolvedOneBotGroupFile, false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.media.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.media != request.media)
      throw std::invalid_argument("QQ result scope mismatch");
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::ResolveOneBotPrivateFileRequest,
                   OBCX_DOMAIN_FIELD(media));
template <>
struct OperationTraits<onebot11::bot::ResolveOneBotPrivateFileRequest>
    : OperationContract<onebot11::bot::ResolveOneBotPrivateFileRequest,
                        onebot11::bot::ResolvedOneBotPrivateFile, false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.media.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.media != request.media)
      throw std::invalid_argument("QQ result scope mismatch");
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::PokeOneBotGroupRequest,
                   OBCX_DOMAIN_FIELD(target), OBCX_DOMAIN_FIELD(user));
template <>
struct OperationTraits<onebot11::bot::PokeOneBotGroupRequest>
    : OperationContract<onebot11::bot::PokeOneBotGroupRequest,
                        onebot11::bot::OneBotGroupPokeResult, true> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.target.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.target != request.target || result.user != request.user)
      throw std::invalid_argument("QQ result scope mismatch");
  }
};
} // namespace obcx::bot
