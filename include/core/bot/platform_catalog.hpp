#ifndef OBCX_INCLUDE_CORE_BOT_PLATFORM_CATALOG_HPP_
#define OBCX_INCLUDE_CORE_BOT_PLATFORM_CATALOG_HPP_

#include "core/bot/installation_plan.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

namespace obcx::core {

struct BotInstallationInput {
  std::string installation_id;
  bool enabled;
  bot::SurfaceId surface;
  std::string transport;
};

struct BotRecipeRegistration {
  using Parser = std::function<std::shared_ptr<const BotInstallationPlan>(
      const BotInstallationInput &, const toml::table &, std::string_view)>;
  bot::SurfaceId surface;
  std::string transport;
  std::string ingress_platform;
  Parser parse;
};

// Construction is explicit and single-owner. Only sealed const catalogs are
// injected into loaders/runtime; no singleton or static platform discovery.
class BotPlatformCatalog final {
public:
  BotPlatformCatalog() = default;
  BotPlatformCatalog(const BotPlatformCatalog &) = delete;
  auto operator=(const BotPlatformCatalog &) -> BotPlatformCatalog & = delete;
  BotPlatformCatalog(BotPlatformCatalog &&) = delete;
  auto operator=(BotPlatformCatalog &&) -> BotPlatformCatalog & = delete;

  void register_recipe(BotRecipeRegistration registration);
  void seal();
  [[nodiscard]] auto sealed() const noexcept -> bool { return sealed_; }
  [[nodiscard]] auto supports(const bot::SurfaceId &surface) const -> bool;
  [[nodiscard]] auto supports(const bot::SurfaceId &surface,
                              std::string_view transport) const -> bool;
  [[nodiscard]] auto supports_ingress(std::string_view platform) const -> bool;
  [[nodiscard]] auto recipes() const
      -> std::vector<std::pair<bot::SurfaceId, std::string>>;
  [[nodiscard]] auto parse(const BotInstallationInput &input,
                           const toml::table &connection,
                           std::string_view bot_path) const
      -> std::shared_ptr<const BotInstallationPlan>;

private:
  void require_sealed() const;
  using Key = std::pair<bot::SurfaceId, std::string>;
  std::map<Key, BotRecipeRegistration> recipes_;
  bool sealed_{};
};

} // namespace obcx::core

#endif
