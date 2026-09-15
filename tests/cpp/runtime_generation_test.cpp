#include "common/config_snapshot.hpp"
#include "core/actor/actor_manager.hpp"
#include "core/actor/actor_messages.hpp"
#include "core/bot/bot_installation_directory.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/typed_operation.hpp"
#include "core/command/command_coordinator.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "core/infrastructure/process_staging_uuid.hpp"
#include "core/runtime/orchestrator.hpp"
#include "core/runtime/runtime_generation.hpp"
#include "support/bot_platform_fixture.hpp"

#include <array>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

class CatalogTelegramCapability final
    : public obcx::core::CommandCatalogPublisher {
public:
  auto publish(const std::vector<obcx::core::CommandCatalogEntry> &commands)
      -> boost::asio::awaitable<
          obcx::core::CommandCatalogPublishResult> override {
    {
      std::scoped_lock lock(mutex_);
      calls_.push_back(commands);
    }
    if (fail) {
      throw std::runtime_error{"catalog publish failed"};
    }
    co_return obcx::core::CommandCatalogPublishResult{.supported = true,
                                                      .succeeded = true};
  }

  [[nodiscard]] auto calls() const
      -> std::vector<std::vector<obcx::core::CommandCatalogEntry>> {
    std::scoped_lock lock(mutex_);
    return calls_;
  }

  std::atomic_bool fail = false;

private:
  mutable std::mutex mutex_;
  std::vector<std::vector<obcx::core::CommandCatalogEntry>> calls_;
};

auto make_test_endpoint(obcx::bot::BotInstallationRef installation)
    -> std::shared_ptr<obcx::core::OperationRegistry> {
  auto endpoint = std::make_shared<obcx::core::OperationRegistry>(installation);
  endpoint->seal(installation, {});
  return endpoint;
}

struct TestRegistryPlaceholder {};

template <typename Predicate>
auto wait_until(Predicate predicate,
                const std::chrono::milliseconds timeout = 2s) -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

