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

TEST_F(ActorConfigTest, RejectsMalformedCommandMessageObservers) {
  const auto built = ActorConfigSnapshotBuilder::build(
      toml::parse(R"(
[command_runtime]
message_observers = [
  { actor = "", platforms = [], bots = ["primary"] },
  "invalid",
]
)"),
      {{.installation_id = "primary",
        .enabled = true,
        .surface = obcx::bot::SurfaceId{"telegram.bot_api"},
        .transport = "http",
        .ingress_platform = "telegram",
        .command_target = "fixture_bot"}},
      "invalid-message-observers.toml");
  ASSERT_TRUE(built);
  const auto errors = built.snapshot->validate_actor_runtime_config();
  const auto contains = [&errors](const std::string_view code) {
    return std::ranges::any_of(
        errors, [code](const auto &error) { return error.code == code; });
  };
  EXPECT_TRUE(contains("invalid_command_message_observer"));
  EXPECT_TRUE(contains("invalid_command_message_observer_actor"));
  EXPECT_TRUE(contains("invalid_command_message_observer_scope"));
  EXPECT_TRUE(contains("invalid_command_message_observer_timeout"));
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
[command_runtime.access.overrides.help]
groups = { mode = "invalid", entries = [] }
users = { mode = "unrestricted", entries = [], native_user_id = "bad" }
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
  for (const auto &error : errors) {
    EXPECT_EQ(error.message.find("secret-value-must-not-appear"),
              std::string::npos);
  }
}

TEST_F(ActorConfigTest, RejectsMalformedKeyedCommandAccessOverrides) {
  const std::string global_policies = R"(
[command_runtime.access.groups]
mode = "unrestricted"
entries = []
[command_runtime.access.users]
mode = "unrestricted"
entries = []
)";
  struct InvalidOverride {
    std::string document;
    std::string code;
    std::string dependency;
  };
  const std::vector<InvalidOverride> cases = {
      {R"(
[[command_runtime.access.overrides]]
command = "help"
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
)",
       "invalid_command_access_overrides", "command_runtime.access.overrides"},
      {"[command_runtime.access]\noverrides = []\n",
       "invalid_command_access_overrides", "command_runtime.access.overrides"},
      {"[command_runtime.access]\noverrides = false\n",
       "invalid_command_access_overrides", "command_runtime.access.overrides"},
      {"[command_runtime.access.overrides]\nhelp = 7\n",
       "invalid_command_access_override",
       "command_runtime.access.overrides.help"},
      {"[[command_runtime.access.overrides.help]]\n",
       "invalid_command_access_override",
       "command_runtime.access.overrides.help"},
      {R"([command_runtime.access.overrides."Bad!"])",
       "invalid_command_access_override_name",
       "command_runtime.access.overrides.Bad!"},
      {R"([command_runtime.access.overrides.""])",
       "invalid_command_access_override_name",
       "command_runtime.access.overrides."},
      {R"([command_runtime.access.overrides."/help"])",
       "invalid_command_access_override_name",
       "command_runtime.access.overrides./help"},
      {"[command_runtime.access.overrides." + std::string(33, 'a') + "]",
       "invalid_command_access_override_name",
       "command_runtime.access.overrides." + std::string(33, 'a')},
      {"[command_runtime.access.overrides.help]\ncommand = 'test'\n",
       "unknown_command_runtime_field",
       "command_runtime.access.overrides.help.command"},
      {"[command_runtime.access.overrides.help]\nactor = 'actor'\n",
       "unknown_command_runtime_field",
       "command_runtime.access.overrides.help.actor"},
      {"[command_runtime.access.overrides.help]\nusers = { mode = "
       "'unrestricted', entries = [] }\n",
       "missing_command_access_policy",
       "command_runtime.access.overrides.help.groups"},
      {"[command_runtime.access.overrides.help]\ngroups = { mode = "
       "'unrestricted', entries = [] }\n",
       "missing_command_access_policy",
       "command_runtime.access.overrides.help.users"},
      {"[command_runtime.access.overrides.help.groups]\nentries = []\n",
       "invalid_command_access_mode",
       "command_runtime.access.overrides.help.groups.mode"},
      {"[command_runtime.access.overrides.help.users]\nmode = 'unrestricted'\n",
       "invalid_command_access_entries",
       "command_runtime.access.overrides.help.users.entries"},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.document);
    const auto built = ActorConfigSnapshotBuilder::build(
        toml::parse(global_policies + test.document), {},
        "invalid-keyed-overrides.toml");
    ASSERT_TRUE(built);
    const auto errors = built.snapshot->validate_actor_runtime_config();
    const auto found =
        std::ranges::find(errors, test.code, &ConfigValidationError::code);
    ASSERT_NE(found, errors.end());
    EXPECT_EQ(found->dependency, test.dependency);
    if (test.code == "invalid_command_access_overrides") {
      EXPECT_NE(found->message.find(
                    "[command_runtime.access.overrides.<command>.groups]"),
                std::string::npos);
    }
  }
}

TEST_F(ActorConfigTest, RejectsDuplicateKeyedCommandOverrideTables) {
  EXPECT_THROW((void)toml::parse(R"(
[command_runtime.access.overrides.help]
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
[command_runtime.access.overrides.help]
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
)"),
               toml::parse_error);
  EXPECT_THROW((void)toml::parse(R"(
[command_runtime.access.overrides.help.groups]
mode = "unrestricted"
entries = []
[command_runtime.access.overrides.help.groups]
mode = "allowlist"
entries = []
)"),
               toml::parse_error);
}

TEST_F(ActorConfigTest, BoundsKeyedCommandAccessOverrides) {
  std::string document = R"(
[command_runtime.access.groups]
mode = "unrestricted"
entries = []
[command_runtime.access.users]
mode = "unrestricted"
entries = []
[command_runtime.access.overrides]
)";
  const auto validate = [&document]() {
    const auto built = ActorConfigSnapshotBuilder::build(
        toml::parse(document), {}, "bounded-keyed-overrides.toml");
    EXPECT_TRUE(built);
    return built.snapshot->validate_actor_runtime_config();
  };
  EXPECT_TRUE(validate().empty());
  for (std::size_t index = 0;
       index < CommandRuntimeConfig::max_access_overrides; ++index) {
    document += "\n[command_runtime.access.overrides.cmd_" +
                std::to_string(index) + "]\n" +
                "groups = { mode = 'unrestricted', entries = [] }\n" +
                "users = { mode = 'unrestricted', entries = [] }\n";
  }
  EXPECT_TRUE(validate().empty());
  document += R"(
[command_runtime.access.overrides.overflow]
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
)";
  const auto errors = validate();
  EXPECT_TRUE(std::ranges::any_of(errors, [](const auto &error) {
    return error.code == "command_access_overrides_exceed_limit";
  }));
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
