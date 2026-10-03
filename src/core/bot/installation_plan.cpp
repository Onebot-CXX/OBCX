#include "core/bot/installation_plan.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/configuration_fingerprint.hpp"
#include "core/command/command_platform_adapter.hpp"

#include <algorithm>
#include <stdexcept>

namespace obcx::core {
namespace {

auto normalized_descriptor(ComponentDescriptor descriptor)
    -> ComponentDescriptor {
  std::ranges::sort(descriptor.provides, {}, &CapabilityId::value);
  std::ranges::sort(descriptor.required, {}, &CapabilityId::value);
  return descriptor;
}

auto checked_recipe(BotRecipeDescription recipe,
                    const common::BotInstallationMetadata &metadata)
    -> BotRecipeDescription {
  metadata.validate();
  if (!bot::detail::valid_bot_id(recipe.recipe_id) ||
      recipe.surface != metadata.surface ||
      recipe.transport != metadata.transport) {
    throw std::invalid_argument("installation plan recipe identity mismatch");
  }
  (void)validate_component_recipe(recipe.components);
  for (auto &component : recipe.components) {
    component = normalized_descriptor(std::move(component));
  }
  std::ranges::sort(recipe.advertised_actions);
  for (const auto &action : recipe.advertised_actions) {
    action.validate();
  }
  if (std::ranges::adjacent_find(recipe.advertised_actions) !=
      recipe.advertised_actions.end()) {
    throw std::invalid_argument("duplicate action in installation plan");
  }
  return recipe;
}

auto plan_fingerprint(const common::BotInstallationMetadata &metadata,
                      const BotRecipeDescription &recipe,
                      const std::string &connection_digest) -> std::string {
  if (connection_digest.size() != SHA256_DIGEST_LENGTH * 2 ||
      !std::ranges::all_of(connection_digest, [](const char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
      })) {
    throw std::invalid_argument(
        "installation plan requires a connection digest");
  }
  nlohmann::json components = nlohmann::json::array();
  for (const auto &component : recipe.components) {
    std::vector<std::string> provides;
    std::vector<std::string> required;
    for (const auto &id : component.provides) {
      provides.push_back(id.value());
    }
    for (const auto &id : component.required) {
      required.push_back(id.value());
    }
    std::ranges::sort(provides);
    std::ranges::sort(required);
    components.push_back({{"id", component.id.value()},
                          {"provides", provides},
                          {"required", required}});
  }
  const nlohmann::json value = {
      {"installation_id", metadata.installation_id},
      {"enabled", metadata.enabled},
      {"surface", metadata.surface},
      {"transport", metadata.transport},
      {"ingress_platform", metadata.ingress_platform},
      {"command_target", metadata.command_target},
      {"recipe", recipe.recipe_id},
      {"components", components},
      {"actions", recipe.advertised_actions},
      {"connection_digest", connection_digest},
      {"command_publisher",
       recipe.command_publisher
           ? nlohmann::json(recipe.command_publisher->value())
           : nlohmann::json(nullptr)}};
  return configuration_digest(value.dump());
}

} // namespace

BotInstallationPlan::BotInstallationPlan(
    common::BotInstallationMetadata metadata, BotRecipeDescription recipe,
    std::string connection_digest, ComponentFactory factory,
    std::shared_ptr<ICommandPlatformAdapter> command_adapter)
    : metadata_(std::move(metadata)),
      recipe_(checked_recipe(std::move(recipe), metadata_)),
      fingerprint_(plan_fingerprint(metadata_, recipe_, connection_digest)),
      factory_(std::move(factory)),
      command_adapter_(std::move(command_adapter)) {
  if (!factory_) {
    throw std::invalid_argument(
        "installation plan requires a component factory");
  }
  if (command_adapter_ &&
      command_adapter_->platform() != metadata_.ingress_platform) {
    throw std::invalid_argument(
        "command adapter does not match installation metadata");
  }
  if (recipe_.command_publisher) {
    const auto provided =
        std::ranges::any_of(recipe_.components, [&](const auto &component) {
          return std::ranges::find(component.provides,
                                   *recipe_.command_publisher) !=
                 component.provides.end();
        });
    if (!provided || !command_adapter_ ||
        !command_adapter_->supports_catalog_publication()) {
      throw std::invalid_argument(
          "command publisher is not bound to an executable recipe");
    }
  }
}

BotInstallationPlan::~BotInstallationPlan() = default;

auto BotInstallationPlan::create_components(boost::asio::io_context &executor)
    const -> std::vector<std::unique_ptr<BotComponent>> {
  auto components = factory_(executor);
  if (components.size() != recipe_.components.size()) {
    throw BotComponentRuntimeError(
        "component factory does not match recipe manifest");
  }
  for (std::size_t index = 0; index < components.size(); ++index) {
    if (!components[index] ||
        normalized_descriptor(components[index]->descriptor()) !=
            recipe_.components[index]) {
      throw BotComponentRuntimeError(
          "component factory does not match recipe manifest");
    }
  }
  return components;
}

} // namespace obcx::core