class RuntimeGenerationTest : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("obcx-runtime-generation-test-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root_);
    obcx::core::DbManager::reset_shared_managers_for_tests();
  }

  void TearDown() override {
    obcx::core::DbManager::reset_shared_managers_for_tests();
    fs::remove_all(root_);
  }

  auto snapshot(std::string name, const std::string &document)
      -> std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> {
    const auto path = root_ / std::move(name);
    {
      std::ofstream output(path);
      output << document;
    }
    auto built =
        obcx::core::RuntimeGenerationBuilder{obcx::test::bot_platform_catalog()}
            .parse_config(path.string());
    EXPECT_TRUE(built);
    return built.snapshot;
  }

  auto valid_config(const std::string &actor_library,
                    std::string token = "stable-token",
                    std::string actor_config = "label = \"a\"") -> std::string {
    const auto database = (root_ / "runtime.sqlite3").string();
    return "[bots.primary]\n"
           "enabled = true\n"
           "surface = \"onebot11.qq\"\n"
           "transport = \"http\"\n\n"
           "[bots.primary.connection]\n"
           "host = \"localhost\"\n"
           "port = 3000\n"
           "access_token = \"" +
           token +
           "\"\n"
           "use_tls = false\n"
           "connect_timeout_ms = 5000\n"
           "action_timeout_ms = 30000\n"
           "poll_interval_ms = 1000\n\n"
           "[db.instances.main]\n"
           "type = \"sqlite\"\n"
           "path = \"" +
           database +
           "\"\n\n"
           "[actor_runtime.scheduler]\n"
           "workers = 2\n"
           "blocking_workers = 1\n\n"
           "[actors.test_actor_v2]\n"
           "library = \"" +
           actor_library +
           "\"\n"
           "enabled = true\n"
           "db = \"main\"\n\n"
           "[actors.test_actor_v2.config]\n"
           "target_installation = \"primary\"\n" +
           actor_config +
           "\n\n[pipelines.sdk]\n"
           "source = \"obcx::tests::events::SdkSmoke\"\n\n"
           "[[pipelines.sdk.stages]]\n"
           "name = \"handle\"\n"
           "actor = \"test_actor_v2\"\n"
           "input = \"obcx::tests::events::SdkSmoke\"\n"
           "output = \"ignored::Output\"\n";
  }

  auto valid_command_config(const std::string &actor_library,
                            std::string command = "sdk_ping",
                            std::string platform = "qq",
                            std::string bot_type = "qq") -> std::string {
    auto document = valid_config(actor_library);
    const auto surface = document.find("surface = \"onebot11.qq\"");
    if (surface != std::string::npos && bot_type == "telegram") {
      document.replace(surface, std::string{"surface = \"onebot11.qq\""}.size(),
                       "surface = \"telegram.bot_api\"");
      const std::string onebot_connection = "host = \"localhost\"\n"
                                            "port = 3000\n"
                                            "access_token = \"stable-token\"\n"
                                            "use_tls = false\n"
                                            "connect_timeout_ms = 5000\n"
                                            "action_timeout_ms = 30000\n"
                                            "poll_interval_ms = 1000\n";
      const std::string telegram_connection =
          "host = \"api.telegram.org\"\n"
          "port = 443\n"
          "access_token = \"stable-token\"\n"
          "bot_username = \"fixture_bot\"\n"
          "use_tls = true\n"
          "connect_timeout_ms = 5000\n"
          "action_timeout_ms = 30000\n"
          "poll_timeout_ms = 25000\n"
          "poll_force_close_ms = 30000\n"
          "poll_retry_interval_ms = 3000\n";
      const auto connection = document.find(onebot_connection);
      if (connection != std::string::npos) {
        document.replace(connection, onebot_connection.size(),
                         telegram_connection);
      }
    }
    return document +
           "\n[command_runtime]\n"
           "timeout_ms = 500\n\n"
           "[command_runtime.help]\n"
           "page_bytes = 3500\n"
           "maximum_pages = 10\n\n"
           "[command_runtime.access.groups]\n"
           "mode = \"unrestricted\"\n"
           "entries = []\n\n"
           "[command_runtime.access.users]\n"
           "mode = \"unrestricted\"\n"
           "entries = []\n\n"
           "[[command_runtime.routes]]\n"
           "actor = \"test_actor_v2\"\n"
           "commands = [\"" +
           command +
           "\"]\n"
           "platforms = [\"" +
           platform +
           "\"]\n"
           "bots = [\"primary\"]\n"
           "fallback = \"continue\"\n";
  }

  auto services_for(
      const std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> &config)
      -> std::pair<std::shared_ptr<obcx::core::DbManager>,
                   std::shared_ptr<TestRegistryPlaceholder>> {
    return {obcx::core::DbManager::shared_manager(
                config->get_db_instance_configs()),
            std::make_shared<TestRegistryPlaceholder>()};
  }

  auto private_actor_config(const std::string &library, std::string pipeline,
                            std::string stage, std::size_t hop_limit)
      -> std::string {
    const auto database = (root_ / "private-runtime.sqlite3").string();
    return "[db.instances.main]\n"
           "type = \"sqlite\"\n"
           "path = \"" +
           database +
           "\"\n\n"
           "[actor_runtime]\n"
           "routing_hop_limit = " +
           std::to_string(hop_limit) +
           "\n\n[actors.private_dependency_actor]\n"
           "library = \"" +
           library +
           "\"\n"
           "enabled = true\n"
           "partition = \"conversation_id\"\n"
           "db = \"main\"\n\n"
           "[actors.private_dependency_actor.config]\n"
           "label = \"generation-owned\"\n\n"
           "[pipelines." +
           pipeline +
           "]\nsource = \"obcx::tests::events::PrivateDependencyProbe\"\n\n"
           "[[pipelines." +
           pipeline + ".stages]]\nname = \"" + stage +
           "\"\nactor = \"private_dependency_actor\"\n"
           "input = \"obcx::tests::events::PrivateDependencyProbe\"\n"
           "output = \"PrivateDependencyObserved\"\n"
           "mode = \"await\"\n";
  }

  auto request(
      obcx::core::RuntimeGenerationBuildPurpose purpose,
      std::uint64_t generation_id,
      std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> config,
      std::shared_ptr<obcx::core::DbManager> database,
      std::shared_ptr<TestRegistryPlaceholder>)
      -> obcx::core::RuntimeGenerationBuildRequest {
    return {.purpose = purpose,
            .generation_id = generation_id,
            .snapshot = std::move(config),
            .actor_search_directories =
                {fs::path{OBCX_TEST_ACTOR_V2_LIBRARY}.parent_path()},
            .staging_root = root_ / "staging",
            .configured_io_sources = 1,
            .db_manager = std::move(database),
            .bot_operation_client = gateway_};
  }

  fs::path root_;
  std::shared_ptr<obcx::bot::BotOperationGateway> gateway_ = [] {
    auto gateway = std::make_shared<obcx::core::BotOperationDispatcher>(
        [](const obcx::bot::SurfaceId &) { return false; });
    gateway->seal_registrations();
    return gateway;
  }();
};

