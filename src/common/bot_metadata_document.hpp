#pragma once

#include "common/bot_installation_metadata.hpp"
#include <toml++/toml.hpp>
#include <vector>

namespace obcx::common::detail {

// Reconstruct an allowlisted document rather than selectively redacting a
// credential-bearing input. This is also used by the Actor-only SDK builder.
inline auto bot_metadata_document(
    const std::vector<BotInstallationMetadata> &bots) -> toml::table {
  toml::table result;
  for (const auto &bot : bots) {
    bot.validate();
    if (result.contains(bot.installation_id)) {
      throw std::invalid_argument("duplicate installation metadata");
    }
    result.insert(bot.installation_id,
                  toml::table{{"enabled", bot.enabled},
                              {"surface", bot.surface.value()},
                              {"transport", bot.transport},
                              {"ingress_platform", bot.ingress_platform},
                              {"command_target", bot.command_target}});
  }
  return result;
}

} // namespace obcx::common::detail
