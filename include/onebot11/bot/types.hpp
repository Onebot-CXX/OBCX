#pragma once
#include "core/bot/domain_events.hpp"
#include "onebot11/bot/actions.hpp"
#include <charconv>

namespace obcx::onebot11::bot {
using obcx::bot::BotInstallationRef;
using obcx::bot::ConversationRef;
using obcx::bot::Json;
using obcx::bot::MediaRef;
using obcx::bot::MessageRef;
using obcx::bot::UserRef;
namespace detail {
using obcx::bot::detail::require_object;
using obcx::bot::detail::require_string;
using obcx::bot::detail::validate_identifier;
inline void preflight_text(const Json &document, std::string_view key,
                           std::size_t maximum, bool allow_empty) {
  const auto &field = document.at(std::string(key));
  if (!field.is_string())
    throw std::invalid_argument("QQ field must be a string");
  const auto &text = field.get_ref<const std::string &>();
  if (text.size() > maximum || (!allow_empty && text.empty()))
    throw std::invalid_argument("QQ field exceeds native bound");
  if (!text.empty())
    validate_identifier(text, key, maximum);
  obcx::bot::detail::validate_utf8(text);
}
inline void preflight_array(const Json &document, std::string_view key,
                            std::size_t maximum, bool allow_empty) {
  const auto &field = document.at(std::string(key));
  if (!field.is_array() || field.size() > maximum ||
      (!allow_empty && field.empty()))
    throw std::invalid_argument("QQ collection exceeds native bound");
}
inline void require_onebot(const BotInstallationRef &installation,
                           std::string_view) {
  installation.validate();
  if (installation.surface != surface)
    throw std::invalid_argument("expected onebot11.qq installation");
}
inline void validate_user_id(std::string_view text) {
  std::int64_t n{};
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), n);
  if (error != std::errc{} || end != text.data() + text.size() || n <= 0 ||
      std::to_string(n) != text)
    throw std::invalid_argument("invalid QQ user identity");
}
inline void user_scope(const ConversationRef &target, const UserRef &user) {
  target.validate();
  user.validate();
  require_onebot(target.installation, "QQ scope");
  if (target.installation != user.installation)
    throw std::invalid_argument("QQ user installation mismatch");
}
inline void validate_emoji_like(const MessageRef &message,
                                std::string_view emoji) {
  message.validate();
  require_onebot(message.conversation.installation, "QQ reaction");
  validate_identifier(emoji, "QQ emoji", 20);
  if (!std::ranges::all_of(emoji, [](char c) { return c >= '0' && c <= '9'; }))
    throw std::invalid_argument("invalid QQ emoji");
}
} // namespace detail
struct OneBotMessageEmojiLikeResult {
  MessageRef message;
  std::string emoji_id;
  bool set;
  void validate() const { detail::validate_emoji_like(message, emoji_id); }
};
struct OneBotUserInfo {
  UserRef user;
  std::string nickname;
  std::string sex;
  std::int64_t age;
  void validate() const {
    user.validate();
    detail::require_onebot(user.installation, "QQ user");
    if (age < 0 || (sex != "male" && sex != "female" && sex != "unknown"))
      throw std::invalid_argument("invalid QQ profile");
    if (!nickname.empty())
      detail::validate_identifier(nickname, "nickname", 1024);
  }
};
struct OneBotGroupMember {
  ConversationRef target;
  UserRef user;
  std::string nickname, card, title, role, sex;
  std::optional<std::int64_t> age;
  std::string area, level;
  void validate() const {
    detail::user_scope(target, user);
    if ((age && *age < 0) ||
        (!role.empty() && role != "owner" && role != "admin" &&
         role != "member") ||
        (!sex.empty() && sex != "male" && sex != "female" && sex != "unknown"))
      throw std::invalid_argument("invalid QQ member");
    for (const auto *v : {&nickname, &card, &title, &area, &level})
      if (!v->empty())
        detail::validate_identifier(*v, "QQ profile", 1024);
  }
};
struct ResolvedOneBotGroupFile {
  MediaRef media;
  std::string url;
  void validate() const {
    media.validate();
    detail::require_onebot(media.installation, "QQ file");
    obcx::bot::HttpSource{url, {}}.validate();
  }
};
struct ResolvedOneBotPrivateFile {
  MediaRef media;
  std::string url;
  void validate() const {
    media.validate();
    detail::require_onebot(media.installation, "QQ file");
    obcx::bot::HttpSource{url, {}}.validate();
  }
};
struct OneBotGroupPokeResult {
  ConversationRef target;
  UserRef user;
  void validate() const { detail::user_scope(target, user); }
};
} // namespace obcx::onebot11::bot
namespace obcx::bot {
template <>
struct detail::DomainRecordPreflight<
    onebot11::bot::OneBotMessageEmojiLikeResult> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "emoji_id", 20, false);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::OneBotUserInfo> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "nickname", 1024, true);
    onebot11::bot::detail::preflight_text(d, "sex", 7, false);
    (void)detail::domain_decode<std::int64_t>(d.at("age"));
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::OneBotGroupMember> {
  static void validate(const Json &d) {
    for (auto key : {"nickname", "card", "title", "area", "level"})
      onebot11::bot::detail::preflight_text(d, key, 1024, true);
    onebot11::bot::detail::preflight_text(d, "sex", 7, true);
    onebot11::bot::detail::preflight_text(d, "role", 6, true);
    if (!d.at("age").is_null())
      (void)detail::domain_decode<std::int64_t>(d.at("age"));
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ResolvedOneBotGroupFile> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "url", 8192, false);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ResolvedOneBotPrivateFile>
    : detail::DomainRecordPreflight<onebot11::bot::ResolvedOneBotGroupFile> {};
OBCX_DOMAIN_FIELDS(onebot11::bot::OneBotMessageEmojiLikeResult,
                   OBCX_DOMAIN_FIELD(message), OBCX_DOMAIN_FIELD(emoji_id),
                   OBCX_DOMAIN_FIELD(set));
OBCX_DOMAIN_FIELDS(onebot11::bot::OneBotUserInfo, OBCX_DOMAIN_FIELD(user),
                   OBCX_DOMAIN_FIELD(nickname), OBCX_DOMAIN_FIELD(sex),
                   OBCX_DOMAIN_FIELD(age));
OBCX_DOMAIN_FIELDS(onebot11::bot::OneBotGroupMember, OBCX_DOMAIN_FIELD(target),
                   OBCX_DOMAIN_FIELD(user), OBCX_DOMAIN_FIELD(nickname),
                   OBCX_DOMAIN_FIELD(card), OBCX_DOMAIN_FIELD(title),
                   OBCX_DOMAIN_FIELD(role), OBCX_DOMAIN_FIELD(sex),
                   OBCX_DOMAIN_FIELD(age), OBCX_DOMAIN_FIELD(area),
                   OBCX_DOMAIN_FIELD(level));
OBCX_DOMAIN_FIELDS(onebot11::bot::ResolvedOneBotGroupFile,
                   OBCX_DOMAIN_FIELD(media), OBCX_DOMAIN_FIELD(url));
OBCX_DOMAIN_FIELDS(onebot11::bot::ResolvedOneBotPrivateFile,
                   OBCX_DOMAIN_FIELD(media), OBCX_DOMAIN_FIELD(url));
OBCX_DOMAIN_FIELDS(onebot11::bot::OneBotGroupPokeResult,
                   OBCX_DOMAIN_FIELD(target), OBCX_DOMAIN_FIELD(user));
} // namespace obcx::bot