TEST_F(RuntimeGenerationTest,
       StartupValidationAndReloadShareConstructionAndServices) {
  const auto config =
      snapshot("valid.toml", valid_config(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto [database, registry] = services_for(config);
  auto endpoint = make_test_endpoint(obcx::bot::BotInstallationRef{
      .installation_id = "primary",
      .surface = obcx::bot::SurfaceId{"onebot11.qq"}});
  auto directory = std::make_shared<obcx::core::BotInstallationDirectory>();
  directory->register_capabilities(endpoint->installation(), endpoint);

  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto operation_client = std::make_shared<obcx::core::BotOperationDispatcher>(
      [](const obcx::bot::SurfaceId &) { return false; });
  std::vector<std::shared_ptr<obcx::core::RuntimeGeneration>> generations;
  const std::array purposes = {
      obcx::core::RuntimeGenerationBuildPurpose::Startup,
      obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
      obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate};
  std::shared_ptr<obcx::core::BlockingExecutor> process_blocking_executor;
  std::uint64_t id = 1;
  for (const auto purpose : purposes) {
    auto build_request = request(purpose, id++, config, database, registry);
    build_request.bot_installation_directory = directory;
    build_request.bot_operation_client = operation_client;
    build_request.require_registered_bots =
        purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate;
    if (!generations.empty()) {
      build_request.active_process_owned_fingerprint =
          generations.front()->process_owned_fingerprint();
      build_request.active_process_owned_dependencies =
          generations.front()->process_owned_dependencies();
    }
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
      build_request.blocking_executor = process_blocking_executor;
    }
    auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.ready())
        << (result.failure
                ? result.failure->code + ": " + result.failure->message
                : "missing failure");
    EXPECT_EQ(result.generation->db_manager(), database);
    EXPECT_EQ(result.generation->bot_installation_directory(), directory);
    EXPECT_EQ(result.generation->bot_operation_client(), operation_client);
    const auto generation_info =
        result.generation->services()
            ->get_service<obcx::core::ActorGenerationInfo>();
    ASSERT_NE(generation_info, nullptr);
    EXPECT_EQ(generation_info->generation_id, id - 1);
    const auto expected_purpose =
        purpose == obcx::core::RuntimeGenerationBuildPurpose::Startup
            ? obcx::core::ActorGenerationPurpose::Startup
        : purpose == obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly
            ? obcx::core::ActorGenerationPurpose::ValidationOnly
            : obcx::core::ActorGenerationPurpose::ReloadCandidate;
    EXPECT_EQ(generation_info->purpose, expected_purpose);
    EXPECT_EQ(result.generation->services()
                  ->get_service<obcx::bot::BotOperationGateway>(),
              operation_client);
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::Startup) {
      process_blocking_executor = result.generation->blocking_executor();
      ASSERT_NE(process_blocking_executor, nullptr);
      EXPECT_EQ(process_blocking_executor->worker_count(),
                result.generation->thread_budget().blocking_workers);
      EXPECT_EQ(result.generation->services()
                    ->get_service<obcx::core::BlockingExecutor>(),
                process_blocking_executor);
    } else if (purpose ==
               obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly) {
      EXPECT_EQ(result.generation->blocking_executor(), nullptr);
      EXPECT_EQ(result.generation->services()
                    ->get_service<obcx::core::BlockingExecutor>(),
                nullptr);
    } else {
      EXPECT_EQ(result.generation->blocking_executor(),
                process_blocking_executor);
      EXPECT_EQ(result.generation->services()
                    ->get_service<obcx::core::BlockingExecutor>(),
                process_blocking_executor);
    }
    EXPECT_TRUE(
        result.generation->actor_manager()->is_actor_loaded("test_actor_v2"));
    generations.push_back(std::move(result.generation));
  }
}

TEST_F(RuntimeGenerationTest, GenerationPreparationFailureIsTyped) {
  struct Case {
    std::string status;
    std::string expected_code;
    std::string expected_message;
  };
  for (const auto &test :
       std::vector<Case>{{"failed", "reload_actor_initialization_failed",
                          "fixture preparation failed"},
                         {"restart", "reload_restart_required",
                          "fixture preparation requires restart"}}) {
    const auto config =
        snapshot("preparation-" + test.status + ".toml",
                 valid_config(OBCX_TEST_ACTOR_V2_LIBRARY, "stable-token",
                              "label = \"a\"\npreparation_status = \"" +
                                  test.status + "\""));
    auto [database, registry] = services_for(config);
    auto build_request =
        request(obcx::core::RuntimeGenerationBuildPurpose::Startup, 1, config,
                database, registry);

    obcx::core::RuntimeGenerationBuilder builder{
        obcx::test::bot_platform_catalog()};
    const auto result = builder.build(std::move(build_request));

    ASSERT_FALSE(result.ready());
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->code, test.expected_code);
    EXPECT_NE(result.failure->message.find(test.expected_message),
              std::string::npos);
  }
}

