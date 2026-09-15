#include "common/config_snapshot.hpp"
#include "core/actor/actor.hpp"
#include "support/bot_platform_fixture.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <type_traits>

using namespace obcx::common;

class ActorConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto *test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    ASSERT_NE(test_info, nullptr);
    test_config_dir_ = std::filesystem::temp_directory_path() /
                       "obcx_actor_config_test" / test_info->name();
    std::filesystem::remove_all(test_config_dir_);
    std::filesystem::create_directories(test_config_dir_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_config_dir_)) {
      std::filesystem::remove_all(test_config_dir_);
    }
  }

  void create_test_config(const std::string &content) {
    auto config_path = write_test_config("test_config.toml", content);
    auto &config_loader = loader_;
    ASSERT_TRUE(config_loader.load_config(config_path.string()));
  }

  auto write_test_config(const std::string &name, const std::string &content)
      -> std::filesystem::path {
    auto config_path = test_config_dir_ / name;
    std::ofstream ofs(config_path);
    ofs << content;
    ofs.close();
    return config_path;
  }

  ConfigLoader loader_{obcx::test::bot_platform_catalog()};
  std::filesystem::path test_config_dir_;
};

TEST_F(ActorConfigTest, CandidateParsingDoesNotMutatePublishedSnapshot) {
  const auto active_path = write_test_config("active.toml", R"(
[actors.bridge]
enabled = true

[actors.bridge.config]
bridge_files_dir = "/srv/active"
)");
  const auto candidate_path = write_test_config("candidate.toml", R"(
[actors.bridge]
enabled = true

[actors.bridge.config]
bridge_files_dir = "/srv/candidate"
)");

  auto &loader = loader_;
  ASSERT_TRUE(loader.load_config(active_path.string()));
  const auto active = loader.current_snapshot();
  ASSERT_NE(active, nullptr);

  const auto candidate = ConfigLoader::build_snapshot(
      candidate_path.string(), obcx::test::bot_platform_catalog());
  ASSERT_TRUE(candidate);
  EXPECT_EQ(loader.current_snapshot(), active);
  EXPECT_EQ(active->get_actor_value<std::string>("bridge", "bridge_files_dir"),
            "/srv/active");
  EXPECT_EQ(candidate.snapshot->get_actor_value<std::string>(
                "bridge", "bridge_files_dir"),
            "/srv/candidate");
}

TEST_F(ActorConfigTest, PublishedSnapshotsRemainImmutableAcrossReloads) {
  static_assert(std::is_same_v<decltype(loader_.current_snapshot()),
                               std::shared_ptr<const RuntimeConfigSnapshot>>);

  const auto first_path = write_test_config("first.toml", R"(
[actors.bridge.config]
bridge_files_dir = "/srv/first"
)");
  const auto second_path = write_test_config("second.toml", R"(
[actors.bridge.config]
bridge_files_dir = "/srv/second"
)");

  auto &loader = loader_;
  ASSERT_TRUE(loader.load_config(first_path.string()));
  const auto first = loader.current_snapshot();
  ASSERT_TRUE(loader.load_config(second_path.string()));

  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->get_actor_value<std::string>("bridge", "bridge_files_dir"),
            "/srv/first");
  EXPECT_EQ(
      loader.get_value<std::string>("actors.bridge.config.bridge_files_dir"),
      "/srv/second");
}

