#ifndef OBCX_INCLUDE_CORE_BOT_REFERENCES_HPP_
#define OBCX_INCLUDE_CORE_BOT_REFERENCES_HPP_

#include "core/bot/ids.hpp"
#include "core/bot/json_codec.hpp"
#include "core/bot/validation.hpp"

#include <string>

namespace obcx::bot {

struct BotInstallationRef {
  std::string installation_id;
  SurfaceId surface;

  void validate() const {
    detail::validate_identifier(installation_id, "installation_id", 256);
    surface.validate();
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> BotInstallationRef {
    detail::require_object(document, "BotInstallationRef");
    if (!document.contains("surface")) {
      throw std::invalid_argument("BotInstallationRef requires surface");
    }
    BotInstallationRef result{
        .installation_id = detail::require_string(document, "installation_id",
                                                  "BotInstallationRef"),
        .surface = document.at("surface").get<SurfaceId>()};
    result.validate();
    return result;
  }

  auto operator==(const BotInstallationRef &) const -> bool = default;
};

struct GroupTarget {
  BotInstallationRef installation;
  std::string native_group_id;

  void validate() const {
    installation.validate();
    detail::validate_identifier(native_group_id, "native_group_id");
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> GroupTarget {
    detail::require_object(document, "GroupTarget");
    if (!document.contains("installation")) {
      throw std::invalid_argument("GroupTarget requires installation");
    }
    GroupTarget result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .native_group_id =
            detail::require_string(document, "native_group_id", "GroupTarget")};
    result.validate();
    return result;
  }

  auto operator==(const GroupTarget &) const -> bool = default;
};

struct PrivateTarget {
  BotInstallationRef installation;
  std::string native_user_id;

  void validate() const {
    installation.validate();
    detail::validate_identifier(native_user_id, "native_user_id");
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> PrivateTarget {
    detail::require_only_keys(document, "PrivateTarget",
                              {"installation", "native_user_id"});
    if (!document.contains("installation")) {
      throw std::invalid_argument("PrivateTarget requires installation");
    }
    PrivateTarget result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .native_user_id = detail::require_string(document, "native_user_id",
                                                 "PrivateTarget")};
    result.validate();
    return result;
  }

  auto operator==(const PrivateTarget &) const -> bool = default;
};

struct PrivateMessageRef {
  PrivateTarget target;
  std::string native_message_id;

  void validate() const {
    target.validate();
    detail::validate_identifier(native_message_id, "native_message_id");
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> PrivateMessageRef {
    detail::require_only_keys(document, "PrivateMessageRef",
                              {"target", "native_message_id"});
    if (!document.contains("target") || !document.at("target").is_object()) {
      throw std::invalid_argument("PrivateMessageRef requires target");
    }
    PrivateMessageRef result{
        .target = document.at("target").get<PrivateTarget>(),
        .native_message_id = detail::require_string(
            document, "native_message_id", "PrivateMessageRef")};
    result.validate();
    return result;
  }

  auto operator==(const PrivateMessageRef &) const -> bool = default;
};

struct BotMessageRef {
  GroupTarget group;
  std::string native_message_id;

  void validate() const {
    group.validate();
    detail::validate_identifier(native_message_id, "native_message_id");
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> BotMessageRef {
    detail::require_object(document, "BotMessageRef");
    if (!document.contains("group") || !document.at("group").is_object()) {
      throw std::invalid_argument("BotMessageRef requires group");
    }
    BotMessageRef result{.group = document.at("group").get<GroupTarget>(),
                         .native_message_id = detail::require_string(
                             document, "native_message_id", "BotMessageRef")};
    result.validate();
    return result;
  }

  auto operator==(const BotMessageRef &) const -> bool = default;
};

inline void to_json(Json &document, const BotInstallationRef &installation) {
  installation.validate();
  document = {{"installation_id", installation.installation_id},
              {"surface", installation.surface}};
}

inline void to_json(Json &document, const GroupTarget &target) {
  target.validate();
  document = {{"installation", target.installation},
              {"native_group_id", target.native_group_id}};
}

inline void to_json(Json &document, const PrivateTarget &target) {
  target.validate();
  document = {{"installation", target.installation},
              {"native_user_id", target.native_user_id}};
}

inline void to_json(Json &document, const PrivateMessageRef &message) {
  message.validate();
  document = {{"target", message.target},
              {"native_message_id", message.native_message_id}};
}

inline void to_json(Json &document, const BotMessageRef &message) {
  message.validate();
  document = {{"group", message.group},
              {"native_message_id", message.native_message_id}};
}

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_REFERENCES_HPP_
