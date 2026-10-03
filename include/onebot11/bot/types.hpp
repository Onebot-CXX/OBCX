#ifndef OBCX_INCLUDE_ONEBOT11_BOT_TYPES_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_TYPES_HPP_

#include "core/bot/references.hpp"
#include "onebot11/bot/actions.hpp"

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
} // namespace detail

struct OneBotGroupMember {
  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> OneBotGroupMember;

  GroupTarget target;
  std::string user_id;
  std::string nickname;
  std::string card;
  std::string title;

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
  }

  auto operator==(const OneBotGroupMember &) const -> bool = default;
};

inline void to_json(Json &document, const OneBotGroupMember &member) {
  member.validate();
  document = {{"target", member.target},
              {"user_id", member.user_id},
              {"nickname", member.nickname},
              {"card", member.card},
              {"title", member.title}};
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

#endif // OBCX_INCLUDE_ONEBOT11_BOT_TYPES_HPP_