TEST_F(ActorConfigTest,
       ProcessFingerprintCoversDisabledBotsWithoutExposingSecrets) {
  const auto active_path = write_test_config("fingerprint-active.toml", R"(
[bots.disabled]
enabled = false
surface = "telegram.bot_api"
transport = "http"

[bots.disabled.connection]
host = "api.telegram.org"
port = 443
access_token = "active-secret-token"
bot_username = ""
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000

[db.instances.main]
type = "sqlite"
path = "state.db"
)");
  const auto candidate_path =
      write_test_config("fingerprint-candidate.toml", R"(
[bots.disabled]
enabled = false
surface = "telegram.bot_api"
transport = "http"

[bots.disabled.connection]
host = "api.telegram.org"
port = 443
access_token = "candidate-secret-token"
bot_username = ""
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000

[db.instances.main]
type = "sqlite"
path = "state.db"
)");

  const auto active = ConfigLoader::build_snapshot(
      active_path.string(), obcx::test::bot_platform_catalog());
  const auto candidate = ConfigLoader::build_snapshot(
      candidate_path.string(), obcx::test::bot_platform_catalog());
  ASSERT_TRUE(active);
  ASSERT_TRUE(candidate);
  const RuntimeThreadFingerprintInput budget{
      .actor_workers = 2,
      .io_workers = 1,
      .blocking_workers = 1,
  };
  const auto active_fingerprint =
      active.snapshot->process_owned_fingerprint(budget);
  const auto candidate_fingerprint =
      candidate.snapshot->process_owned_fingerprint(budget);

  EXPECT_NE(active_fingerprint, candidate_fingerprint);
  EXPECT_EQ(
      changed_process_owned_domains(active_fingerprint, candidate_fingerprint),
      (std::vector<std::string>{"bots"}));
  const auto description =
      describe_process_owned_changes(active_fingerprint, candidate_fingerprint);
  EXPECT_EQ(description, "bots");
  EXPECT_EQ(description.find("active-secret-token"), std::string::npos);
  EXPECT_EQ(description.find("candidate-secret-token"), std::string::npos);
}