TEST_F(RuntimeGenerationTest, StagingRootContainsTheProcessUuid) {
  const auto config = snapshot("process-staging-uuid.toml",
                               valid_config(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto [database, registry] = services_for(config);
  auto build_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::Startup, 1, config,
              database, registry);
  build_request.staging_root.clear();

  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto result = builder.build(std::move(build_request));

  ASSERT_TRUE(result.ready())
      << (result.failure ? result.failure->code + ": " + result.failure->message
                         : "missing failure");
  EXPECT_EQ(result.generation->staging_root().parent_path(),
            fs::temp_directory_path() / "obcx-runtime-generations");
  EXPECT_NE(result.generation->staging_root().filename().string().find(
                obcx::core::detail::process_staging_uuid()),
            std::string::npos);
}

TEST_F(RuntimeGenerationTest,
       ReloadCandidateRequiresTheActiveProcessBlockingExecutor) {
  const auto config = snapshot("missing-blocking-service.toml",
                               valid_config(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto [database, registry] = services_for(config);
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto active =
      builder.build(request(obcx::core::RuntimeGenerationBuildPurpose::Startup,
                            1, config, database, registry));
  ASSERT_TRUE(active.ready());

  auto candidate_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 2,
              config, database, registry);
  candidate_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  candidate_request.active_process_owned_dependencies =
      active.generation->process_owned_dependencies();
  auto candidate = builder.build(std::move(candidate_request));

  ASSERT_TRUE(candidate.failure.has_value());
  EXPECT_EQ(candidate.failure->code, "reload_process_service_missing");
  EXPECT_EQ(candidate.failure->message,
            "process blocking executor is missing for reload candidate");
}

TEST_F(RuntimeGenerationTest,
       CommandRoutesValidateForEveryBuildPurposeAndRunThroughScheduler) {
  const auto config = snapshot(
      "valid-command.toml", valid_command_config(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto [database, registry] = services_for(config);
  auto endpoint = make_test_endpoint(obcx::bot::BotInstallationRef{
      .installation_id = "primary",
      .surface = obcx::bot::SurfaceId{"onebot11.qq"}});
  auto directory = std::make_shared<obcx::core::BotInstallationDirectory>();
  directory->register_capabilities(endpoint->installation(), endpoint);

  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  std::shared_ptr<obcx::core::RuntimeGeneration> startup;
  std::shared_ptr<obcx::core::BlockingExecutor> process_blocking_executor;
  std::uint64_t id = 50;
  for (const auto purpose :
       {obcx::core::RuntimeGenerationBuildPurpose::Startup,
        obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
        obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate}) {
    auto build_request = request(purpose, id++, config, database, registry);
    build_request.bot_installation_directory = directory;
    build_request.require_registered_bots =
        purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate;
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
      build_request.blocking_executor = process_blocking_executor;
    }
    auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.ready())
        << (result.failure
                ? result.failure->code + ": " + result.failure->message
                : "");
    ASSERT_TRUE(result.generation->command_routing_table());
    EXPECT_EQ(result.generation->command_routing_table()->routes().size(), 1U);
    const auto &command_bots =
        result.generation->command_routing_table()->bots();
    ASSERT_EQ(command_bots.size(), 1U);
    EXPECT_EQ(command_bots.begin()->second.patterns.size(), 1U);
    EXPECT_EQ(command_bots.begin()->second.catalog.size(), 2U);
    EXPECT_EQ(command_bots.begin()->second.catalog[0].name, "help");
    EXPECT_EQ(command_bots.begin()->second.catalog[1].name, "sdk_ping");
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::Startup) {
      process_blocking_executor = result.generation->blocking_executor();
      startup = std::move(result.generation);
    }
  }

  obcx::core::MessageEnvelope raw;
  raw.id = "command-root";
  raw.type = obcx::core::canonical_message_type_name<
      obcx::core::events::RawMessageEvent>();
  raw.source_platform = "qq";
  raw.source_bot = "primary";
  raw.conversation_id = "group:42";
  raw.payload = {{"source_bot_configured", true},
                 {"sender", "7"},
                 {"group_id", "42"},
                 {"chat_id", ""},
                 {"message_type", "group"}};
  raw.raw = {{"raw_message", "/sdk_alias consume"}};

  boost::asio::io_context io;
  auto future = boost::asio::co_spawn(
      io, startup->process(std::move(raw), startup->admit_route()),
      boost::asio::use_future);
  io.run();
  const auto result = future.get();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type, "SdkCommandObserved");
}

TEST_F(RuntimeGenerationTest,
       InvalidCommandEdgesFailBeforeActorActivationForEveryPurpose) {
  const auto config = snapshot(
      "invalid-command.toml",
      valid_command_config(OBCX_TEST_ACTOR_V2_LIBRARY, "not_declared"));
  auto [database, registry] = services_for(config);
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto process_blocking_executor =
      std::make_shared<obcx::core::BlockingExecutor>(1);

  for (const auto purpose :
       {obcx::core::RuntimeGenerationBuildPurpose::Startup,
        obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
        obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate}) {
    auto build_request = request(purpose, 60, config, database, registry);
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
      build_request.blocking_executor = process_blocking_executor;
    }
    auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.failure);
    EXPECT_EQ(result.failure->code, "command_not_declared");
  }
}

TEST_F(RuntimeGenerationTest,
       CatalogPublicationStartsAfterActivationAndFailureKeepsLocalRouting) {
  const auto config = snapshot(
      "catalog.toml", valid_command_config(OBCX_TEST_ACTOR_V2_LIBRARY,
                                           "sdk_ping", "telegram", "telegram"));
  auto [database, registry] = services_for(config);
  auto catalog = std::make_shared<CatalogTelegramCapability>();
  catalog->fail.store(true);
  auto directory = std::make_shared<obcx::core::BotInstallationDirectory>();
  auto endpoint =
      make_test_endpoint({.installation_id = "primary",
                          .surface = obcx::bot::SurfaceId{"telegram.bot_api"}});
  directory->register_capabilities(
      {.installation_id = "primary",
       .surface = obcx::bot::SurfaceId{"telegram.bot_api"}},
      endpoint, catalog);

  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto build_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::Startup, 70, config,
              database, registry);
  build_request.bot_installation_directory = directory;
  auto built = builder.build(std::move(build_request));
  ASSERT_TRUE(built.ready()) << (built.failure ? built.failure->message : "");
  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(catalog->calls().empty());

  built.generation->activate_command_catalogs();
  ASSERT_TRUE(wait_until([&] {
    const auto status = built.generation->command_catalog_status();
    return status.size() == 1 && status.front().attempts == 3;
  }));
  const auto status = built.generation->command_catalog_status();
  ASSERT_EQ(status.size(), 1U);
  EXPECT_EQ(status.front().desired_generation, 70U);
  EXPECT_FALSE(status.front().last_success_generation.has_value());
  EXPECT_EQ(status.front().failure_code, "command_catalog_publish_failed");
  const auto published = catalog->calls();
  ASSERT_EQ(published.size(), 3U);
  for (const auto &attempt : published) {
    ASSERT_EQ(attempt.size(), 2U);
    EXPECT_EQ(attempt[0].name, "help");
    EXPECT_EQ(attempt[1].name, "sdk_ping");
  }

  obcx::core::MessageEnvelope raw;
  raw.id = "catalog-failure-command";
  raw.type = obcx::core::canonical_message_type_name<
      obcx::core::events::RawMessageEvent>();
  raw.source_platform = "telegram";
  raw.source_bot = "primary";
  raw.conversation_id = "chat:42";
  raw.payload = {{"source_bot_configured", true},
                 {"sender", "7"},
                 {"group_id", "42"},
                 {"chat_id", "42"},
                 {"message_type", "group"}};
  raw.raw = {
      {"text", "/sdk_ping consume"},
      {"entities",
       obcx::common::json::array(
           {{{"type", "bot_command"}, {"offset", 0}, {"length", 9}}})},
  };
  boost::asio::io_context io;
  auto future = boost::asio::co_spawn(
      io,
      built.generation->process(std::move(raw),
                                built.generation->admit_route()),
      boost::asio::use_future);
  io.run();
  const auto result = future.get();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type, "SdkCommandObserved");
}

