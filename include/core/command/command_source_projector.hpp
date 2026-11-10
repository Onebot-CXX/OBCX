#pragma once

// Process-only capability of the actual registered command adapter. It does not
// extend either existing adapter or platform-adapter virtual layout.
#include "core/actor/actor.hpp"
#include "core/actor/command_availability.hpp"
#include "core/bot/bot_events.hpp"
#include "core/bot/operation_result.hpp"

namespace obcx::core {

struct CommandSourceProjection {
  bot::MessageReceived source;
  command::Subject subject;
};

class CommandSourceProjector {
public:
  virtual ~CommandSourceProjector() = default;
  // Current ACL selectors may name observed non-user sender bytes.
  // Validation is platform-owned and must not manufacture a UserRef.
  [[nodiscard]] virtual auto valid_acl_sender(
      const bot::BotInstallationRef &installation,
      std::string_view sender) const -> bool = 0;
  [[nodiscard]] virtual auto project_source(
      const MessageEnvelope &event,
      const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<CommandSourceProjection> = 0;
};

} // namespace obcx::core