TEST_F(ActorConfigTest, FailedCandidateParsePreservesActiveAndIsSecretSafe) {
  const auto active_path = write_test_config("valid.toml", R"(
[actors.bridge]
enabled = true
)");
  const auto invalid_path = write_test_config("invalid.toml", R"(
[bots.telegram.connection]
access_token = "do-not-log-this-token"
broken = [
)");

  auto &loader = loader_;
  ASSERT_TRUE(loader.load_config(active_path.string()));
  const auto active = loader.current_snapshot();
  const auto candidate = ConfigLoader::build_snapshot(
      invalid_path.string(), obcx::test::bot_platform_catalog());

  EXPECT_FALSE(candidate);
  ASSERT_TRUE(candidate.diagnostic.has_value());
  EXPECT_EQ(candidate.diagnostic->code, "config_parse_failed");
  EXPECT_EQ(candidate.diagnostic->path, invalid_path.string());
  EXPECT_EQ(loader.current_snapshot(), active);
  EXPECT_EQ(candidate.diagnostic->code.find("do-not-log-this-token"),
            std::string::npos);
}

TEST_F(ActorConfigTest, ActorContextExposesGenerationScopedConfigView) {
  const auto config_path = write_test_config("actor-view.toml", R"(
[actors.bridge.config]
bridge_files_dir = "/srv/generation"

[group_mappings]
owner = "bridge"
)");
  const auto built = ConfigLoader::build_snapshot(
      config_path.string(), obcx::test::bot_platform_catalog());
  ASSERT_TRUE(built);

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<ActorConfigService>(
      std::make_shared<ActorConfigService>(built.snapshot));
  obcx::core::ActorContext context("bridge", services);
  const auto view = context.config();

  ASSERT_TRUE(view.available());
  EXPECT_EQ(view.get_value<std::string>("bridge_files_dir"), "/srv/generation");
  const auto mappings = view.get_root_section("group_mappings");
  ASSERT_TRUE(mappings.has_value());
  EXPECT_EQ(mappings->get("owner")->value_or<std::string>(""), "bridge");
}

TEST_F(ActorConfigTest, ParsesExplicitCommandRuntimeRoutes) {
  const auto config_path = write_test_config("commands.toml", R"(
[command_runtime]
timeout_ms = 6000

[command_runtime.help]
page_bytes = 3500
maximum_pages = 10

[command_runtime.access.groups]
mode = "unrestricted"
entries = []

[command_runtime.access.users]
mode = "unrestricted"
entries = []

[[command_runtime.routes]]
actor = "chat_llm"
commands = ["chat", "toggle_think"]
platforms = ["telegram", "qq"]
bots = ["telegram_bot", "qq_bot"]
fallback = "consume"
timeout_ms = 1500
)");
  const auto built = ConfigLoader::build_snapshot(
      config_path.string(), obcx::test::bot_platform_catalog());
  ASSERT_TRUE(built);
  const auto commands = built.snapshot->get_command_runtime_config();
  EXPECT_EQ(commands.timeout_ms, 6000);
  ASSERT_EQ(commands.routes.size(), 1);
  const auto &route = commands.routes.front();
  EXPECT_EQ(route.actor, "chat_llm");
  EXPECT_EQ(route.commands, (std::vector<std::string>{"chat", "toggle_think"}));
  EXPECT_EQ(route.platforms, (std::vector<std::string>{"telegram", "qq"}));
  EXPECT_EQ(route.bots, (std::vector<std::string>{"telegram_bot", "qq_bot"}));
  EXPECT_EQ(route.fallback, CommandFallback::Consume);
  EXPECT_EQ(route.timeout_ms, 1500);
  EXPECT_TRUE(built.snapshot->validate_actor_runtime_config().empty());
}

TEST_F(ActorConfigTest, ParsesExplicitAccessPoliciesAndOverrides) {
  const auto built = ActorConfigSnapshotBuilder::build(
      toml::parse(R"(
[command_runtime]
timeout_ms = 6000

[command_runtime.help]
page_bytes = 2048
maximum_pages = 4

[command_runtime.access.groups]
mode = "allowlist"
entries = [
  { platform = "telegram", bot = "primary", native_group_id = "-1001" },
]

[command_runtime.access.users]
mode = "denylist"
entries = [
  { platform = "telegram", bot = "primary", native_user_id = "7" },
]

[[command_runtime.access.overrides]]
command = "help"
groups = { mode = "unrestricted", entries = [] }
users = { mode = "allowlist", entries = [{ platform = "telegram", bot = "primary", native_user_id = "8" }] }

[[command_runtime.routes]]
actor = "chat_llm"
commands = ["chat"]
platforms = ["telegram"]
bots = ["primary"]
fallback = "consume"
)"),
      {{.installation_id = "primary",
        .enabled = true,
        .surface = obcx::bot::SurfaceId{"telegram.bot_api"},
        .transport = "http",
        .ingress_platform = "telegram",
        .command_target = "fixture_bot"}},
      "access-fixture.toml");
  ASSERT_TRUE(built);
  EXPECT_TRUE(built.snapshot->validate_actor_runtime_config().empty());
  const auto runtime = built.snapshot->get_command_runtime_config();
  ASSERT_EQ(runtime.help.page_bytes, 2048U);
  ASSERT_EQ(runtime.help.maximum_pages, 4U);
  ASSERT_EQ(runtime.access.groups.mode, CommandAccessMode::Allowlist);
  ASSERT_EQ(runtime.access.groups.entries.size(), 1U);
  EXPECT_EQ(runtime.access.groups.entries.front().native_group_id, "-1001");
  ASSERT_EQ(runtime.access.users.mode, CommandAccessMode::Denylist);
  ASSERT_EQ(runtime.access.overrides.size(), 1U);
  EXPECT_EQ(runtime.access.overrides.front().command, "help");
  EXPECT_EQ(runtime.access.overrides.front().users.mode,
            CommandAccessMode::Allowlist);
}

TEST_F(ActorConfigTest, RejectsMissingMalformedAndUnsafeAccessConfiguration) {
  const std::vector<BotInstallationMetadata> bots = {
      {.installation_id = "primary",
       .enabled = true,
       .surface = obcx::bot::SurfaceId{"onebot11.qq"},
       .transport = "http",
       .ingress_platform = "qq",
       .command_target = {}},
  };
  const auto validate = [&bots](const std::string &document) {
    auto built = ActorConfigSnapshotBuilder::build(
        toml::parse(document), bots, "invalid-access-fixture.toml");
    EXPECT_TRUE(built);
    return built.snapshot->validate_actor_runtime_config();
  };
  const auto contains = [](const auto &errors, const std::string_view code) {
    return std::ranges::any_of(
        errors, [code](const auto &error) { return error.code == code; });
  };

  auto errors = validate(R"(
[command_runtime]
timeout_ms = 5000
[[command_runtime.routes]]
actor = "actor"
commands = ["test"]
platforms = ["qq"]
bots = ["primary"]
fallback = "consume"
)");
  EXPECT_TRUE(contains(errors, "missing_command_help_configuration"));
  EXPECT_TRUE(contains(errors, "missing_command_access_configuration"));

  errors = validate(R"(
[command_runtime]
timeout_ms = 5000
unknown = "secret-value-must-not-appear"
[command_runtime.help]
page_bytes = 0
maximum_pages = 101
extra = true
[command_runtime.access]
extra = true
[command_runtime.access.groups]
mode = "unrestricted"
entries = [{ platform = "qq", bot = "primary", native_group_id = "42" }]
[command_runtime.access.users]
mode = "allowlist"
entries = [
  { platform = "telegram", bot = "primary", native_user_id = "7" },
  { platform = "telegram", bot = "primary", native_user_id = "7" },
]
[[command_runtime.access.overrides]]
command = "help"
groups = { mode = "invalid", entries = [] }
users = { mode = "unrestricted", entries = [], native_user_id = "bad" }
[[command_runtime.access.overrides]]
command = "help"
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
[[command_runtime.routes]]
actor = "actor"
commands = ["test"]
platforms = ["qq"]
bots = ["primary"]
fallback = "consume"
unknown = "field"
)");
  EXPECT_TRUE(contains(errors, "unknown_command_runtime_field"));
  EXPECT_TRUE(contains(errors, "invalid_command_help_bound"));
  EXPECT_TRUE(contains(errors, "unrestricted_command_access_has_entries"));
  EXPECT_TRUE(contains(errors, "command_access_installation_mismatch"));
  EXPECT_TRUE(contains(errors, "duplicate_command_access_identity"));
  EXPECT_TRUE(contains(errors, "invalid_command_access_mode"));
  EXPECT_TRUE(contains(errors, "duplicate_command_access_override"));
  for (const auto &error : errors) {
    EXPECT_EQ(error.message.find("secret-value-must-not-appear"),
              std::string::npos);
  }
}

TEST_F(ActorConfigTest, RejectsMalformedCommandRuntimeRoutes) {
  const auto config_path = write_test_config("invalid-commands.toml", R"(
[command_runtime]
timeout_ms = 1

[command_runtime.help]
page_bytes = 3500
maximum_pages = 10

[command_runtime.access.groups]
mode = "unrestricted"
entries = []

[command_runtime.access.users]
mode = "unrestricted"
entries = []

[[command_runtime.routes]]
actor = ""
commands = ["Bad!"]
platforms = []
bots = ["telegram_bot"]
fallback = "actor"
timeout_ms = 999999
)");
  const auto built = ConfigLoader::build_snapshot(
      config_path.string(), obcx::test::bot_platform_catalog());
  ASSERT_TRUE(built);
  const auto errors = built.snapshot->validate_actor_runtime_config();
  const auto contains = [&errors](const std::string_view code) {
    return std::ranges::any_of(
        errors, [code](const auto &error) { return error.code == code; });
  };
  EXPECT_TRUE(contains("invalid_command_timeout"));
  EXPECT_TRUE(contains("invalid_command_actor"));
  EXPECT_TRUE(contains("invalid_command_scope"));
  EXPECT_TRUE(contains("invalid_command_name"));
  EXPECT_TRUE(contains("invalid_command_fallback"));
}

TEST_F(ActorConfigTest, ParsesActorConfigs) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true
requires = ["sqlite", "metrics"]
partition = "source_platform:group_id"
db = "main"
db_namespace = "message_store"

[actors.bridge]
library = "bridge"
enabled = false
partition = "global"
db = "main"
db_namespace = "bridge"
)");

  const auto actors = loader_.get_actor_configs();

  ASSERT_EQ(actors.size(), 2);
  const auto message_store =
      std::find_if(actors.begin(), actors.end(), [](const auto &actor) {
        return actor.name == "message_store";
      });
  ASSERT_NE(message_store, actors.end());
  EXPECT_EQ(message_store->library, "message_store");
  EXPECT_TRUE(message_store->enabled);
  EXPECT_EQ(message_store->required,
            (std::vector<std::string>{"sqlite", "metrics"}));
  EXPECT_EQ(message_store->partition, "source_platform:group_id");
  EXPECT_EQ(message_store->db, "main");
  EXPECT_EQ(message_store->db_namespace, "message_store");

  const auto bridge =
      std::find_if(actors.begin(), actors.end(),
                   [](const auto &actor) { return actor.name == "bridge"; });
  ASSERT_NE(bridge, actors.end());
  EXPECT_EQ(bridge->library, "bridge");
  EXPECT_FALSE(bridge->enabled);
  EXPECT_TRUE(bridge->required.empty());
  EXPECT_EQ(bridge->partition, "global");
  EXPECT_EQ(bridge->db, "main");
  EXPECT_EQ(bridge->db_namespace, "bridge");
}

