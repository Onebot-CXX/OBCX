#include "core/actor/actor_manager.hpp"
#include "core/actor/native_actor_scheduler.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>

using namespace obcx::core;

TEST(ActorManagerTest, LoadsActorFromDynamicLibraryPath) {
  ActorManager manager;
  ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
  EXPECT_TRUE(manager.is_actor_loaded("test_actor_v2"));

  const auto names = manager.get_loaded_actor_names();
  EXPECT_NE(std::ranges::find(names, std::string{"test_actor_v2"}),
            names.end());
  auto *actor = manager.get_actor("test_actor_v2");
  ASSERT_NE(actor, nullptr);
  EXPECT_EQ(actor->get_name(), "test_actor_v2");
  EXPECT_EQ(actor->get_version(), "2.0.0");
}

TEST(ActorManagerTest, LoadingSameActorPathIsIdempotent) {
  ActorManager manager;
  ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
  ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
  EXPECT_TRUE(manager.last_error().empty());
  EXPECT_EQ(manager.get_loaded_actor_names(),
            (std::vector<std::string>{"test_actor_v2"}));
}

TEST(ActorManagerTest, DiscoversContractWithoutConstructingActor) {
  ActorManager manager;
  ASSERT_TRUE(manager.discover_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
  EXPECT_FALSE(manager.is_actor_loaded("test_actor_v2"));
  EXPECT_EQ(manager.get_actor("test_actor_v2"), nullptr);
  const auto *contract = manager.get_actor_contract("test_actor_v2");
  ASSERT_NE(contract, nullptr);
  EXPECT_EQ(contract->schema_version, 2);
  EXPECT_EQ(contract->actor, "test_actor_v2");
  EXPECT_EQ(contract->accepted_inputs, (std::vector<std::string>{
                                           "obcx::tests::events::SdkCommand",
                                           "obcx::tests::events::SdkSmoke",
                                       }));
  ASSERT_EQ(contract->commands.size(), 1);
  EXPECT_EQ(contract->commands.front().name, "sdk_ping");
  EXPECT_EQ(contract->commands.front().description, "Ping the SDK fixture");
  EXPECT_EQ(contract->commands.front().request_type,
            "obcx::tests::events::SdkCommand");
  ASSERT_TRUE(contract->commands.front().matcher);
  EXPECT_EQ(contract->commands.front().matcher->kind, "re2");
  EXPECT_EQ(contract->commands.front().matcher->pattern,
            R"(^(?:sdk_ping|sdk_alias)$)");
  EXPECT_EQ(contract->commands.front().matcher->mode, "full");
  ASSERT_EQ(contract->integer_configuration.size(), 3);
  EXPECT_EQ(contract->integer_configuration.front().key, "positive_limit");
  EXPECT_EQ(contract->required_string_configuration,
            (std::vector<std::string>{"label"}));
  ASSERT_EQ(contract->bot_installation_configuration.size(), 1U);
  EXPECT_EQ(contract->bot_installation_configuration.front().key,
            "target_installation");
  EXPECT_EQ(contract->bot_installation_configuration.front().expected_types,
            (std::vector<std::string>{"onebot11.qq", "telegram.bot_api"}));
  EXPECT_EQ(contract->bot_installation_configuration.front().alternative_group,
            "target_form");
  ASSERT_EQ(contract->bot_installation_collection_configuration.size(), 1U);
  const auto &collection =
      contract->bot_installation_collection_configuration.front();
  EXPECT_EQ(collection.key, "target_installations");
  EXPECT_EQ(collection.minimum_items, 1U);
  EXPECT_EQ(collection.identity_key, "id");
  EXPECT_EQ(collection.alternative_group, "target_form");
  EXPECT_EQ(collection.unique_fields,
            (std::vector<std::string>{"target_installation"}));
  ASSERT_EQ(collection.installation_fields.size(), 1U);
  EXPECT_EQ(collection.installation_fields.front().key, "target_installation");
  EXPECT_EQ(collection.installation_fields.front().expected_types,
            (std::vector<std::string>{"onebot11.qq", "telegram.bot_api"}));
  ASSERT_EQ(contract->collection_identity_reference_configuration.size(), 1U);
  const auto &reference =
      contract->collection_identity_reference_configuration.front();
  EXPECT_EQ(reference.source_key, "selected_target");
  EXPECT_EQ(reference.target_collection, "target_installations");
  EXPECT_EQ(reference.target_identity, "id");
  EXPECT_TRUE(reference.optional);
  ASSERT_EQ(contract->less_equal_configuration.size(), 1);
  EXPECT_EQ(contract->less_equal_configuration.front().lesser, "retry_base");
  EXPECT_EQ(contract->less_equal_configuration.front().greater, "retry_max");

  ASSERT_TRUE(manager.activate_actor("test_actor_v2"));
  EXPECT_TRUE(manager.is_actor_loaded("test_actor_v2"));
}

TEST(ActorManagerTest, RejectsFrozenSchema1BeforeFactoryAndPreparation) {
  const auto close_library = [](void *handle) { (void)dlclose(handle); };
  std::unique_ptr<void, decltype(close_library)> library{
      dlopen(OBCX_TEST_FROZEN_SCHEMA1_LIBRARY, RTLD_NOW | RTLD_LOCAL),
      close_library};
  ASSERT_NE(library, nullptr);
  const auto factory_calls = reinterpret_cast<unsigned (*)()>(
      dlsym(library.get(), "obcx_frozen_factory_calls"));
  const auto preparation_calls = reinterpret_cast<unsigned (*)()>(
      dlsym(library.get(), "obcx_frozen_preparation_calls"));
  ASSERT_NE(factory_calls, nullptr);
  ASSERT_NE(preparation_calls, nullptr);
  ActorManager manager;
  EXPECT_FALSE(
      manager.discover_actor_from_path(OBCX_TEST_FROZEN_SCHEMA1_LIBRARY));
  EXPECT_NE(manager.last_error().find("rebuild"), std::string::npos);
  EXPECT_FALSE(manager.load_actor_from_path(OBCX_TEST_FROZEN_SCHEMA1_LIBRARY));
  EXPECT_NE(manager.last_error().find("schema_version 1"), std::string::npos);
  EXPECT_EQ(manager.get_actor_contract("frozen_schema1_actor"), nullptr);
  EXPECT_TRUE(manager.get_loaded_actor_names().empty());
  EXPECT_EQ(factory_calls(), 0U);
  EXPECT_EQ(preparation_calls(), 0U);
}

TEST(ActorManagerTest, RunsOptionalGenerationPreparationWithTypedStatus) {
  ActorManager manager;
  ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));

  ActorContext ready_context("test_actor_v2");
  EXPECT_TRUE(manager.prepare_actor("test_actor_v2", ready_context).ok());

  ActorContext failed_context("prepare-failed");
  const auto failed = manager.prepare_actor("test_actor_v2", failed_context);
  EXPECT_EQ(failed.status, ActorPreparationStatus::Failed);
  EXPECT_EQ(failed.message, "fixture preparation failed");

  ActorContext restart_context("prepare-restart");
  const auto restart = manager.prepare_actor("test_actor_v2", restart_context);
  EXPECT_EQ(restart.status, ActorPreparationStatus::RestartRequired);
  EXPECT_EQ(restart.message, "fixture preparation requires restart");
}

