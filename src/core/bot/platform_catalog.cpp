#include "core/bot/platform_catalog.hpp"
#include "core/bot/configuration_error.hpp"

#include <algorithm>
#include <stdexcept>

namespace obcx::core {

void BotPlatformCatalog::register_recipe(BotRecipeRegistration registration) {
  if (sealed_) {
    throw std::logic_error("bot platform catalog is sealed");
  }
  registration.surface.validate();
  if (!bot::detail::valid_bot_id(registration.transport) ||
      !bot::detail::valid_bot_id(registration.ingress_platform) ||
      !registration.parse) {
    throw std::invalid_argument("incomplete bot recipe registration");
  }
  Key key{registration.surface, registration.transport};
  if (!recipes_.emplace(std::move(key), std::move(registration)).second) {
    throw std::invalid_argument("duplicate bot surface/transport recipe");
  }
}

void BotPlatformCatalog::seal() {
  if (sealed_) {
    throw std::logic_error("bot platform catalog is already sealed");
  }
  sealed_ = true;
}

void BotPlatformCatalog::require_sealed() const {
  if (!sealed_) {
    throw std::logic_error("bot platform catalog must be sealed before use");
  }
}

auto BotPlatformCatalog::supports(const bot::SurfaceId &surface) const -> bool {
  require_sealed();
  return std::ranges::any_of(recipes_, [&](const auto &entry) {
    return entry.first.first == surface;
  });
}

auto BotPlatformCatalog::supports(const bot::SurfaceId &surface,
                                  const std::string_view transport) const
    -> bool {
  require_sealed();
  return recipes_.contains(Key{surface, std::string{transport}});
}

auto BotPlatformCatalog::supports_ingress(const std::string_view platform) const
    -> bool {
  require_sealed();
  return std::ranges::any_of(recipes_, [&](const auto &entry) {
    return entry.second.ingress_platform == platform;
  });
}

auto BotPlatformCatalog::recipes() const
    -> std::vector<std::pair<bot::SurfaceId, std::string>> {
  require_sealed();
  std::vector<Key> result;
  for (const auto &[key, registration] : recipes_) {
    (void)registration;
    result.push_back(key);
  }
  return result;
}

auto BotPlatformCatalog::parse(const BotInstallationInput &input,
                               const toml::table &connection,
                               const std::string_view bot_path) const
    -> std::shared_ptr<const BotInstallationPlan> {
  require_sealed();
  const auto path = std::string{bot_path};
  if (!supports(input.surface)) {
    throw BotConfigurationError("unsupported_bot_surface", path + ".surface",
                                path + ".surface is not registered");
  }
  const auto entry = recipes_.find(Key{input.surface, input.transport});
  if (entry == recipes_.end()) {
    const auto known_transport =
        std::ranges::any_of(recipes_, [&](const auto &item) {
          return item.first.second == input.transport;
        });
    throw BotConfigurationError(
        known_transport ? "unsupported_bot_surface_transport"
                        : "unsupported_bot_transport",
        path + ".transport",
        path + ".transport has no registered recipe for this surface");
  }
  auto plan = entry->second.parse(input, connection, path + ".connection");
  if (!plan || plan->metadata().installation_id != input.installation_id ||
      plan->metadata().enabled != input.enabled ||
      plan->metadata().surface != input.surface ||
      plan->metadata().transport != input.transport ||
      plan->metadata().ingress_platform != entry->second.ingress_platform) {
    throw BotConfigurationError(
        "invalid_bot_installation_plan", path,
        path + " parser returned an inconsistent installation plan");
  }
  return plan;
}

} // namespace obcx::core