TEST_F(ActorConfigTest, ParsesDbInstances) {
  create_test_config(R"(
[db.instances.main]
type = "sqlite"
path = "data/obcx.sqlite3"

[db.instances.audit]
type = "postgres"
dsn = "postgres://obcx@example.local/audit"
)");

  const auto db_instances = loader_.get_db_instance_configs();

  ASSERT_EQ(db_instances.size(), 2);
  const auto main =
      std::find_if(db_instances.begin(), db_instances.end(),
                   [](const auto &db) { return db.name == "main"; });
  ASSERT_NE(main, db_instances.end());
  EXPECT_EQ(main->type, "sqlite");
  EXPECT_EQ(main->path, "data/obcx.sqlite3");
  EXPECT_TRUE(main->dsn.empty());

  const auto audit =
      std::find_if(db_instances.begin(), db_instances.end(),
                   [](const auto &db) { return db.name == "audit"; });
  ASSERT_NE(audit, db_instances.end());
  EXPECT_EQ(audit->type, "postgres");
  EXPECT_EQ(audit->dsn, "postgres://obcx@example.local/audit");
  EXPECT_TRUE(audit->path.empty());
}

TEST_F(ActorConfigTest, ParsesPipelineConfigsWithStringAndArrayOutputs) {
  create_test_config(R"(
[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.message.stages]]
name = "fanout"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = ["QQForwarded", "TelegramForwarded"]
after = ["persist"]
mode = "async"
)");

  const auto pipelines = loader_.get_pipeline_configs();

  ASSERT_EQ(pipelines.size(), 1);
  EXPECT_EQ(pipelines[0].name, "message");
  EXPECT_EQ(pipelines[0].source, "obcx::core::events::RawMessageEvent");
  ASSERT_EQ(pipelines[0].stages.size(), 2);

  const auto &persist = pipelines[0].stages[0];
  EXPECT_EQ(persist.name, "persist");
  EXPECT_EQ(persist.actor, "message_store");
  EXPECT_EQ(persist.input, "obcx::core::events::RawMessageEvent");
  EXPECT_EQ(
      persist.outputs,
      (std::vector<std::string>{"obcx::message_store::events::MessageStored"}));
  EXPECT_TRUE(persist.after.empty());
  EXPECT_EQ(persist.mode, "await");

  const auto &fanout = pipelines[0].stages[1];
  EXPECT_EQ(fanout.name, "fanout");
  EXPECT_EQ(fanout.actor, "bridge");
  EXPECT_EQ(fanout.input, "obcx::message_store::events::MessageStored");
  EXPECT_EQ(fanout.outputs,
            (std::vector<std::string>{"QQForwarded", "TelegramForwarded"}));
  EXPECT_EQ(fanout.after, (std::vector<std::string>{"persist"}));
  EXPECT_EQ(fanout.mode, "async");
}

