#include "core/bot/bot_event_components.hpp"
#include "core/bot/bot_installation_assembler.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/messaging.hpp"
#include "onebot11/bot/protocol.hpp"
#include "onebot11/bot/transport.hpp"
#include "support/bot_platform_fixture.hpp"
#include "telegram/bot/command_catalog_component.hpp"
#include "telegram/bot/protocol.hpp"
#include "telegram/bot/transport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using obcx::bot::ActionId;

using OneBot11HttpConnectionConfig =
    obcx::onebot11::configuration::HttpConnection;
using OneBot11WebSocketConnectionConfig =
    obcx::onebot11::configuration::WebSocketConnection;
using TelegramHttpConnectionConfig =
    obcx::telegram::configuration::HttpConnection;
using obcx::core::BotComponentRuntimeError;
using obcx::core::BotInstallationAssembler;
using obcx::core::BotRecipeDescription;

auto onebot_websocket_connection() -> OneBot11WebSocketConnectionConfig {
  return {.host = "localhost",
          .port = 3001,
          .access_token = "",
          .connect_timeout = std::chrono::milliseconds{5000},
          .action_timeout = std::chrono::milliseconds{30000}};
}

auto onebot_http_connection() -> OneBot11HttpConnectionConfig {
  return {.host = "localhost",
          .port = 3000,
          .access_token = "",
          .use_tls = false,
          .connect_timeout = std::chrono::milliseconds{5000},
          .action_timeout = std::chrono::milliseconds{30000},
          .poll_interval = std::chrono::milliseconds{1000}};
}

auto telegram_http_connection() -> TelegramHttpConnectionConfig {
  return {.host = "api.telegram.org",
          .port = 443,
          .access_token = "YOUR_TELEGRAM_TOKEN",
          .bot_username = "fixture_bot",
          .use_tls = true,
          .connect_timeout = std::chrono::milliseconds{5000},
          .action_timeout = std::chrono::milliseconds{30000},
          .poll_timeout = std::chrono::milliseconds{25000},
          .poll_force_close = std::chrono::milliseconds{30000},
          .poll_retry_interval = std::chrono::milliseconds{3000}};
}

auto onebot_websocket_config()
    -> std::shared_ptr<const obcx::core::BotInstallationPlan> {
  return obcx::test::installation_plan(obcx::bot::SurfaceId{"onebot11.qq"},
                                       "websocket", "qq-ws", true);
}
auto onebot_http_config()
    -> std::shared_ptr<const obcx::core::BotInstallationPlan> {
  return obcx::test::installation_plan(obcx::bot::SurfaceId{"onebot11.qq"},
                                       "http", "qq-http", true);
}
auto telegram_http_config()
    -> std::shared_ptr<const obcx::core::BotInstallationPlan> {
  return obcx::test::installation_plan(obcx::bot::SurfaceId{"telegram.bot_api"},
                                       "http", "telegram-http", true);
}

auto component_ids(const BotRecipeDescription &recipe)
    -> std::vector<std::string> {
  std::vector<std::string> ids;
  for (const auto &component : recipe.components) {
    ids.push_back(component.id.value());
  }
  return ids;
}

auto provided_capabilities(const BotRecipeDescription &recipe)
    -> std::set<std::string> {
  std::set<std::string> capabilities;
  for (const auto &component : recipe.components) {
    for (const auto &capability : component.provides) {
      capabilities.emplace(capability.value());
    }
  }
  return capabilities;
}

TEST(BotInstallationAssemblerTest,
     ConcreteProtocolComponentsPublishProviderTypedCapabilities) {
  obcx::core::BotInstallation onebot{"onebot-protocol-test",
                                     obcx::bot::SurfaceId{"onebot11.qq"}};
  onebot.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  onebot.assemble();
  const auto onebot_protocol =
      onebot.capability<obcx::adapter::onebot11::ProtocolAdapter>(
          obcx::core::CapabilityId{"onebot11.protocol"});
  ASSERT_NE(onebot_protocol, nullptr);
  const auto onebot_payload = onebot_protocol->serialize_send_message_request(
      "123", {{.type = "text", .data = {{"text", "hello"}}}});
  EXPECT_TRUE(nlohmann::json::parse(onebot_payload).contains("action"));

  obcx::core::BotInstallation telegram{
      "telegram-protocol-test", obcx::bot::SurfaceId{"telegram.bot_api"}};
  telegram.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  telegram.assemble();
  const auto telegram_protocol =
      telegram.capability<obcx::adapter::telegram::ProtocolAdapter>(
          obcx::core::CapabilityId{"telegram.protocol"});
  ASSERT_NE(telegram_protocol, nullptr);
  const auto telegram_payload =
      telegram_protocol->serialize_send_message_request(
          "-1001", {{.type = "text", .data = {{"text", "hello"}}}});
  EXPECT_EQ(nlohmann::json::parse(telegram_payload).at("method"),
            "sendMessage");
}