TEST(ActorManagerTest, LegacyV2ActorWithoutPreparationExportRemainsReady) {
  ActorManager manager;
  ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_LEGACY_V2_ACTOR_LIBRARY));
  ActorContext context("legacy_v2_actor");
  const auto preparation = manager.prepare_actor("legacy_v2_actor", context);
  EXPECT_TRUE(preparation.ok());
}

TEST(ActorManagerTest, FindsActorByNameInActorDirectory) {
  ActorManager manager;
  manager.add_actor_directory(OBCX_TEST_ACTOR_DIRECTORY);
  ASSERT_TRUE(manager.load_actor("test_actor_v2"));
  ASSERT_NE(manager.get_actor("test_actor_v2"), nullptr);
}

TEST(ActorManagerTest, LoadsActorWithSecondaryBaseClass) {
  ActorManager manager;
  ASSERT_TRUE(manager.load_actor_from_path(
      OBCX_TEST_MULTIPLE_INHERITANCE_ACTOR_LIBRARY));
  auto *actor = manager.get_actor("multiple_inheritance_actor");
  ASSERT_NE(actor, nullptr);
  EXPECT_EQ(actor->get_name(), "multiple_inheritance_actor");
}

TEST(ActorManagerTest, SharedActorKeepsLibraryLoadedAfterManagerDestruction) {
  std::shared_ptr<IActorV2> actor;
  {
    ActorManager manager;
    ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
    actor = manager.get_actor_shared("test_actor_v2");
    ASSERT_NE(actor, nullptr);
    manager.unload_actor("test_actor_v2");
    EXPECT_FALSE(manager.is_actor_loaded("test_actor_v2"));
  }
  EXPECT_EQ(actor->get_name(), "test_actor_v2");
  EXPECT_EQ(actor->get_version(), "2.0.0");
}