TEST_F(RuntimeGenerationTest,
       SupersededGenerationStopsRetriesBeforeNewCatalogPublication) {
  const auto config =
      snapshot("catalog-retry.toml",
               valid_command_config(OBCX_TEST_ACTOR_V2_LIBRARY, "sdk_ping",
                                    "telegram", "telegram"));
  auto [database, registry] = services_for(config);
  auto catalog = std::make_shared<CatalogTelegramCapability>();
  catalog->fail.store(true);
  auto directory = std::make_shared<obcx::core::BotInstallationDirectory>();
  auto endpoint =
      make_test_endpoint({.installation_id = "primary",
                          .surface = obcx::bot::SurfaceId{"telegram.bot_api"}});
  directory->register_capabilities(
      {.installation_id = "primary",
       .surface = obcx::bot::SurfaceId{"telegram.bot_api"}},
      endpoint, catalog);

  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto old_request = request(obcx::core::RuntimeGenerationBuildPurpose::Startup,
                             80, config, database, registry);
  old_request.bot_installation_directory = directory;
  auto old = builder.build(std::move(old_request));
  ASSERT_TRUE(old.ready());
  auto candidate_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 81,
              config, database, registry);
  candidate_request.bot_installation_directory = directory;
  candidate_request.require_registered_bots = true;
  candidate_request.active_process_owned_fingerprint =
      old.generation->process_owned_fingerprint();
  candidate_request.active_process_owned_dependencies =
      old.generation->process_owned_dependencies();
  candidate_request.blocking_executor = old.generation->blocking_executor();
  auto candidate = builder.build(std::move(candidate_request));
  ASSERT_TRUE(candidate.ready())
      << (candidate.failure
              ? candidate.failure->code + ": " + candidate.failure->message
              : "missing generation");

  old.generation->activate_command_catalogs();
  ASSERT_TRUE(wait_until([&] { return !catalog->calls().empty(); }));
  old.generation->shutdown();
  const auto attempts_after_shutdown = catalog->calls().size();
  std::this_thread::sleep_for(250ms);
  EXPECT_EQ(catalog->calls().size(), attempts_after_shutdown);

  catalog->fail.store(false);
  candidate.generation->activate_command_catalogs();
  ASSERT_TRUE(wait_until([&] {
    const auto status = candidate.generation->command_catalog_status();
    return status.size() == 1 && status.front().last_success_generation == 81U;
  }));
  EXPECT_EQ(catalog->calls().size(), attempts_after_shutdown + 1U);
}

TEST_F(RuntimeGenerationTest,
       ActorOwnedChangesRemainReloadableButProcessChangesDoNot) {
  const auto active_config =
      snapshot("active.toml", valid_config(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto [database, registry] = services_for(active_config);
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto active =
      builder.build(request(obcx::core::RuntimeGenerationBuildPurpose::Startup,
                            1, active_config, database, registry));
  ASSERT_TRUE(active.ready());

  const auto actor_changed =
      snapshot("actor-changed.toml",
               valid_config(OBCX_TEST_ACTOR_V2_LIBRARY, "stable-token",
                            "label = \"candidate\""));
  auto actor_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 2,
              actor_changed, database, registry);
  actor_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  actor_request.active_process_owned_dependencies =
      active.generation->process_owned_dependencies();
  actor_request.blocking_executor = active.generation->blocking_executor();
  auto actor_result = builder.build(std::move(actor_request));
  ASSERT_TRUE(actor_result.ready())
      << (actor_result.failure ? actor_result.failure->message : "");

  const auto bot_changed =
      snapshot("bot-changed.toml", valid_config(OBCX_TEST_ACTOR_V2_LIBRARY,
                                                "different-secret-token"));
  auto bot_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 3,
              bot_changed, database, registry);
  bot_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  bot_request.blocking_executor = active.generation->blocking_executor();
  auto bot_result = builder.build(std::move(bot_request));
  ASSERT_TRUE(bot_result.failure.has_value());
  EXPECT_EQ(bot_result.failure->code, "reload_restart_required");
  EXPECT_EQ(bot_result.failure->message, "bots");
  EXPECT_EQ(bot_result.failure->message.find("different-secret-token"),
            std::string::npos);

  auto database_document = valid_config(OBCX_TEST_ACTOR_V2_LIBRARY);
  const auto database_path = database_document.find("runtime.sqlite3");
  ASSERT_NE(database_path, std::string::npos);
  database_document.replace(database_path,
                            std::string{"runtime.sqlite3"}.size(),
                            "candidate.sqlite3");
  const auto database_changed =
      snapshot("database-changed.toml", database_document);
  auto database_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 4,
              database_changed, database, registry);
  database_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  database_request.blocking_executor = active.generation->blocking_executor();
  auto database_result = builder.build(std::move(database_request));
  ASSERT_TRUE(database_result.failure.has_value());
  EXPECT_EQ(database_result.failure->code, "reload_restart_required");
  EXPECT_EQ(database_result.failure->message, "database_instances");

  auto thread_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 5,
              active_config, database, registry);
  thread_request.configured_io_sources = 2;
  thread_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  thread_request.blocking_executor = active.generation->blocking_executor();
  auto thread_result = builder.build(std::move(thread_request));
  ASSERT_TRUE(thread_result.failure.has_value());
  EXPECT_EQ(thread_result.failure->code, "reload_restart_required");
  EXPECT_EQ(thread_result.failure->message, "runtime_thread_budget");

  auto blocking_document = valid_config(OBCX_TEST_ACTOR_V2_LIBRARY);
  const auto blocking_workers = blocking_document.find("blocking_workers = 1");
  ASSERT_NE(blocking_workers, std::string::npos);
  blocking_document.replace(blocking_workers,
                            std::string{"blocking_workers = 1"}.size(),
                            "blocking_workers = 2");
  const auto blocking_changed =
      snapshot("blocking-workers-changed.toml", blocking_document);
  auto blocking_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 6,
              blocking_changed, database, registry);
  blocking_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  blocking_request.blocking_executor = active.generation->blocking_executor();
  auto blocking_result = builder.build(std::move(blocking_request));
  ASSERT_TRUE(blocking_result.failure.has_value());
  EXPECT_EQ(blocking_result.failure->code, "reload_restart_required");
  EXPECT_EQ(blocking_result.failure->message, "runtime_thread_budget");
}