TEST_F(ActorConfigTest, DefaultsNativeActorRuntimeSettings) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true
)");

  const auto runtime = loader_.get_actor_runtime_config();
  EXPECT_EQ(runtime.policy, ActorSchedulerPolicy::Stealing);
  EXPECT_EQ(runtime.workers, 0);
  EXPECT_EQ(runtime.blocking_workers, 0);
  EXPECT_EQ(runtime.slow_resume_warning_ms, 10);
  EXPECT_EQ(runtime.routing_hop_limit, 32);
  EXPECT_EQ(runtime.reload_drain_timeout_ms, 5000);
}

TEST_F(ActorConfigTest, ParsesNativeActorRuntimeConfig) {
  create_test_config(R"(
[actor_runtime.scheduler]
policy = "sharing"
workers = 6
blocking_workers = 3
slow_resume_warning_ms = 25

[actor_runtime.routing]
hop_limit = 48

[actor_runtime.reload]
drain_timeout_ms = 7500
)");

  const auto runtime = loader_.get_actor_runtime_config();
  EXPECT_EQ(runtime.policy, ActorSchedulerPolicy::Sharing);
  EXPECT_EQ(runtime.workers, 6);
  EXPECT_EQ(runtime.blocking_workers, 3);
  EXPECT_EQ(runtime.slow_resume_warning_ms, 25);
  EXPECT_EQ(runtime.routing_hop_limit, 48);
  EXPECT_EQ(runtime.reload_drain_timeout_ms, 7500);
  EXPECT_TRUE(loader_.validate_actor_runtime_config().empty());
}

