#pragma once

// Process-only construction and ACL configuration projection.
#include "common/config_snapshot.hpp"
#include "core/actor/command_scope.hpp"
#include "core/bot/conversation_resolver.hpp"
#include "core/command/command_coordinator.hpp"

namespace obcx::core {

class CommandScopeResolverFactory final {
public:
  [[nodiscard]] static auto make(
      std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot,
      std::shared_ptr<bot::ConversationResolver> conversations)
      -> std::shared_ptr<command::CommandScopeResolver>;
};

[[nodiscard]] auto compile_command_policy(
    const common::RuntimeConfigSnapshot &snapshot,
    const bot::ConversationResolver &conversations,
    const common::CommandGroupPolicyConfig &groups,
    const common::CommandUserPolicyConfig &users)
    -> std::optional<ActiveCommandPolicy>;

} // namespace obcx::core
