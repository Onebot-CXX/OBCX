#include "core/bot/bot_installation_assembler.hpp"

namespace obcx::core {

auto BotInstallationAssembler::validate(const BotInstallationPlan &plan)
    -> ComponentRecipeValidation {
  return validate_component_recipe(plan.recipe().components);
}

auto BotInstallationAssembler::assemble(const BotInstallationPlan &plan)
    -> std::unique_ptr<BotInstallation> {
  (void)validate(plan);
  const auto &metadata = plan.metadata();
  auto installation = std::make_unique<BotInstallation>(
      metadata.installation_id, metadata.surface);
  for (auto &component : plan.create_components(installation->executor())) {
    installation->add_component(std::move(component));
  }
  installation->assemble();
  return installation;
}

} // namespace obcx::core