TEST(ActorManagerTest, RejectsLibrariesWithoutActorSymbols) {
  ActorManager manager;
  EXPECT_FALSE(manager.load_actor_from_path(OBCX_TEST_INVALID_ACTOR_LIBRARY));
  EXPECT_NE(manager.last_error().find("obcx_get_actor_abi_generation"),
            std::string::npos);
  EXPECT_TRUE(manager.get_loaded_actor_names().empty());
}

TEST(ActorManagerTest, RejectsUnsupportedAndIncompleteActorLibraries) {
  ActorManager manager;
  EXPECT_FALSE(
      manager.load_actor_from_path(OBCX_TEST_UNSUPPORTED_ACTOR_LIBRARY));
  EXPECT_NE(manager.last_error().find("unsupported actor ABI generation"),
            std::string::npos);
  EXPECT_FALSE(
      manager.load_actor_from_path(OBCX_TEST_MISSING_V2_FACTORY_LIBRARY));
  EXPECT_NE(manager.last_error().find("obcx_create_actor_v2"),
            std::string::npos);
}

TEST(ActorManagerTest, RejectsMissingAndMalformedActorContracts) {
  struct Rejection {
    const char *path;
    const char *diagnostic;
  };
  const Rejection rejections[] = {
      {OBCX_TEST_CONTRACT_MISSING_LIBRARY, "obcx_get_actor_contract"},
      {OBCX_TEST_CONTRACT_NULL_LIBRARY, "returned nullptr"},
      {OBCX_TEST_CONTRACT_INVALID_JSON_LIBRARY, "not valid JSON"},
      {OBCX_TEST_CONTRACT_SCHEMA_LIBRARY, "unsupported actor input contract"},
      {OBCX_TEST_CONTRACT_NAME_LIBRARY, "does not match exported actor name"},
      {OBCX_TEST_CONTRACT_DUPLICATE_LIBRARY, "duplicate input"},
      {OBCX_TEST_CONTRACT_OUTPUTS_LIBRARY, "must not declare outputs"},
      {OBCX_TEST_CONTRACT_MALFORMED_INPUT_LIBRARY, "malformed canonical input"},
      {OBCX_TEST_CONTRACT_COMMANDS_NOT_ARRAY_LIBRARY,
       "commands must be an array"},
      {OBCX_TEST_CONTRACT_DUPLICATE_COMMAND_LIBRARY, "duplicate command"},
      {OBCX_TEST_CONTRACT_UNSORTED_COMMAND_LIBRARY,
       "commands must be deterministic and sorted"},
      {OBCX_TEST_CONTRACT_COMMAND_CALLABLE_LIBRARY, "unsupported member"},
      {OBCX_TEST_CONTRACT_COMMAND_UNSUPPORTED_INPUT_LIBRARY,
       "not an accepted input"},
      {OBCX_TEST_CONTRACT_COMMAND_INVALID_NAME_LIBRARY, "invalid command name"},
      {OBCX_TEST_CONTRACT_COMMAND_RESERVED_NAME_LIBRARY,
       "reserved command name"},
      {OBCX_TEST_CONTRACT_COMMAND_INVALID_PATTERN_LIBRARY,
       "RE2 command pattern is invalid"},
      {OBCX_TEST_CONTRACT_COMMAND_MATCHER_CALLABLE_LIBRARY,
       "unsupported member"},
      {OBCX_TEST_CONTRACT_COMMAND_MATCHER_KIND_LIBRARY,
       "matcher kind is unsupported"},
      {OBCX_TEST_CONTRACT_COMMAND_PATTERN_TOO_LARGE_LIBRARY,
       "RE2 command pattern exceeds the fixed byte limit"},
      {OBCX_TEST_CONTRACT_COLLECTION_CALLABLE_LIBRARY, "unsupported member"},
      {OBCX_TEST_CONTRACT_COLLECTION_DUPLICATE_FIELD_LIBRARY,
       "invalid or duplicate item field"},
      {OBCX_TEST_CONTRACT_COLLECTION_DUPLICATE_TYPE_LIBRARY, "duplicate types"},
      {OBCX_TEST_CONTRACT_COLLECTION_INVALID_ALTERNATIVE_LIBRARY,
       "must contain one scalar form"},
      {OBCX_TEST_CONTRACT_COLLECTION_UNKNOWN_UNIQUE_FIELD_LIBRARY,
       "unknown item field"},
      {OBCX_TEST_CONTRACT_COLLECTION_UNKNOWN_REFERENCE_LIBRARY,
       "unknown collection identity"},
  };

  ActorManager manager;
  for (const auto &rejection : rejections) {
    EXPECT_FALSE(manager.load_actor_from_path(rejection.path));
    EXPECT_NE(manager.last_error().find(rejection.diagnostic),
              std::string::npos)
        << rejection.path << ": " << manager.last_error();
    EXPECT_TRUE(manager.get_loaded_actor_names().empty());
    EXPECT_TRUE(manager.get_discovered_actor_names().empty());
  }

  EXPECT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
  EXPECT_TRUE(manager.is_actor_loaded("test_actor_v2"));
}

TEST(ActorManagerTest, LoadedActorHandlesMessageOnNativeScheduler) {
  ActorManager manager;
  ASSERT_TRUE(manager.load_actor_from_path(OBCX_TEST_ACTOR_V2_LIBRARY));
  auto actor = manager.get_actor_shared("test_actor_v2");
  ASSERT_NE(actor, nullptr);

  NativeActorScheduler scheduler(
      NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(std::move(actor));
  MessageEnvelope message;
  message.id = "sdk-smoke";
  message.type = "obcx::tests::events::SdkSmoke";
  std::promise<ActorResult> completion;
  auto result = completion.get_future();
  ASSERT_TRUE(scheduler.enqueue(ActorInvocation{.actor_id = "test_actor_v2",
                                                .partition_key = "same",
                                                .message = std::move(message)},
                                [&completion](ActorResult actor_result) {
                                  completion.set_value(std::move(actor_result));
                                }));

  const auto handled = result.get();
  ASSERT_TRUE(handled.ok());
  ASSERT_EQ(handled.emitted.size(), 1);
  EXPECT_EQ(handled.emitted.front().type, "V2Handled");
  EXPECT_EQ(handled.emitted.front().causation_id, "sdk-smoke");
  scheduler.shutdown();
}