TEST_F(RuntimeGenerationTest,
       AllBuildPurposesRejectInvalidActorConfigurationContracts) {
  const std::array invalid_actor_configs = {
      std::string{"label = \"a\"\npositive_limit = 0"},
      std::string{"label = \"a\"\npositive_limit = \"secret-value\""},
      std::string{"label = \"a\"\nretry_base = 20\nretry_max = 10"},
  };
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto process_blocking_executor =
      std::make_shared<obcx::core::BlockingExecutor>(1);
  std::uint64_t generation_id = 30;

  for (const auto &actor_config : invalid_actor_configs) {
    const auto config = snapshot(
        "invalid-actor-config-" + std::to_string(generation_id) + ".toml",
        valid_config(OBCX_TEST_ACTOR_V2_LIBRARY, "stable-token", actor_config));
    auto [database, registry] = services_for(config);
    for (const auto purpose :
         {obcx::core::RuntimeGenerationBuildPurpose::Startup,
          obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
          obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate}) {
      auto build_request =
          request(purpose, generation_id++, config, database, registry);
      if (purpose ==
          obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
        build_request.blocking_executor = process_blocking_executor;
      }
      auto result = builder.build(std::move(build_request));
      ASSERT_TRUE(result.failure.has_value());
      EXPECT_EQ(result.failure->code, "reload_actor_config_invalid");
      EXPECT_EQ(result.failure->message.find("secret-value"),
                std::string::npos);
    }
  }
}

TEST_F(RuntimeGenerationTest,
       RequiredStringsAndBotInstallationsAreValidatedBeforeActivation) {
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  std::uint64_t generation_id = 90;
  const auto expect_invalid = [&](std::string name, std::string document,
                                  std::string_view expected_key) {
    const auto config = snapshot(std::move(name), document);
    auto [database, registry] = services_for(config);
    auto result = builder.build(request(
        obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
        generation_id++, config, std::move(database), std::move(registry)));
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->code, "reload_actor_config_invalid");
    EXPECT_NE(result.failure->message.find(expected_key), std::string::npos);
  };

  auto missing_label = valid_config(OBCX_TEST_ACTOR_V2_LIBRARY);
  const auto label = missing_label.find("label = \"a\"\n");
  ASSERT_NE(label, std::string::npos);
  missing_label.erase(label, std::string{"label = \"a\"\n"}.size());
  expect_invalid("missing-required-string.toml", std::move(missing_label),
                 "label");

  auto missing_installation = valid_config(OBCX_TEST_ACTOR_V2_LIBRARY);
  const auto installation =
      missing_installation.find("target_installation = \"primary\"\n");
  ASSERT_NE(installation, std::string::npos);
  missing_installation.erase(
      installation, std::string{"target_installation = \"primary\"\n"}.size());
  expect_invalid("missing-installation.toml", std::move(missing_installation),
                 "target_installation");
}

