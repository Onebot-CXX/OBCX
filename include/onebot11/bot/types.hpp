#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/actions.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>

namespace obcx::onebot11::bot {

using obcx::bot::BotInstallationRef;
using obcx::bot::GroupTarget;
using obcx::bot::Json;

namespace detail {
using obcx::bot::detail::require_object;
using obcx::bot::detail::require_string;
using obcx::bot::detail::validate_identifier;

inline void require_onebot(const BotInstallationRef &installation,
                           const std::string_view type) {
  installation.validate();
  if (installation.surface != surface) {
    throw std::invalid_argument(std::string{type} + " requires onebot11.qq");
  }
}
inline void validate_user_id(const std::string_view user_id) {
  std::int64_t value = 0;
  const auto [end, error] =
      std::from_chars(user_id.data(), user_id.data() + user_id.size(), value);
  if (user_id.empty() || user_id.front() == '0' || value <= 0 ||
      error != std::errc{} || end != user_id.data() + user_id.size()) {
    throw std::invalid_argument("Invalid OneBot user_id");
  }
}
inline void validate_emoji_like(const obcx::bot::BotMessageRef &message,
                                const std::string_view emoji_id) {
  message.validate();
  require_onebot(message.group.installation, "OneBot message emoji like");
  validate_user_id(message.group.native_group_id);
  const auto &id = message.native_message_id;
  std::int64_t value = 0;
  const auto [end, error] =
      std::from_chars(id.data(), id.data() + id.size(), value);
  if (error != std::errc{} || end != id.data() + id.size()) {
    throw std::invalid_argument("Invalid OneBot message_id");
  }
  validate_identifier(emoji_id, "OneBot emoji_id", 20);
  if (!std::ranges::all_of(emoji_id,
                           [](char c) { return c >= '0' && c <= '9'; })) {
    throw std::invalid_argument("OneBot emoji_id must be decimal digits");
  }
}
} // namespace detail

struct OneBotMessageEmojiLikeResult {
  using obcx_bot_json_factory = void;
  obcx::bot::BotMessageRef message;
  std::string emoji_id;
  bool set;

  void validate() const { detail::validate_emoji_like(message, emoji_id); }

  static auto from_json(const Json &document) -> OneBotMessageEmojiLikeResult {
    obcx::bot::detail::require_only_keys(document,
                                         "OneBotMessageEmojiLikeResult",
                                         {"message", "emoji_id", "set"});
    OneBotMessageEmojiLikeResult result{
        .message = document.at("message").get<obcx::bot::BotMessageRef>(),
        .emoji_id = detail::require_string(document, "emoji_id",
                                           "OneBotMessageEmojiLikeResult"),
        .set = document.at("set").get<bool>()};
    result.validate();
    return result;
  }

  auto operator==(const OneBotMessageEmojiLikeResult &) const -> bool = default;
};

inline void to_json(Json &document,
                    const OneBotMessageEmojiLikeResult &result) {
  result.validate();
  document = {{"message", result.message},
              {"emoji_id", result.emoji_id},
              {"set", result.set}};
}

struct OneBotUserInfo {
  using obcx_bot_json_factory = void;
  BotInstallationRef installation;
  std::string user_id;
  std::string nickname;
  std::string sex;
  std::int64_t age;

  void validate() const {
    detail::require_onebot(installation, "OneBotUserInfo");
    detail::validate_user_id(user_id);
    if (!nickname.empty()) {
      detail::validate_identifier(nickname, "OneBot nickname", 1024);
    }
    if ((sex != "male" && sex != "female" && sex != "unknown") || age < 0) {
      throw std::invalid_argument("Invalid OneBot user profile");
    }
  }

  static auto from_json(const Json &document) -> OneBotUserInfo {
    detail::require_object(document, "OneBotUserInfo");
    const auto &age = document.at("age");
    if (!age.is_number_integer() ||
        (age.is_number_unsigned() && age.get<std::uint64_t>() > INT64_MAX)) {
      throw std::invalid_argument("OneBot age must be a signed-range integer");
    }
    OneBotUserInfo result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .user_id =
            detail::require_string(document, "user_id", "OneBotUserInfo"),
        .nickname = document.at("nickname").get<std::string>(),
        .sex = document.at("sex").get<std::string>(),
        .age = age.get<std::int64_t>()};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document, const OneBotUserInfo &value) {
  value.validate();
  document = {{"installation", value.installation},
              {"user_id", value.user_id},
              {"nickname", value.nickname},
              {"sex", value.sex},
              {"age", value.age}};
}

struct OneBotLoginInfo {
  using obcx_bot_json_factory = void;
  BotInstallationRef installation;
  std::string user_id;

  void validate() const {
    detail::require_onebot(installation, "OneBotLoginInfo");
    detail::validate_identifier(user_id, "OneBot self user_id", 20);
    if (user_id.front() == '0' || !std::ranges::all_of(user_id, [](char c) {
          return c >= '0' && c <= '9';
        })) {
      throw std::invalid_argument("Invalid OneBot self user_id");
    }
  }

