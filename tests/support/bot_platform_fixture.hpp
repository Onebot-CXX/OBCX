#pragma once

#include "../../src/app/builtin_bot_platforms.hpp"
#include "core/bot/configuration_error.hpp"
#include "core/runtime/process_configuration.hpp"

namespace obcx::test {

// Shared identity only within this test process, never a runtime service
// locator.
inline auto bot_platform_catalog()
    -> const std::shared_ptr<const core::BotPlatformCatalog> & {
  static const auto catalog = app::make_builtin_bot_platform_catalog();
  return catalog;
}

// Explicit synthetic connection values, not configuration defaults.
inline auto connection_fixture(const bot::SurfaceId &surface,
                               std::string_view transport) -> toml::table {
  if (surface == bot::SurfaceId{"onebot11.qq"} && transport == "websocket") {
    return toml::table{{"host", "localhost"},
                       {"port", 3001},
                       {"access_token", ""},
                       {"connect_timeout_ms", 5000},
                       {"action_timeout_ms", 30000}};
  }
  if (surface == bot::SurfaceId{"onebot11.qq"} && transport == "http") {
    return toml::table{
        {"host", "localhost"},        {"port", 3000},
        {"access_token", ""},         {"connect_timeout_ms", 5000},
        {"action_timeout_ms", 30000}, {"use_tls", false},
        {"poll_interval_ms", 1000}};
  }
  if (surface == bot::SurfaceId{"telegram.bot_api"} && transport == "http") {
    return toml::table{{"host", "api.telegram.org"},
                       {"port", 443},
                       {"access_token", "YOUR_TELEGRAM_TOKEN"},
                       {"bot_username", "fixture_bot"},
                       {"use_tls", true},
                       {"connect_timeout_ms", 5000},
                       {"action_timeout_ms", 30000},
                       {"poll_timeout_ms", 25000},
                       {"poll_force_close_ms", 30000},
                       {"poll_retry_interval_ms", 3000}};
  }
  throw std::invalid_argument(
      "no synthetic connection fixture for this recipe");
}

inline auto installation_plan(const bot::SurfaceId &surface,
                              std::string transport, std::string id,
                              bool enabled)
    -> std::shared_ptr<const core::BotInstallationPlan> {
  const auto connection = connection_fixture(surface, transport);
  const auto path = "bots." + id;
  return bot_platform_catalog()->parse(
      {std::move(id), enabled, surface, std::move(transport)}, connection,
      path);
}

} // namespace obcx::test
