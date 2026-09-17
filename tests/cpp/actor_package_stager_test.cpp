#include "core/actor/actor_manager.hpp"
#include "core/actor/actor_package_stager.hpp"
#include "core/actor/native_actor_scheduler.hpp"
#include "core/infrastructure/process_staging_uuid.hpp"

#include <boost/uuid/string_generator.hpp>
#include <chrono>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace {

auto staging_root() -> std::filesystem::path {
  return std::filesystem::temp_directory_path() /
         ("obcx-actor-stager-test-" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
}

auto invoke(const std::shared_ptr<obcx::core::IActorV2> &actor, std::string id)
    -> int {
  obcx::core::NativeActorScheduler scheduler(
      obcx::core::NativeActorSchedulerOptions{.worker_count = 2});
  scheduler.register_actor(actor);
  obcx::core::MessageEnvelope message;
  message.id = std::move(id);
  message.type = "obcx::tests::events::PrivateDependencyProbe";
  std::promise<obcx::core::ActorResult> completion;
  auto future = completion.get_future();
  EXPECT_TRUE(scheduler.enqueue(
      obcx::core::ActorInvocation{.actor_id = "private_dependency_actor",
                                  .partition_key = "same",
                                  .message = std::move(message)},
      [&completion](obcx::core::ActorResult result) {
        completion.set_value(std::move(result));
      }));
  EXPECT_EQ(future.wait_for(std::chrono::seconds{5}),
            std::future_status::ready);
  auto result = future.get();
  scheduler.shutdown();
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.emitted.size(), 1);
  return result.emitted.front().payload.value("value", -1);
}

} // namespace

TEST(ActorPackageStagerTest,
     RewritesSameSonameDependenciesToContentVersionedIdentities) {
  const auto root = staging_root();
  obcx::core::ActorPackageStager stager;

  auto first = stager.stage({.actor_library = OBCX_PRIVATE_ACTOR_V1,
                             .staging_root = root,
                             .actor_name = "private_dependency_actor",
                             .generation_id = 1});
  ASSERT_TRUE(first) << first.code << ": " << first.message;
  EXPECT_NE(first.package->root().filename().string().find(
                obcx::core::detail::process_staging_uuid()),
            std::string::npos);
  ASSERT_EQ(first.package->private_dependencies().size(), 1);
  const auto &first_dependency = first.package->private_dependencies().front();
  EXPECT_NE(first_dependency.original_identity,
            first_dependency.staged_identity);
  EXPECT_NE(first_dependency.staged_identity.find(first_dependency.digest),
            std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(first_dependency.staged_path));

  obcx::core::ActorManager first_manager;
  ASSERT_TRUE(first_manager.load_actor_from_path(
      first.package->actor_library().string()))
      << first_manager.last_error();
  auto first_actor = first_manager.get_actor_shared("private_dependency_actor");
  ASSERT_NE(first_actor, nullptr);
  EXPECT_EQ(invoke(first_actor, "first-before"), 1);

  auto second = stager.stage({.actor_library = OBCX_PRIVATE_ACTOR_V2,
                              .staging_root = root,
                              .actor_name = "private_dependency_actor",
                              .generation_id = 2,
                              .expected_process_owned_dependencies =
                                  first.package->process_owned_dependencies()});
  ASSERT_TRUE(second) << second.code << ": " << second.message;
  ASSERT_EQ(second.package->private_dependencies().size(), 1);
  EXPECT_NE(first_dependency.staged_identity,
            second.package->private_dependencies().front().staged_identity);

  obcx::core::ActorManager second_manager;
  ASSERT_TRUE(second_manager.load_actor_from_path(
      second.package->actor_library().string()))
      << second_manager.last_error();
  auto second_actor =
      second_manager.get_actor_shared("private_dependency_actor");
  ASSERT_NE(second_actor, nullptr);

  EXPECT_EQ(invoke(second_actor, "second"), 2);
  EXPECT_EQ(invoke(first_actor, "first-after"), 1);

  std::filesystem::remove_all(root);
}

TEST(ProcessStagingUuidTest, IsOneCanonicalRandomUuidForTheProcess) {
  const auto first = obcx::core::detail::process_staging_uuid();
  const auto second = obcx::core::detail::process_staging_uuid();

  EXPECT_EQ(first.data(), second.data());
  EXPECT_EQ(first, second);
  const auto parsed =
      boost::uuids::string_generator{}(first.begin(), first.end());
  EXPECT_EQ(parsed.version(), boost::uuids::uuid::version_random_number_based);
  EXPECT_EQ(parsed.variant(), boost::uuids::uuid::variant_rfc_4122);
}

TEST(ActorPackageStagerTest, FailedAndDestroyedStagesCleanTheirOwnDirectory) {
  const auto root = staging_root();
  std::filesystem::path staged_root;
  {
    obcx::core::ActorPackageStager stager;
    auto staged = stager.stage({.actor_library = OBCX_PRIVATE_ACTOR_V1,
                                .staging_root = root,
                                .actor_name = "cleanup",
                                .generation_id = 7});
    ASSERT_TRUE(staged) << staged.message;
    staged_root = staged.package->root();
    EXPECT_TRUE(std::filesystem::exists(staged_root));
  }
  EXPECT_FALSE(std::filesystem::exists(staged_root));

  obcx::core::ActorPackageStager stager;
  auto failed = stager.stage({.actor_library = root / "missing.so",
                              .staging_root = root,
                              .actor_name = "missing",
                              .generation_id = 8});
  EXPECT_FALSE(failed);
  EXPECT_EQ(failed.code, "reload_dependency_identity_conflict");
  std::filesystem::remove_all(root);
}