TEST_F(RuntimeGenerationTest,
       BotInstallationCollectionsValidateBeforeActivationForEveryPurpose) {
  const auto collection_document = [&](std::string installation = "primary",
                                       std::string identity = "route-a",
                                       bool keep_scalar = false) {
    auto document = valid_config(OBCX_TEST_ACTOR_V2_LIBRARY);
    if (!keep_scalar) {
      const auto scalar = document.find("target_installation = \"primary\"\n");
      EXPECT_NE(scalar, std::string::npos);
      if (scalar != std::string::npos) {
        document.erase(
            scalar, std::string{"target_installation = \"primary\"\n"}.size());
      }
    }
    document += "\n[[actors.test_actor_v2.config.target_installations]]\n"
                "id = \"" +
                identity + "\"\ntarget_installation = \"" + installation +
                "\"\n";
    return document;
  };

  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto process_blocking_executor =
      std::make_shared<obcx::core::BlockingExecutor>(1);
  std::uint64_t generation_id = 100;
  const auto valid =
      snapshot("valid-installation-collection.toml", collection_document());
  auto [database, registry] = services_for(valid);
  for (const auto purpose :
       {obcx::core::RuntimeGenerationBuildPurpose::Startup,
        obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
        obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate}) {
    auto build_request =
        request(purpose, generation_id++, valid, database, registry);
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
      build_request.blocking_executor = process_blocking_executor;
    }
    auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.ready())
        << (result.failure ? result.failure->message : "missing failure");
  }

  const auto expect_invalid = [&](std::string name, std::string document,
                                  std::string_view expected) {
    const auto config = snapshot(std::move(name), document);
    auto [invalid_database, invalid_registry] = services_for(config);
    auto result = builder.build(
        request(obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
                generation_id++, config, std::move(invalid_database),
                std::move(invalid_registry)));
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->code, "reload_actor_config_invalid");
    EXPECT_NE(result.failure->message.find(expected), std::string::npos)
        << result.failure->message;
  };

  expect_invalid("missing-collection-installation.toml",
                 collection_document("missing"), "target_installation");

  auto disabled = collection_document("secondary");
  disabled += "\n[bots.secondary]\n"
              "enabled = false\n"
              "surface = \"onebot11.qq\"\n"
              "transport = \"http\"\n"
              "[bots.secondary.connection]\n"
              "host = \"localhost\"\n"
              "port = 3000\n"
              "access_token = \"\"\n"
              "use_tls = false\n"
              "connect_timeout_ms = 5000\n"
              "action_timeout_ms = 30000\n"
              "poll_interval_ms = 1000\n";
  expect_invalid("disabled-collection-installation.toml", std::move(disabled),
                 "target_installation");

  auto duplicate = collection_document();
  duplicate += "\n[[actors.test_actor_v2.config.target_installations]]\n"
               "id = \"route-a\"\ntarget_installation = \"primary\"\n";
  expect_invalid("duplicate-collection-identity.toml", std::move(duplicate),
                 "duplicate id");

  auto duplicate_installation = collection_document();
  duplicate_installation +=
      "\n[[actors.test_actor_v2.config.target_installations]]\n"
      "id = \"route-b\"\ntarget_installation = \"primary\"\n";
  expect_invalid("duplicate-collection-installation.toml",
                 std::move(duplicate_installation),
                 "duplicate target_installation");

  expect_invalid("mixed-installation-forms.toml",
                 collection_document("primary", "route-a", true),
                 "exactly one form");

  auto unknown_reference = collection_document();
  const auto label = unknown_reference.find("label = \"a\"\n");
  ASSERT_NE(label, std::string::npos);
  unknown_reference.insert(label + std::string{"label = \"a\"\n"}.size(),
                           "selected_target = \"missing\"\n");
  expect_invalid("unknown-collection-reference.toml",
                 std::move(unknown_reference), "unknown target_installations");
}

