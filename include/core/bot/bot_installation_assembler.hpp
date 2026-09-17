#ifndef OBCX_INCLUDE_CORE_BOT_INSTALLATION_ASSEMBLER_HPP_
#define OBCX_INCLUDE_CORE_BOT_INSTALLATION_ASSEMBLER_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/capability_ids.hpp"
#include "core/bot/installation_plan.hpp"

namespace obcx::core {

class BotInstallationAssembler {
public:
  [[nodiscard]] static auto describe(const BotInstallationPlan &plan) noexcept
      -> const BotRecipeDescription & {
    return plan.recipe();
  }
  [[nodiscard]] static auto validate(const BotInstallationPlan &plan)
      -> ComponentRecipeValidation;
  [[nodiscard]] static auto assemble(const BotInstallationPlan &plan)
      -> std::unique_ptr<BotInstallation>;
};

} // namespace obcx::core

#endif
