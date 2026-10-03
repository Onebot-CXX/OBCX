#include "core/actor/actor_messages.hpp"
#include "core/bot/messaging.hpp"
#include "core/command/command_coordinator.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "core/runtime/runtime_generation.hpp"
#include "support/bot_platform_fixture.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include <toml++/toml.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
namespace fs = std::filesystem;

class HelpGateway final : public obcx::bot::BotOperationGateway {
public:
  auto supported_actions(
      const obcx::bot::BotInstallationRef &installation) const
      -> obcx::bot::BotOperationResult<obcx::bot::SupportedActions> override {
    return obcx::bot::BotOperationResult<obcx::bot::SupportedActions>::success(
        {.installation = installation, .actions = {}});
  }
  auto invoke(obcx::bot::OperationEnvelope operation)
      -> boost::asio::awaitable<obcx::bot::OperationReply> override {
    operations.push_back(std::move(operation));
    co_return obcx::bot::OperationReply::success(obcx::bot::Json::object());
  }
  std::vector<obcx::bot::OperationEnvelope> operations;
};

TEST(ActorCommandAvailabilityIntegrationTest,
     RealActorsHideBridgeInExHentaiOnlyGroup) {
  const auto root =
      fs::temp_directory_path() /
      ("obcx-command-scope-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  struct Cleanup {
    fs::path root;
    ~Cleanup() { fs::remove_all(root); }
  } cleanup{root};
  auto config = toml::parse_file(OBCX_EXHENTAI_CONFIG_EXAMPLE);
  const auto actor_path = [](const char *workspace_path) {
    if (const auto *installed = std::getenv("OBCX_AVAILABILITY_ACTOR_DIR")) {
      return (fs::path{installed} / fs::path{workspace_path}.filename())
          .string();
    }
    return std::string{workspace_path};
  };
  auto &actors = *config["actors"].as_table();
  (*actors["exhentai_fetch"].as_table())
      .insert_or_assign("library", actor_path(OBCX_AVAILABILITY_EXHENTAI));
  config["db"]["instances"]["main"].as_table()->insert_or_assign(
      "path", (root / "state.sqlite3").string());
  auto additions = toml::parse(R"(
[bots.qq_bot]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq_bot.connection]
host = "localhost"
port = 3000
access_token = ""
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000
[actors.bridge]
enabled = true
db = "main"
db_namespace = "bridge"
partition = "conversation_id"
[actors.bridge.config]
telegram_installation = "telegram_bot"
onebot11_installation = "qq_bot"
bridge_files_dir = "/unused/availability-test"
bridge_files_container_dir = "/unused/availability-peer"
[[group_mappings.group_to_group]]
telegram_group_id = "-10020"
qq_group_id = "20000"
mode = "group_to_group"
show_qq_to_tg_sender = true
show_tg_to_qq_sender = true
enable_qq_to_tg = true
enable_tg_to_qq = true
[actor_runtime.scheduler]
workers = 6
blocking_workers = 6
[qq_shared_files]
mappings = [{ installation_id = "qq_bot", host_root = "/unused/availability-test", peer_root = "/unused/availability-peer" }]
success_retention_seconds = 600
uncertain_retention_seconds = 3600
cleanup_interval_seconds = 60
maximum_batch_image_bytes = 268435456
maximum_cache_bytes = 2147483648
maximum_cache_files = 10000
maximum_payload_bytes = 1048576
directory_mode = 0o700
file_mode = 0o600
)");
  config["bots"].as_table()->insert_or_assign(
      "qq_bot", *additions["bots"]["qq_bot"].as_table());
  actors.insert_or_assign("bridge", *additions["actors"]["bridge"].as_table());
  actors["bridge"].as_table()->insert_or_assign(
      "library", actor_path(OBCX_AVAILABILITY_BRIDGE));
  config.insert_or_assign("group_mappings",
                          *additions["group_mappings"].as_table());
  config.insert_or_assign("actor_runtime",
                          *additions["actor_runtime"].as_table());
  auto &exhentai = *actors["exhentai_fetch"]["config"].as_table();
  // Synthetic equivalent of the reported group; do not read deployment config.
  exhentai.insert_or_assign(
      "onebot_targets",
      toml::array{toml::table{
          {"installation_id", "qq_bot"},
          {"group_id", "1004979108"},
          {"manager_access",
           toml::table{{"mode", "allowlist"}, {"entries", toml::array{}}}}}});
  exhentai.insert_or_assign("qq_shared_files",
                            *additions["qq_shared_files"].as_table());
  exhentai.insert_or_assign("qq_send_images", false);
  exhentai.insert_or_assign("qq_image_ffmpeg_path", "ffmpeg");
  auto &routes = *config["command_runtime"]["routes"].as_array();
  routes.push_back(toml::table{{"actor", "exhentai_fetch"},
                               {"commands", toml::array{"exhentai"}},
                               {"platforms", toml::array{"qq"}},
                               {"bots", toml::array{"qq_bot"}},
                               {"fallback", "continue"}});
  routes.push_back(toml::table{{"actor", "bridge"},
                               {"commands", toml::array{"bridge_status"}},
                               {"platforms", toml::array{"qq"}},
                               {"bots", toml::array{"qq_bot"}},
                               {"fallback", "continue"}});
  const auto path = root / "config.toml";
  {
    std::ofstream output(path);
    output << config;
  }
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  const auto parsed = builder.parse_config(path.string());
  ASSERT_TRUE(parsed);
  auto gateway = std::make_shared<HelpGateway>();
  auto database = std::make_shared<obcx::core::DbManager>();
  database->configure(parsed.snapshot->get_db_instance_configs());
  const auto tables = [&] {
    return database->connection("main")->query(
        "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name");
  };
  const auto initial_tables = tables();
  auto built = builder.build(
      {.purpose = obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
       .generation_id = 1,
       .snapshot = parsed.snapshot,
       .actor_search_directories =
           {fs::path{actor_path(OBCX_AVAILABILITY_BRIDGE)}.parent_path(),
            fs::path{actor_path(OBCX_AVAILABILITY_EXHENTAI)}.parent_path()},
       .staging_root = root / "staging",
       .configured_io_sources = 1,
       .db_manager = database,
       .bot_operation_client = gateway});
  ASSERT_TRUE(built.ready())
      << (built.failure ? built.failure->code + ": " + built.failure->message
                        : "");
  EXPECT_TRUE(gateway->operations.empty());
  EXPECT_EQ(tables(), initial_tables);
  const auto table = built.generation->command_routing_table();
  const obcx::core::CommandPolicySubject subject{
      "qq",         "qq_bot", obcx::core::CommandConversationKind::Group,
      "1004979108", "7",      std::nullopt};
  EXPECT_EQ(table->eligibility("exhentai", subject),
            obcx::core::CommandEligibility::Eligible);
  EXPECT_EQ(table->eligibility("bridge_status", subject),
            obcx::core::CommandEligibility::Unavailable);
  ASSERT_EQ(table->find_bot({"qq", "qq_bot"})->catalog.size(), 3U);

  // Test-only ingress against the prepared generation; all providers are fake.
  boost::asio::io_context io;
  const auto invoke = [&](const std::string &command) {
    obcx::core::MessageEnvelope message;
    message.id = "scope-test-" + command;
    message.type = obcx::core::canonical_message_type_name<
        obcx::core::events::RawMessageEvent>();
    message.source_platform = "qq";
    message.source_bot = "qq_bot";
    message.conversation_id = "group:1004979108";
    message.payload = {{"source_bot_configured", true},
                       {"sender", "7"},
                       {"group_id", "1004979108"},
                       {"message_type", "group"}};
    message.raw = {{"raw_message", "/" + command}};
    auto result = boost::asio::co_spawn(
        io,
        built.generation->process(std::move(message),
                                  built.generation->admit_route()),
        boost::asio::use_future);
    io.run();
    io.restart();
    return result.get();
  };
  EXPECT_TRUE(invoke("help").ok());
  ASSERT_EQ(gateway->operations.size(), 1U);
  const auto text = gateway->operations.front()
                        .payload.at("message")
                        .at(0)
                        .at("data")
                        .at("text")
                        .get<std::string>();
  EXPECT_NE(text.find("/exhentai - "), std::string::npos);
  EXPECT_NE(text.find("/help - "), std::string::npos);
  EXPECT_EQ(text.find("bridge_status"), std::string::npos);
  const auto denied = invoke("bridge_status");
  ASSERT_EQ(denied.failures.size(), 1U);
  EXPECT_EQ(denied.failures.front().failure.code, "command_unavailable");
  EXPECT_TRUE(denied.stages.empty());
  EXPECT_TRUE(denied.emitted.empty());
  EXPECT_EQ(gateway->operations.size(), 1U);
  EXPECT_EQ(tables(), initial_tables);
}
} // namespace