TEST_F(ActorConfigTest, RejectsInvalidActorRuntimeConfig) {
  create_test_config(R"(
[actor_runtime.scheduler]
policy = "random"
workers = -1
blocking_workers = -2
slow_resume_warning_ms = "fast"

[actor_runtime.routing]
hop_limit = 0

[actor_runtime.reload]
drain_timeout_ms = 99
)");

  const auto errors = loader_.validate_actor_runtime_config();
  ASSERT_EQ(errors.size(), 6);
  EXPECT_EQ(errors[0].code, "invalid_actor_scheduler_policy");
  EXPECT_EQ(errors[1].code, "invalid_actor_worker_count");
  EXPECT_EQ(errors[2].code, "invalid_blocking_worker_count");
  EXPECT_EQ(errors[3].code, "invalid_slow_resume_warning");
  EXPECT_EQ(errors[4].code, "invalid_routing_hop_limit");
  EXPECT_EQ(errors[5].code, "invalid_reload_drain_timeout");
}

TEST_F(ActorConfigTest,
       ReturnsEmptyActorAndPipelineConfigsWhenSectionsMissing) {
  create_test_config(R"(
[unrelated.bridge]
enabled = true
)");

  EXPECT_TRUE(loader_.get_actor_configs().empty());
  EXPECT_TRUE(loader_.get_pipeline_configs().empty());
}