  static auto from_json(const Json &document) -> OneBotLoginInfo {
    detail::require_object(document, "OneBotLoginInfo");
    OneBotLoginInfo result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .user_id =
            detail::require_string(document, "user_id", "OneBotLoginInfo")};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document, const OneBotLoginInfo &value) {
  value.validate();
  document = {{"installation", value.installation}, {"user_id", value.user_id}};
}

struct OneBotGroupMember {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> OneBotGroupMember;

  GroupTarget target;
  std::string user_id;
  std::string nickname;
  std::string card;
  std::string title;
  std::string role;
  std::string sex;
  std::optional<std::int64_t> age;
  std::string area;
  std::string level;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "OneBotGroupMember");
    detail::validate_identifier(user_id, "OneBot user_id");
    if (!nickname.empty()) {
      detail::validate_identifier(nickname, "OneBot nickname", 1024);
    }
    if (!card.empty()) {
      detail::validate_identifier(card, "OneBot card", 1024);
    }
    if (!title.empty()) {
      detail::validate_identifier(title, "OneBot title", 1024);
    }
    if ((!role.empty() && role != "owner" && role != "admin" &&
         role != "member") ||
        (!sex.empty() && sex != "male" && sex != "female" &&
         sex != "unknown") ||
        (age.has_value() && *age < 0)) {
      throw std::invalid_argument("Invalid OneBot member profile");
    }
    for (const auto *text : {&area, &level}) {
      if (!text->empty()) {
        detail::validate_identifier(*text, "OneBot member profile", 1024);
      }
    }
  }

  auto operator==(const OneBotGroupMember &) const -> bool = default;
};

inline void to_json(Json &document, const OneBotGroupMember &member) {
  member.validate();
  document = {{"target", member.target},     {"user_id", member.user_id},
              {"nickname", member.nickname}, {"card", member.card},
              {"title", member.title},       {"role", member.role},
              {"sex", member.sex},           {"area", member.area},
              {"level", member.level}};
  if (member.age.has_value()) {
    document["age"] = *member.age;
  }
}

inline void from_json(const Json &document, OneBotGroupMember &member) {
  detail::require_object(document, "OneBotGroupMember");
  if (!document.contains("target")) {
    throw std::invalid_argument("OneBotGroupMember requires target");
  }
  member.target = document.at("target").get<GroupTarget>();
  member.user_id =
      detail::require_string(document, "user_id", "OneBotGroupMember");
  member.nickname = document.value("nickname", std::string{});
  member.card = document.value("card", std::string{});
  member.title = document.value("title", std::string{});
  member.role = document.value("role", std::string{});
  member.sex = document.value("sex", std::string{});
  member.area = document.value("area", std::string{});
  member.level = document.value("level", std::string{});
  member.age.reset();
  if (document.contains("age")) {
    const auto &age = document.at("age");
    if (!age.is_number_integer() ||
        (age.is_number_unsigned() && age.get<std::uint64_t>() > INT64_MAX)) {
      throw std::invalid_argument("OneBot age must be a signed-range integer");
    }
    member.age = age.get<std::int64_t>();
  }
  member.validate();
}

