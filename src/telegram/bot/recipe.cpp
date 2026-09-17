#include "telegram/bot/recipe.hpp"
#include "core/bot/configuration_fingerprint.hpp"
#include "telegram/bot/capability_ids.hpp"
#include "telegram/bot/command_adapter.hpp"
#include "telegram/bot/command_catalog_component.hpp"
#include "telegram/bot/configuration.hpp"
#include "telegram/bot/event_ingress.hpp"
#include "telegram/bot/operation_component.hpp"
#include "telegram/bot/operation_definitions.hpp"
#include "telegram/bot/protocol.hpp"
#include "telegram/bot/transport.hpp"

namespace obcx::telegram::bot {
namespace {

auto telegram_http_recipe() -> core::BotRecipeDescription {
  using capability_ids::command_catalog;
  using capability_ids::media_upload;
  using capability_ids::protocol;
  using capability_ids::transport;
  using core::bot_capability_ids::events;
  using core::bot_capability_ids::operations;
  return {
      .recipe_id = "telegram.bot_api.http",
      .surface = surface,
      .transport = "http",
      .components =
          {
              {.id = core::ComponentId{"telegram.protocol"},
               .provides = {core::CapabilityId{std::string{protocol}}},
               .required = {}},
              {.id = core::ComponentId{"telegram.transport.http"},
               .provides = {core::CapabilityId{std::string{transport}}},
               .required = {core::CapabilityId{std::string{protocol}}}},
              {.id = core::ComponentId{"telegram.event-ingress"},
               .provides = {core::CapabilityId{std::string{events}}},
               .required = {core::CapabilityId{std::string{protocol}},
                            core::CapabilityId{std::string{transport}}}},
              {.id = core::ComponentId{"telegram.media-upload"},
               .provides = {core::CapabilityId{std::string{media_upload}}},
               .required = {core::CapabilityId{std::string{transport}}}},
              {.id = core::ComponentId{"telegram.operations"},
               .provides = {core::CapabilityId{std::string{operations}}},
               .required = {core::CapabilityId{std::string{protocol}},
                            core::CapabilityId{std::string{transport}},
                            core::CapabilityId{std::string{media_upload}}}},
              {.id = core::ComponentId{"telegram.command-catalog"},
               .provides = {core::CapabilityId{std::string{command_catalog}}},
               .required = {core::CapabilityId{std::string{protocol}},
                            core::CapabilityId{std::string{transport}}}},
          },
      .advertised_actions = operation_actions(true),
      .command_publisher = core::CapabilityId{std::string{command_catalog}},
  };
}

} // namespace

void register_recipes(core::BotPlatformCatalog &catalog) {
  catalog.register_recipe(
      {surface, "http", "telegram",
       [](const core::BotInstallationInput &input,
          const toml::table &connection, std::string_view path) {
         const auto typed = configuration::parse_http(connection, path);
         return std::make_shared<core::BotInstallationPlan>(
             common::BotInstallationMetadata{
                 input.installation_id, input.enabled, input.surface,
                 input.transport, "telegram", typed.bot_username},
             telegram_http_recipe(), core::configuration_digest(connection),
             [typed,
              id = input.installation_id](boost::asio::io_context &executor) {
               std::vector<std::unique_ptr<core::BotComponent>> components;
               components.push_back(
                   std::make_unique<core::TelegramProtocolComponent>());
               components.push_back(
                   std::make_unique<core::TelegramHttpTransportComponent>(
                       executor, typed));
               components.push_back(
                   std::make_unique<core::TelegramEventIngressComponent>(
                       executor.get_executor(), id));
               components.push_back(
                   std::make_unique<core::TelegramMediaUploadComponent>());
               components.push_back(
                   std::make_unique<core::TelegramOperationsComponent>(id,
                                                                       true));
               components.push_back(
                   std::make_unique<core::TelegramCommandCatalogComponent>());
               return components;
             },
             make_command_adapter(typed.bot_username));
       }});
}

} // namespace obcx::telegram::bot