TEST_F(ActorConfigTest, ParsesTypedBotConnectionWithoutExtensionLists) {
  create_test_config(R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq.connection]
host = "localhost"
port = 3001
access_token = ""
connect_timeout_ms = 5000
action_timeout_ms = 30000

[actors.message_store]
library = "message_store"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"
)");

  const auto bots = loader_.get_bot_configs();
  ASSERT_EQ(bots.size(), 1);
  EXPECT_EQ(bots[0].installation_id, "qq");
  EXPECT_EQ(bots[0].surface, obcx::bot::SurfaceId{"onebot11.qq"});
  EXPECT_EQ(bots[0].transport, "websocket");
  EXPECT_TRUE(bots[0].enabled);
  EXPECT_EQ(bots[0].ingress_platform, "qq");
  EXPECT_TRUE(bots[0].command_target.empty());
  EXPECT_FALSE(loader_.get_section("bots.qq.connection").has_value());
}

TEST_F(ActorConfigTest, ValidatesActorPipelineReferences) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = "bridge::events::MessageForwarded"
after = ["missing_stage"]
mode = "await"
)");

  const auto errors = loader_.validate_actor_pipeline_configs();

  ASSERT_EQ(errors.size(), 2);
  EXPECT_EQ(errors[0].code, "missing_actor");
  EXPECT_EQ(errors[0].pipeline, "message");
  EXPECT_EQ(errors[0].stage, "forward");
  EXPECT_EQ(errors[0].actor, "bridge");

  EXPECT_EQ(errors[1].code, "missing_stage_dependency");
  EXPECT_EQ(errors[1].pipeline, "message");
  EXPECT_EQ(errors[1].stage, "forward");
  EXPECT_EQ(errors[1].dependency, "missing_stage");
}

TEST_F(ActorConfigTest, ValidatesActorDbReferences) {
  create_test_config(R"(
[db.instances.main]
type = "sqlite"
path = "data/obcx.sqlite3"

[actors.message_store]
library = "message_store"
enabled = true
db = "missing"
db_namespace = "message_store"
)");

  const auto errors = loader_.validate_actor_pipeline_configs();

  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].code, "missing_db_instance");
  EXPECT_EQ(errors[0].actor, "message_store");
  EXPECT_EQ(errors[0].dependency, "missing");
}

TEST_F(ActorConfigTest, IgnoresDbReferencesForDisabledActors) {
  create_test_config(R"(
[actors.archived]
library = "archived"
enabled = false
db = "missing"
)");

  EXPECT_TRUE(loader_.validate_actor_pipeline_configs().empty());
}

TEST_F(ActorConfigTest, ValidatesPipelineDependencyCycles) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[actors.bridge]
library = "bridge"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
after = ["forward"]
mode = "await"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = "bridge::events::MessageForwarded"
after = ["persist"]
mode = "await"
)");

  const auto errors = loader_.validate_actor_pipeline_configs();

  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].code, "stage_dependency_cycle");
  EXPECT_EQ(errors[0].pipeline, "message");
}

TEST_F(ActorConfigTest, ValidActorPipelineConfigsHaveNoValidationErrors) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[actors.bridge]
library = "bridge"
enabled = true
requires = ["message_store"]

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "await"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "obcx::message_store::events::MessageStored"
output = "bridge::events::MessageForwarded"
after = ["persist"]
mode = "await"
)");

  EXPECT_TRUE(loader_.validate_actor_pipeline_configs().empty());
}

TEST_F(ActorConfigTest, ValidatesActorDependencies) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true
requires = ["missing"]

[actors.bridge]
library = "bridge"
enabled = true
requires = ["audit"]

[actors.audit]
library = "audit"
enabled = true
requires = ["bridge"]
)");

  const auto errors = loader_.validate_actor_pipeline_configs();
  ASSERT_EQ(errors.size(), 2);
  EXPECT_EQ(errors[0].code, "missing_actor_dependency");
  EXPECT_EQ(errors[0].actor, "message_store");
  EXPECT_EQ(errors[0].dependency, "missing");
  EXPECT_EQ(errors[1].code, "actor_dependency_cycle");
}