TEST(BotInstallationAssemblerTest,
     TransportComponentsOwnProviderStateWithoutStartingDuringAssembly) {
  obcx::core::BotInstallation onebot_websocket{
      "onebot-websocket-test", obcx::bot::SurfaceId{"onebot11.qq"}};
  onebot_websocket.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  onebot_websocket.add_component(
      std::make_unique<obcx::core::OneBot11WebSocketTransportComponent>(
          onebot_websocket.executor(), onebot_websocket_connection()));
  onebot_websocket.assemble();
  const auto websocket =
      onebot_websocket.capability<obcx::core::OneBot11Transport>(
          obcx::core::CapabilityId{"onebot11.transport"});
  ASSERT_NE(websocket, nullptr);
  EXPECT_FALSE(websocket->is_connected());

  obcx::core::BotInstallation onebot_http{"onebot-http-test",
                                          obcx::bot::SurfaceId{"onebot11.qq"}};
  onebot_http.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  onebot_http.add_component(
      std::make_unique<obcx::core::OneBot11HttpTransportComponent>(
          onebot_http.executor(), onebot_http_connection()));
  onebot_http.assemble();
  EXPECT_FALSE(onebot_http
                   .capability<obcx::core::OneBot11Transport>(
                       obcx::core::CapabilityId{"onebot11.transport"})
                   ->is_connected());

  obcx::core::BotInstallation telegram{
      "telegram-http-test", obcx::bot::SurfaceId{"telegram.bot_api"}};
  telegram.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  telegram.add_component(
      std::make_unique<obcx::core::TelegramHttpTransportComponent>(
          telegram.executor(), telegram_http_connection()));
  telegram.assemble();
  EXPECT_FALSE(telegram
                   .capability<obcx::core::TelegramTransport>(
                       obcx::core::CapabilityId{"telegram.transport"})
                   ->is_connected());
}

TEST(BotInstallationAssemblerTest,
     AssemblesReviewedComponentsWithoutProviderIoBeforeStart) {
  for (const auto &config : {onebot_websocket_config(), onebot_http_config(),
                             telegram_http_config()}) {
    auto installation = BotInstallationAssembler::assemble(*config);
    ASSERT_NE(installation, nullptr);
    EXPECT_EQ(installation->state(),
              obcx::core::BotInstallationState::Assembled);
    EXPECT_EQ(installation->installation_id(),
              config->metadata().installation_id);
    EXPECT_NE(installation->capability<obcx::core::BotOperationEndpoint>(
                  obcx::core::CapabilityId{"bot.operations"}),
              nullptr);
    EXPECT_NE(installation->capability<obcx::core::BotEventCapability>(
                  obcx::core::CapabilityId{"bot.events"}),
              nullptr);
    if (config->metadata().surface ==
        obcx::bot::SurfaceId{"telegram.bot_api"}) {
      EXPECT_NE(installation->capability<obcx::core::CommandCatalogPublisher>(
                    obcx::core::CapabilityId{"telegram.command-catalog"}),
                nullptr);
    }
  }
}

TEST(BotInstallationAssemblerTest,
     ReviewedRecipesStartAndStopWithoutRunningProviderExecutor) {
  for (const auto &config : {onebot_websocket_config(), onebot_http_config(),
                             telegram_http_config()}) {
    auto installation = BotInstallationAssembler::assemble(*config);
    const auto events =
        installation->capability<obcx::core::BotEventCapability>(
            obcx::core::CapabilityId{"bot.events"});
    events->subscribe_messages(
        [](const obcx::core::BotEventContext &,
           const obcx::common::MessageEvent &) -> boost::asio::awaitable<void> {
          co_return;
        });
    installation->start();
    EXPECT_EQ(installation->state(), obcx::core::BotInstallationState::Running);
    EXPECT_TRUE(installation->accepting_work());
    installation->stop();
    installation->stop();
    EXPECT_EQ(installation->state(), obcx::core::BotInstallationState::Stopped);
    EXPECT_FALSE(installation->accepting_work());
  }
}

