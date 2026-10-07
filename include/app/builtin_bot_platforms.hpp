#pragma once

#include "core/bot/platform_catalog.hpp"
#include "onebot11/bot/recipe.hpp"
#include "telegram/bot/recipe.hpp"

namespace obcx::app {

// This composition root is the only production code enumerating built-in
// modules. Call explicitly before configuration loading; never register during
// static init.
inline auto make_builtin_bot_platform_catalog()
    -> std::shared_ptr<const core::BotPlatformCatalog> {
  auto catalog = std::make_shared<core::BotPlatformCatalog>();
  onebot11::bot::register_recipes(*catalog);
  telegram::bot::register_recipes(*catalog);
  catalog->seal();
  return catalog;
}

} // namespace obcx::app