TEST_F(ActorConfigTest, RejectsDuplicateStageNames) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
)");

  const auto errors = loader_.validate_actor_pipeline_configs();
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors.front().code, "duplicate_stage_name");
  EXPECT_EQ(errors.front().pipeline, "message");
  EXPECT_EQ(errors.front().stage, "persist");
}

TEST_F(ActorConfigTest, RejectsUnknownSourcesAndStageModes) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEven"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "obcx::message_store::events::MessageStored"
mode = "asyc"
)");

  const auto errors = loader_.validate_actor_pipeline_configs();
  ASSERT_EQ(errors.size(), 2);
  EXPECT_EQ(errors[0].code, "unknown_pipeline_source");
  EXPECT_EQ(errors[0].input, "obcx::core::events::RawMessageEven");
  EXPECT_EQ(errors[1].code, "invalid_stage_mode");
  EXPECT_EQ(errors[1].stage, "persist");
  EXPECT_EQ(errors[1].dependency, "asyc");
}

TEST_F(ActorConfigTest, AcceptsRawNoticeEventAsRuntimeIngress) {
  create_test_config(R"(
[actors.bridge]
library = "bridge"
enabled = true

[pipelines.notice]
source = "obcx::core::events::RawNoticeEvent"

[[pipelines.notice.stages]]
name = "observe"
actor = "bridge"
input = "obcx::core::events::RawNoticeEvent"
mode = "await"
)");

  const auto errors = loader_.validate_actor_pipeline_configs();
  EXPECT_TRUE(errors.empty());
}

TEST_F(ActorConfigTest, AcceptsConfiguredActorInputAsCustomIngress) {
  create_test_config(R"(
[actors.sdk]
library = "sdk"
enabled = true

[pipelines.sdk]
source = "obcx::tests::events::SdkSmoke"

[[pipelines.sdk.stages]]
name = "handle"
actor = "sdk"
input = "obcx::tests::events::SdkSmoke"
mode = "await"
)");

  EXPECT_TRUE(loader_.validate_actor_pipeline_configs().empty());
}

TEST_F(ActorConfigTest, ValidatesConfiguredInputsAgainstActorContracts) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[actors.bridge]
library = "bridge"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = "not::an::inferred::contract"

[[pipelines.message.stages]]
name = "forward"
actor = "bridge"
input = "wrong::Input"
output = "also::not::validated"
after = ["persist"]
)");

  const std::unordered_map<std::string, std::unordered_set<std::string>>
      contracts = {
          {"message_store", {"obcx::core::events::RawMessageEvent"}},
          {"bridge", {"obcx::message_store::events::MessageStored"}},
      };
  const auto errors = loader_.validate_actor_pipeline_contracts(contracts);
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors.front().code, "unsupported_actor_input");
  EXPECT_EQ(errors.front().pipeline, "message");
  EXPECT_EQ(errors.front().stage, "forward");
  EXPECT_EQ(errors.front().actor, "bridge");
  EXPECT_EQ(errors.front().input, "wrong::Input");
}

TEST_F(ActorConfigTest, DoesNotInferOutputSetsOrBusinessReachability) {
  create_test_config(R"(
[actors.message_store]
library = "message_store"
enabled = true

[pipelines.message]
source = "obcx::core::events::RawMessageEvent"

[[pipelines.message.stages]]
name = "persist"
actor = "message_store"
input = "obcx::core::events::RawMessageEvent"
output = ["unreachable::One", "unreachable::Two"]
)");

  const std::unordered_map<std::string, std::unordered_set<std::string>>
      contracts = {
          {"message_store", {"obcx::core::events::RawMessageEvent"}},
      };
  EXPECT_TRUE(loader_.validate_actor_pipeline_configs().empty());
  EXPECT_TRUE(loader_.validate_actor_pipeline_contracts(contracts).empty());
}