inline auto OneBotGroupMember::from_json(const Json &document)
    -> OneBotGroupMember {
  detail::require_object(document, "OneBotGroupMember");
  if (!document.contains("target")) {
    throw std::invalid_argument("OneBotGroupMember requires target");
  }
  OneBotGroupMember result{.target = document.at("target").get<GroupTarget>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct OneBotForwardMessage {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> OneBotForwardMessage;

  BotInstallationRef installation;
  std::string forward_id;
  Json messages = Json::array();

  void validate() const {
    detail::require_onebot(installation, "OneBotForwardMessage");
    detail::validate_identifier(forward_id, "OneBot forward_id");
    if (!messages.is_array()) {
      throw std::invalid_argument("OneBot forwarded messages must be an array");
    }
    for (const auto &message : messages) {
      if (!message.is_object()) {
        throw std::invalid_argument(
            "OneBot forwarded message nodes must be objects");
      }
    }
  }

  auto operator==(const OneBotForwardMessage &) const -> bool = default;
};

inline void to_json(Json &document, const OneBotForwardMessage &message) {
  message.validate();
  document = {{"installation", message.installation},
              {"forward_id", message.forward_id},
              {"messages", message.messages}};
}

inline void from_json(const Json &document, OneBotForwardMessage &message) {
  detail::require_object(document, "OneBotForwardMessage");
  if (!document.contains("installation") || !document.contains("messages")) {
    throw std::invalid_argument(
        "OneBotForwardMessage requires installation and messages");
  }
  message.installation = document.at("installation").get<BotInstallationRef>();
  message.forward_id =
      detail::require_string(document, "forward_id", "OneBotForwardMessage");
  message.messages = document.at("messages");
  message.validate();
}

inline auto OneBotForwardMessage::from_json(const Json &document)
    -> OneBotForwardMessage {
  detail::require_object(document, "OneBotForwardMessage");
  if (!document.contains("installation")) {
    throw std::invalid_argument("OneBotForwardMessage requires installation");
  }
  OneBotForwardMessage result{
      .installation = document.at("installation").get<BotInstallationRef>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct ResolvedOneBotGroupFile {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> ResolvedOneBotGroupFile;

  GroupTarget target;
  std::string file_id;
  std::string url;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "ResolvedOneBotGroupFile");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
    detail::validate_identifier(url, "OneBot file URL", 16384);
  }

  auto operator==(const ResolvedOneBotGroupFile &) const -> bool = default;
};

inline void to_json(Json &document, const ResolvedOneBotGroupFile &file) {
  file.validate();
  document = {
      {"target", file.target}, {"file_id", file.file_id}, {"url", file.url}};
}

inline void from_json(const Json &document, ResolvedOneBotGroupFile &file) {
  detail::require_object(document, "ResolvedOneBotGroupFile");
  if (!document.contains("target")) {
    throw std::invalid_argument("ResolvedOneBotGroupFile requires target");
  }
  file.target = document.at("target").get<GroupTarget>();
  file.file_id =
      detail::require_string(document, "file_id", "ResolvedOneBotGroupFile");
  file.url = detail::require_string(document, "url", "ResolvedOneBotGroupFile");
  file.validate();
}

inline auto ResolvedOneBotGroupFile::from_json(const Json &document)
    -> ResolvedOneBotGroupFile {
  detail::require_object(document, "ResolvedOneBotGroupFile");
  if (!document.contains("target")) {
    throw std::invalid_argument("ResolvedOneBotGroupFile requires target");
  }
  ResolvedOneBotGroupFile result{.target =
                                     document.at("target").get<GroupTarget>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct ResolvedOneBotPrivateFile {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> ResolvedOneBotPrivateFile;

  BotInstallationRef installation;
  std::string user_id;
  std::string file_id;
  std::string url;

  void validate() const {
    detail::require_onebot(installation, "ResolvedOneBotPrivateFile");
    detail::validate_identifier(user_id, "OneBot user_id");
    detail::validate_identifier(file_id, "OneBot file_id", 2048);
    detail::validate_identifier(url, "OneBot file URL", 16384);
  }

  auto operator==(const ResolvedOneBotPrivateFile &) const -> bool = default;
};

inline void to_json(Json &document, const ResolvedOneBotPrivateFile &file) {
  file.validate();
  document = {{"installation", file.installation},
              {"user_id", file.user_id},
              {"file_id", file.file_id},
              {"url", file.url}};
}

inline void from_json(const Json &document, ResolvedOneBotPrivateFile &file) {
  detail::require_object(document, "ResolvedOneBotPrivateFile");
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "ResolvedOneBotPrivateFile requires installation");
  }
  file.installation = document.at("installation").get<BotInstallationRef>();
  file.user_id =
      detail::require_string(document, "user_id", "ResolvedOneBotPrivateFile");
  file.file_id =
      detail::require_string(document, "file_id", "ResolvedOneBotPrivateFile");
  file.url =
      detail::require_string(document, "url", "ResolvedOneBotPrivateFile");
  file.validate();
}

inline auto ResolvedOneBotPrivateFile::from_json(const Json &document)
    -> ResolvedOneBotPrivateFile {
  detail::require_object(document, "ResolvedOneBotPrivateFile");
  if (!document.contains("installation")) {
    throw std::invalid_argument(
        "ResolvedOneBotPrivateFile requires installation");
  }
  ResolvedOneBotPrivateFile result{
      .installation = document.at("installation").get<BotInstallationRef>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

struct OneBotGroupPokeResult {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> OneBotGroupPokeResult;

  GroupTarget target;
  std::string user_id;

  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "OneBotGroupPokeResult");
    detail::validate_identifier(user_id, "OneBot user_id");
  }

  auto operator==(const OneBotGroupPokeResult &) const -> bool = default;
};

inline void to_json(Json &document, const OneBotGroupPokeResult &result) {
  result.validate();
  document = {{"target", result.target}, {"user_id", result.user_id}};
}

inline void from_json(const Json &document, OneBotGroupPokeResult &result) {
  detail::require_object(document, "OneBotGroupPokeResult");
  if (!document.contains("target")) {
    throw std::invalid_argument("OneBotGroupPokeResult requires target");
  }
  result.target = document.at("target").get<GroupTarget>();
  result.user_id =
      detail::require_string(document, "user_id", "OneBotGroupPokeResult");
  result.validate();
}

inline auto OneBotGroupPokeResult::from_json(const Json &document)
    -> OneBotGroupPokeResult {
  detail::require_object(document, "OneBotGroupPokeResult");
  if (!document.contains("target")) {
    throw std::invalid_argument("OneBotGroupPokeResult requires target");
  }
  OneBotGroupPokeResult result{.target =
                                   document.at("target").get<GroupTarget>()};
  obcx::onebot11::bot::from_json(document, result);
  return result;
}

} // namespace obcx::onebot11::bot
