#ifndef OBCX_INCLUDE_COMMON_BOT_INSTALLATION_METADATA_HPP_
#define OBCX_INCLUDE_COMMON_BOT_INSTALLATION_METADATA_HPP_

#include "core/bot/references.hpp"

#include <stdexcept>
#include <string>

namespace obcx::common {

// Actor-visible values only. Connection configuration and assembly behavior
// belong to a process-owned platform plan, never to this SDK value.
struct BotInstallationMetadata {
  std::string installation_id;
  bool enabled;
  bot::SurfaceId surface;
  std::string transport;
  std::string ingress_platform;
  std::string command_target;

  void validate() const {
    bot::BotInstallationRef{.installation_id = installation_id,
                            .surface = surface}
        .validate();
    if (!bot::detail::valid_bot_id(transport) ||
        !bot::detail::valid_bot_id(ingress_platform)) {
      throw std::invalid_argument("invalid bot transport or ingress metadata");
    }
  }

  auto operator==(const BotInstallationMetadata &) const -> bool = default;
};

} // namespace obcx::common

#endif
