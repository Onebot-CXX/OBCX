#include "onebot11/bot/recipe.hpp"
#include "core/bot/configuration_fingerprint.hpp"
#include "onebot11/bot/capability_ids.hpp"
#include "onebot11/bot/command_adapter.hpp"
#include "onebot11/bot/configuration.hpp"
#include "onebot11/bot/event_ingress.hpp"
#include "onebot11/bot/operation_component.hpp"
#include "onebot11/bot/operation_definitions.hpp"
#include "onebot11/bot/protocol.hpp"
#include "onebot11/bot/transport.hpp"

namespace obcx::onebot11::bot {
namespace {

auto onebot_websocket_recipe() -> core::BotRecipeDescription {
  using capability_ids::protocol;
  using capability_ids::transport;
  using core::bot_capability_ids::events;
  using core::bot_capability_ids::operations;
  return {
      .recipe_id = "onebot11.qq.websocket",
      .surface = surface,
      .transport = "websocket",
      .components =
          {
              {.id = core::ComponentId{"onebot11.protocol"},
               .provides = {core::CapabilityId{std::string{protocol}}},
               .required = {}},
              {.id = core::ComponentId{"onebot11.transport.websocket"},
               .provides = {core::CapabilityId{std::string{transport}}},
               .required = {core::CapabilityId{std::string{protocol}}}},
              {.id = core::ComponentId{"onebot11.event-ingress"},
               .provides = {core::CapabilityId{std::string{events}}},
               .required = {core::CapabilityId{std::string{protocol}},
                            core::CapabilityId{std::string{transport}}}},
              {.id = core::ComponentId{"onebot11.operations"},
               .provides = {core::CapabilityId{std::string{operations}}},
               .required = {core::CapabilityId{std::string{protocol}},
                            core::CapabilityId{std::string{transport}}}},
          },
      .advertised_actions = operation_actions(),
      .command_publisher = std::nullopt,
  };
}

auto onebot_http_recipe() -> core::BotRecipeDescription {
  auto recipe = onebot_websocket_recipe();
  recipe.recipe_id = "onebot11.qq.http";
  recipe.transport = "http";
  recipe.components[1].id = core::ComponentId{"onebot11.transport.http"};
  return recipe;
}

template <typename TransportComponent, typename Connection>
auto make_plan(const core::BotInstallationInput &input, Connection connection,
               core::BotRecipeDescription description, std::string digest)
    -> std::shared_ptr<const core::BotInstallationPlan> {
  return std::make_shared<core::BotInstallationPlan>(
      common::BotInstallationMetadata{.installation_id = input.installation_id,
                                      .enabled = input.enabled,
                                      .surface = input.surface,
                                      .transport = input.transport,
                                      .ingress_platform = "qq",
                                      .command_target = ""},
      std::move(description), std::move(digest),
      [connection = std::move(connection),
       id = input.installation_id](boost::asio::io_context &executor) -> auto {
        std::vector<std::unique_ptr<core::BotComponent>> components;
        components.push_back(
            std::make_unique<core::OneBot11ProtocolComponent>());
        components.push_back(
            std::make_unique<TransportComponent>(executor, connection));
        components.push_back(
            std::make_unique<core::OneBot11EventIngressComponent>(
                executor.get_executor(), id));
        components.push_back(
            std::make_unique<core::OneBot11OperationsComponent>(id));
        return components;
      },
      make_command_adapter());
}

} // namespace

void register_recipes(core::BotPlatformCatalog &catalog) {
  catalog.register_recipe(
      {.surface = surface,
       .transport = "websocket",
       .ingress_platform = "qq",
       .parse = [](const core::BotInstallationInput &input,
                   const toml::table &connection, std::string_view path) {
         auto typed = configuration::parse_websocket(connection, path);
         return make_plan<core::OneBot11WebSocketTransportComponent>(
             input, std::move(typed), onebot_websocket_recipe(),
             core::configuration_digest(connection));
       }});
  catalog.register_recipe(
      {.surface = surface,
       .transport = "http",
       .ingress_platform = "qq",
       .parse = [](const core::BotInstallationInput &input,
                   const toml::table &connection, std::string_view path) {
         auto typed = configuration::parse_http(connection, path);
         return make_plan<core::OneBot11HttpTransportComponent>(
             input, std::move(typed), onebot_http_recipe(),
             core::configuration_digest(connection));
       }});
}

} // namespace obcx::onebot11::bot