TEST_F(RuntimeGenerationTest,
       CommandPolicyChangesAreGenerationIsolatedAcrossReloadCandidates) {
  const auto active_config =
      snapshot("command-policy-active.toml",
               valid_command_config(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto [database, registry] = services_for(active_config);
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto active =
      builder.build(request(obcx::core::RuntimeGenerationBuildPurpose::Startup,
                            1, active_config, database, registry));
  ASSERT_TRUE(active.ready())
      << (active.failure ? active.failure->message : "");

  auto candidate_document = valid_command_config(OBCX_TEST_ACTOR_V2_LIBRARY);
  const std::string unrestricted_users = "[command_runtime.access.users]\n"
                                         "mode = \"unrestricted\"\n"
                                         "entries = []";
  const std::string allowlisted_users =
      "[command_runtime.access.users]\n"
      "mode = \"allowlist\"\n"
      "entries = [{ platform = \"qq\", bot = \"primary\", "
      "native_user_id = \"allowed-user\" }]";
  const auto users = candidate_document.find(unrestricted_users);
  ASSERT_NE(users, std::string::npos);
  candidate_document.replace(users, unrestricted_users.size(),
                             allowlisted_users);
  const auto candidate_config =
      snapshot("command-policy-candidate.toml", candidate_document);
  auto candidate_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 2,
              candidate_config, database, registry);
  candidate_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  candidate_request.active_process_owned_dependencies =
      active.generation->process_owned_dependencies();
  candidate_request.blocking_executor = active.generation->blocking_executor();
  auto candidate = builder.build(std::move(candidate_request));
  ASSERT_TRUE(candidate.ready())
      << (candidate.failure
              ? candidate.failure->code + ": " + candidate.failure->message
              : "");

  const obcx::core::CommandPolicySubject denied{
      .platform = "qq",
      .bot = "primary",
      .conversation = obcx::core::CommandConversationKind::Private,
      .user_id = "denied-user"};
  const auto allowed = [&denied] {
    auto value = denied;
    value.user_id = "allowed-user";
    return value;
  }();
  ASSERT_NE(active.generation->command_routing_table(), nullptr);
  ASSERT_NE(candidate.generation->command_routing_table(), nullptr);
  EXPECT_TRUE(
      active.generation->command_routing_table()->permits("sdk_ping", denied));
  EXPECT_FALSE(candidate.generation->command_routing_table()->permits(
      "sdk_ping", denied));
  EXPECT_TRUE(candidate.generation->command_routing_table()->permits("sdk_ping",
                                                                     allowed));
  EXPECT_TRUE(
      active.generation->command_routing_table()->permits("sdk_ping", denied));
}

TEST_F(RuntimeGenerationTest,
       RebuiltActorsPipelinesAndRoutingPolicyRemainReloadable) {
  const auto active_config =
      snapshot("private-active.toml",
               private_actor_config(OBCX_PRIVATE_ACTOR_V1, "active_route",
                                    "active_stage", 32));
  auto [database, registry] = services_for(active_config);
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto active =
      builder.build(request(obcx::core::RuntimeGenerationBuildPurpose::Startup,
                            1, active_config, database, registry));
  ASSERT_TRUE(active.ready())
      << (active.failure ? active.failure->message : "");

  const auto candidate_config =
      snapshot("private-candidate.toml",
               private_actor_config(OBCX_PRIVATE_ACTOR_V2, "candidate_route",
                                    "candidate_stage", 64));
  auto candidate_request =
      request(obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate, 2,
              candidate_config, database, registry);
  candidate_request.active_process_owned_fingerprint =
      active.generation->process_owned_fingerprint();
  candidate_request.active_process_owned_dependencies =
      active.generation->process_owned_dependencies();
  candidate_request.blocking_executor = active.generation->blocking_executor();
  auto candidate = builder.build(std::move(candidate_request));
  ASSERT_TRUE(candidate.ready())
      << (candidate.failure
              ? candidate.failure->code + ": " + candidate.failure->message
              : "");
  EXPECT_TRUE(candidate.generation->actor_manager()->is_actor_loaded(
      "private_dependency_actor"));
}

TEST_F(RuntimeGenerationTest,
       AllPurposesReportTheSameContractAndActivationFailures) {
  auto invalid_document = valid_config(OBCX_TEST_ACTOR_V2_LIBRARY);
  const std::string valid_type = "obcx::tests::events::SdkSmoke";
  for (auto position = invalid_document.find(valid_type);
       position != std::string::npos;
       position = invalid_document.find(valid_type, position)) {
    invalid_document.replace(position, valid_type.size(), "wrong::Input");
    position += std::string{"wrong::Input"}.size();
  }
  const auto invalid_contract =
      snapshot("invalid-contract.toml", invalid_document);
  auto [database, registry] = services_for(invalid_contract);
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto process_blocking_executor =
      std::make_shared<obcx::core::BlockingExecutor>(1);

  for (const auto purpose :
       {obcx::core::RuntimeGenerationBuildPurpose::Startup,
        obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
        obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate}) {
    auto build_request =
        request(purpose, 10, invalid_contract, database, registry);
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
      build_request.blocking_executor = process_blocking_executor;
    }
    auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->code, "reload_contract_invalid");
  }

  const auto activation_failure =
      snapshot("activation-failure.toml",
               "[actors.activation_failure_actor]\n"
               "library = \"" OBCX_ACTIVATION_FAILURE_ACTOR "\"\n"
               "enabled = true\n\n"
               "[pipelines.sdk]\n"
               "source = \"obcx::tests::events::SdkSmoke\"\n\n"
               "[[pipelines.sdk.stages]]\n"
               "name = \"handle\"\n"
               "actor = \"activation_failure_actor\"\n"
               "input = \"obcx::tests::events::SdkSmoke\"\n");
  auto [activation_database, activation_registry] =
      services_for(activation_failure);
  for (const auto purpose :
       {obcx::core::RuntimeGenerationBuildPurpose::Startup,
        obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
        obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate}) {
    auto build_request = request(purpose, 20, activation_failure,
                                 activation_database, activation_registry);
    if (purpose == obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate) {
      build_request.blocking_executor = process_blocking_executor;
    }
    auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->code, "reload_activation_failed");
  }
}

TEST_F(RuntimeGenerationTest,
       UnknownSdkSchemaNeverConstructsOrPreparesAcrossAllPurposes) {
  constexpr auto variable = "OBCX_SCHEMA_GATE_PROBE_MARKER";
  const auto previous = std::getenv(variable);
  struct RestoreEnvironment {
    const char *key;
    std::optional<std::string> value;
    ~RestoreEnvironment() {
      if (value) {
        ::setenv(key, value->c_str(), 1);
      } else {
        ::unsetenv(key);
      }
    }
  } restore{variable,
            previous ? std::optional<std::string>{previous} : std::nullopt};
  const auto marker = root_ / "incompatible-sdk-called";
  ASSERT_EQ(::setenv(variable, marker.c_str(), 1), 0);
  const std::array purposes = {
      obcx::core::RuntimeGenerationBuildPurpose::Startup,
      obcx::core::RuntimeGenerationBuildPurpose::ValidationOnly,
      obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate};
  obcx::core::RuntimeGenerationBuilder builder{
      obcx::test::bot_platform_catalog()};
  auto blocking = std::make_shared<obcx::core::BlockingExecutor>(1);
  const auto config = snapshot("unsupported-schema.toml",
                               valid_config(OBCX_UNKNOWN_SCHEMA_PROBE));
  auto [database, registry] = services_for(config);
  for (const auto purpose : purposes) {
    auto build_request = request(purpose, 1, config, database, registry);
    build_request.blocking_executor = blocking;
    const auto result = builder.build(std::move(build_request));
    ASSERT_TRUE(result.failure);
    EXPECT_EQ(result.failure->code, "reload_contract_invalid");
    EXPECT_FALSE(fs::exists(marker));
  }
}

} // namespace