TEST(BotInstallationAssemblerTest, SelectsExactReviewedRecipes) {
  const auto websocket =
      BotInstallationAssembler::describe(*onebot_websocket_config());
  EXPECT_EQ(websocket.recipe_id, "onebot11.qq.websocket");
  EXPECT_EQ(component_ids(websocket),
            (std::vector<std::string>{
                "onebot11.protocol", "onebot11.transport.websocket",
                "onebot11.event-ingress", "onebot11.operations"}));

  const auto onebot_http =
      BotInstallationAssembler::describe(*onebot_http_config());
  EXPECT_EQ(onebot_http.recipe_id, "onebot11.qq.http");
  EXPECT_EQ(component_ids(onebot_http),
            (std::vector<std::string>{
                "onebot11.protocol", "onebot11.transport.http",
                "onebot11.event-ingress", "onebot11.operations"}));

  const auto telegram =
      BotInstallationAssembler::describe(*telegram_http_config());
  EXPECT_EQ(telegram.recipe_id, "telegram.bot_api.http");
  EXPECT_EQ(component_ids(telegram),
            (std::vector<std::string>{
                "telegram.protocol", "telegram.transport.http",
                "telegram.event-ingress", "telegram.media-upload",
                "telegram.operations", "telegram.command-catalog"}));
}

TEST(BotInstallationAssemblerTest,
     RecipesPublishOnlyTheirReviewedCapabilitySets) {
  const auto websocket =
      BotInstallationAssembler::describe(*onebot_websocket_config());
  EXPECT_EQ(provided_capabilities(websocket),
            (std::set<std::string>{"bot.events", "bot.operations",
                                   "onebot11.protocol", "onebot11.transport"}));
  EXPECT_EQ(std::set<ActionId>(websocket.advertised_actions.begin(),
                               websocket.advertised_actions.end()),
            (std::set<ActionId>{ActionId{"message.send_group"},
                                ActionId{"message.send_private"},
                                ActionId{"message.delete"},
                                ActionId{"onebot11.group_member.get"},
                                ActionId{"onebot11.forward_message.get"},
                                ActionId{"onebot11.group_file.resolve"},
                                ActionId{"onebot11.private_file.resolve"},
                                ActionId{"onebot11.group.poke"}}));

  const auto telegram =
      BotInstallationAssembler::describe(*telegram_http_config());
  EXPECT_EQ(provided_capabilities(telegram),
            (std::set<std::string>{"bot.events", "bot.operations",
                                   "telegram.command-catalog",
                                   "telegram.media-upload", "telegram.protocol",
                                   "telegram.transport"}));
  EXPECT_EQ(telegram.advertised_actions.size(), 9U);
}

TEST(BotInstallationAssemblerTest,
     DescriptorValidationIsDeterministicAndSideEffectFree) {
  EXPECT_EQ(BotInstallationAssembler::validate(*onebot_websocket_config())
                .lifecycle_order,
            (std::vector<std::size_t>{0, 1, 2, 3}));
  EXPECT_EQ(
      BotInstallationAssembler::validate(*onebot_http_config()).lifecycle_order,
      (std::vector<std::size_t>{0, 1, 2, 3}));
  EXPECT_EQ(BotInstallationAssembler::validate(*telegram_http_config())
                .lifecycle_order,
            (std::vector<std::size_t>{0, 1, 2, 3, 4, 5}));
}

TEST(BotInstallationAssemblerTest,
     OptionalTelegramMediaCapabilityCanBeOmittedFromATestRecipe) {
  auto telegram = BotInstallationAssembler::describe(*telegram_http_config());
  const auto media = std::ranges::find(
      telegram.components, std::string_view{"telegram.media-upload"},
      [](const auto &component) {
        return std::string_view{component.id.value()};
      });
  ASSERT_NE(media, telegram.components.end());
  telegram.components.erase(media);
  EXPECT_THROW((void)obcx::core::validate_component_recipe(telegram.components),
               BotComponentRuntimeError);
  // A deliberate upload-free recipe removes both the dependency and action.
  for (auto &component : telegram.components) {
    if (component.id.value() == "telegram.operations") {
      std::erase_if(component.required, [](const auto &capability) {
        return capability.value() == "telegram.media-upload";
      });
    }
  }
  std::erase(telegram.advertised_actions,
             ActionId{"telegram.media.send_group_uploads"});
  EXPECT_NO_THROW(
      (void)obcx::core::validate_component_recipe(telegram.components));
  EXPECT_FALSE(
      provided_capabilities(telegram).contains("telegram.media-upload"));
}

TEST(BotInstallationAssemblerTest,
     RejectsUnsupportedAndForeignConnectionSchemas) {
  const auto &catalog = obcx::test::bot_platform_catalog();
  const auto connection = obcx::test::connection_fixture(
      obcx::bot::SurfaceId{"telegram.bot_api"}, "http");
  EXPECT_THROW(
      (void)catalog->parse(
          {"bad", true, obcx::bot::SurfaceId{"telegram.bot_api"}, "websocket"},
          connection, "bots.bad"),
      obcx::core::BotConfigurationError);
  EXPECT_THROW((void)catalog->parse(
                   {"bad", true, obcx::bot::SurfaceId{"onebot11.qq"}, "http"},
                   connection, "bots.bad"),
               obcx::core::BotConfigurationError);
}

} // namespace
