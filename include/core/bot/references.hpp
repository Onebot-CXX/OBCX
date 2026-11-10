#pragma once

#include "core/bot/bot_ids.hpp"
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
    detail::validate_utf8(installation_id);
    surface.validate();
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> BotInstallationRef {
    detail::validate_bot_wire(document);
    detail::require_only_keys(document, "BotInstallationRef",
                              {"installation_id", "surface"});
    if (!document.contains("surface")) {
      throw std::invalid_argument("BotInstallationRef requires surface");
    }
    BotInstallationRef result{
        .installation_id = detail::require_identifier(
            document, "installation_id", "BotInstallationRef", 256),
        .surface = document.at("surface").get<SurfaceId>()};
    result.validate();
    return result;
  }

  auto operator==(const BotInstallationRef &) const -> bool = default;
};

inline void to_json(Json &document, const BotInstallationRef &installation) {
  installation.validate();
  document = {{"installation_id", installation.installation_id},
              {"surface", installation.surface}};
}

struct ConversationRef {
  using obcx_bot_json_factory = void;
  BotInstallationRef installation;
  ConversationId id;
  void validate() const {
    installation.validate();
    id.validate_surface(installation.surface);
  }
  static auto from_json(const Json &d) -> ConversationRef {
    detail::validate_bot_wire(d);
    detail::require_only_keys(d, "ConversationRef", {"installation", "id"});
    ConversationRef v{d.at("installation").get<BotInstallationRef>(),
                      d.at("id").get<ConversationId>()};
    v.validate();
    return v;
  }
  auto operator==(const ConversationRef &) const -> bool = default;
};
struct UserRef {
  using obcx_bot_json_factory = void;
  BotInstallationRef installation;
  UserId id;
  void validate() const {
    installation.validate();
    id.validate_surface(installation.surface);
  }
  static auto from_json(const Json &d) -> UserRef {
    detail::validate_bot_wire(d);
    detail::require_only_keys(d, "UserRef", {"installation", "id"});
    UserRef v{d.at("installation").get<BotInstallationRef>(),
              d.at("id").get<UserId>()};
    v.validate();
    return v;
  }
  auto operator==(const UserRef &) const -> bool = default;
};
struct MessageRef {
  using obcx_bot_json_factory = void;
  ConversationRef conversation;
  MessageId id;
  void validate() const {
    conversation.validate();
    id.validate_surface(conversation.installation.surface);
  }
  static auto from_json(const Json &d) -> MessageRef {
    detail::validate_bot_wire(d);
    detail::require_only_keys(d, "MessageRef", {"conversation", "id"});
    MessageRef v{d.at("conversation").get<ConversationRef>(),
                 d.at("id").get<MessageId>()};
    v.validate();
    return v;
  }
  auto operator==(const MessageRef &) const -> bool = default;
};
inline void to_json(Json &d, const ConversationRef &v) {
  v.validate();
  d = {{"installation", v.installation}, {"id", v.id}};
}
inline void to_json(Json &d, const UserRef &v) {
  v.validate();
  d = {{"installation", v.installation}, {"id", v.id}};
}
inline void to_json(Json &d, const MessageRef &v) {
  v.validate();
  d = {{"conversation", v.conversation}, {"id", v.id}};
}

} // namespace obcx::bot
