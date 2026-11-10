#include "onebot11/bot/recipe.hpp"
#include "core/bot/configuration_fingerprint.hpp"
#include "onebot11/bot/capability_ids.hpp"
#include "onebot11/bot/command_adapter.hpp"
#include "onebot11/bot/configuration.hpp"
#include "onebot11/bot/event_ingress.hpp"
#include "onebot11/bot/operation_component.hpp"
#include "onebot11/bot/operation_definitions.hpp"
#include "onebot11/bot/platform_adapter.hpp"
#include "onebot11/bot/protocol.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include "onebot11/bot/transport.hpp"
#include "standard_actor_events.hpp"

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
               .provides = {core::CapabilityId{std::string{events}},
                            core::CapabilityId{std::string{
                                standard::standard_event_capability_v1}},
                            core::CapabilityId{std::string{
                                core::ActorServiceContributor::capability_id}}},
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
               core::BotRecipeDescription description, std::string digest,
               std::shared_ptr<const core::PlatformAdapter> binding)
    -> std::shared_ptr<const core::BotInstallationPlan> {
  const obcx::bot::BotInstallationRef actor_event_installation{
      input.installation_id, input.surface};
  actor_event_installation.validate();
  return std::make_shared<core::BotInstallationPlan>(
      [connection = std::move(connection), id = input.installation_id,
       binding](boost::asio::io_context &executor,
                std::shared_ptr<core::BoundedFileReader>) -> auto {
        std::vector<std::unique_ptr<core::BotComponent>> components;
        components.push_back(
            std::make_unique<core::OneBot11ProtocolComponent>());
        components.push_back(
            std::make_unique<TransportComponent>(executor, connection));
        components.push_back(
            std::make_unique<core::OneBot11EventIngressComponent>(
                executor.get_executor(), id));
        components.push_back(
            std::make_unique<core::OneBot11OperationsComponent>(
                id, executor.get_executor(),
                connection.self_identity_refresh_interval, binding,
                connection.action_timeout));
        return components;
      },
      common::BotInstallationMetadata{.installation_id = input.installation_id,
                                      .enabled = input.enabled,
                                      .surface = input.surface,
                                      .transport = input.transport,
                                      .ingress_platform = "qq",
                                      .command_target = ""},
      std::move(description), std::move(digest), make_command_adapter(),
      [installation = actor_event_installation](
          std::weak_ptr<core::BotInstallationDirectory> directory)
          -> std::shared_ptr<core::ActorServiceContributor> {
        return standard::detail::
            make_planned_standard_actor_service_contributor(
                installation, std::move(directory));
      });
}

} // namespace

void register_recipes(core::BotPlatformCatalog &catalog) {
  const auto binding = std::make_shared<QqPlatformAdapter>();
  catalog.register_recipe(
      {.surface = surface,
       .transport = "websocket",
       .ingress_platform = "qq",
       .parse =
           [binding](const core::BotInstallationInput &input,
                     const toml::table &connection, std::string_view path) {
             auto typed = configuration::parse_websocket(connection, path);
             return make_plan<core::OneBot11WebSocketTransportComponent>(
                 input, std::move(typed), onebot_websocket_recipe(),
                 core::configuration_digest(connection), binding);
           },
       .platform_adapter = binding});
  catalog.register_recipe(
      {.surface = surface,
       .transport = "http",
       .ingress_platform = "qq",
       .parse =
           [binding](const core::BotInstallationInput &input,
                     const toml::table &connection, std::string_view path) {
             auto typed = configuration::parse_http(connection, path);
             return make_plan<core::OneBot11HttpTransportComponent>(
                 input, std::move(typed), onebot_http_recipe(),
                 core::configuration_digest(connection), binding);
           },
       .platform_adapter = binding});
}

} // namespace obcx::onebot11::bot
