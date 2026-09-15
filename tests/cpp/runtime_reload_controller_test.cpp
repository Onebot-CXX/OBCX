#include "common/config_snapshot.hpp"
#include "core/actor/actor_generation_lifecycle.hpp"
#include "core/actor/actor_manager.hpp"
#include "core/actor/native_actor_scheduler.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/typed_operation.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "core/runtime/actor_runtime_reload_controller.hpp"
#include "core/runtime/orchestrator.hpp"
#include "support/bot_platform_fixture.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace {

namespace asio = boost::asio;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

template <typename Predicate>
auto wait_until(Predicate predicate,
                const std::chrono::milliseconds timeout = 3s) -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return predicate();
}

void write_file(const fs::path &path, const std::string &value = "open") {
  std::ofstream output(path);
  output << value;
}

auto read_file(const fs::path &path) -> std::string {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto observed_generation(const obcx::core::OrchestratorResult &result)
    -> std::string {
  for (const auto &emitted : result.emitted) {
    if (emitted.type == "obcx::tests::events::ReloadEmission") {
      return emitted.payload.value("generation", std::string{});
    }
  }
  return {};
}

auto observed_private_dependency(const obcx::core::OrchestratorResult &result)
    -> int {
  for (const auto &emitted : result.emitted) {
    if (emitted.type == "PrivateDependencyObserved") {
      return emitted.payload.value("value", -1);
    }
  }
  return -1;
}

TEST(RuntimeReloadOperatorSummaryTest, HighlightsSuccessBeforeDetails) {
  const obcx::core::RuntimeReloadResult result{
      .status = obcx::core::RuntimeReloadStatus::Succeeded,
      .attempt_id = 3,
      .previous_generation_id = 3,
      .active_generation_id = 4,
      .preparation_ms = 3118,
      .drain_ms = 646,
      .total_ms = 3769,
  };

  EXPECT_EQ(obcx::core::runtime_reload_operator_summary(result),
            "========== ACTOR RELOAD SUCCEEDED | generation 3 -> 4 | "
            "attempt 3 | total 3769 ms ==========");
}

TEST(RuntimeReloadOperatorSummaryTest, MakesRestartActionExplicit) {
  const obcx::core::RuntimeReloadResult result{
      .status = obcx::core::RuntimeReloadStatus::Failed,
      .attempt_id = 4,
      .previous_generation_id = 3,
      .active_generation_id = 3,
      .failure =
          obcx::core::RuntimeReloadFailure{
              .code = "reload_dependency_identity_conflict",
              .message = "process-owned dependency identity changed"},
  };

  const auto summary = obcx::core::runtime_reload_operator_summary(result);
  EXPECT_TRUE(summary.starts_with("========== ACTOR RELOAD FAILED"));
  EXPECT_NE(summary.find("generation 3 remains active"), std::string::npos);
  EXPECT_NE(summary.find("FULL PROCESS RESTART REQUIRED"), std::string::npos);
}

class RuntimeReloadControllerTest : public ::testing::Test {
protected:
  using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("obcx-runtime-reload-test-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root_);
    obcx::core::DbManager::reset_shared_managers_for_tests();
    work_guard_.emplace(asio::make_work_guard(io_));
    io_thread_ = std::thread([this] { io_.run(); });
  }

  void TearDown() override {
    if (controller_) {
      controller_->shutdown();
      controller_.reset();
    }
    work_guard_.reset();
    io_.stop();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
    database_.reset();
    obcx::core::DbManager::reset_shared_managers_for_tests();
    fs::remove_all(root_);
  }

  auto config_document(const std::string &generation, const std::string &mode,
                       const bool with_command = false) const -> std::string {
    auto document = std::string{};
    if (with_command) {
      document += "[bots.primary]\n"
                  "enabled = true\n"
                  "surface = \"onebot11.qq\"\n"
                  "transport = \"http\"\n"
                  "[bots.primary.connection]\n"
                  "host = \"localhost\"\n"
                  "port = 3000\n"
                  "access_token = \"\"\n"
                  "use_tls = false\n"
                  "connect_timeout_ms = 5000\n"
                  "action_timeout_ms = 30000\n"
                  "poll_interval_ms = 1000\n\n";
    }
    document += "[db.instances.main]\n"
                "type = \"sqlite\"\n"
                "path = \"" +
                (root_ / "runtime.sqlite3").string() +
                "\"\n\n"
                "[actor_runtime.scheduler]\n"
                "workers = 2\n"
                "blocking_workers = 1\n\n"
                "[actors.reload_lifecycle_actor]\n"
                "library = \"" OBCX_RELOAD_LIFECYCLE_ACTOR "\"\n"
                "enabled = true\n"
                "partition = \"conversation_id\"\n"
                "db = \"main\"\n\n"
                "[actors.reload_lifecycle_actor.config]\n"
                "generation = \"" +
                generation +
                "\"\n\n"
                "[pipelines.root]\n"
                "source = \"obcx::tests::events::ReloadProbe\"\n\n"
                "[[pipelines.root.stages]]\n"
                "name = \"probe\"\n"
                "actor = \"reload_lifecycle_actor\"\n"
                "input = \"obcx::tests::events::ReloadProbe\"\n"
                "output = \"obcx::tests::events::ReloadEmission\"\n"
                "mode = \"" +
                mode +
                "\"\n\n"
                "[pipelines.emission]\n"
                "source = \"obcx::tests::events::ReloadEmission\"\n\n"
                "[[pipelines.emission.stages]]\n"
                "name = \"sink\"\n"
                "actor = \"reload_lifecycle_actor\"\n"
                "input = \"obcx::tests::events::ReloadEmission\"\n"
                "mode = \"await\"\n";
    if (with_command) {
      document += "\n[command_runtime]\n"
                  "timeout_ms = 5000\n\n"
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
                  "actor = \"reload_lifecycle_actor\"\n"
                  "commands = [\"reload_probe\"]\n"
                  "platforms = [\"qq\"]\n"
                  "bots = [\"primary\"]\n"
                  "fallback = \"continue\"\n";
    }
    return document;
  }

  auto build_generation(
      const std::string &generation, const std::string &mode,
      const std::uint64_t id,
      const std::shared_ptr<obcx::core::RuntimeGeneration> &active = nullptr,
      const bool with_command = false)
      -> std::shared_ptr<obcx::core::RuntimeGeneration> {
    const auto config_path =
        root_ / (generation + "-" + std::to_string(id) + ".toml");
    write_file(config_path, config_document(generation, mode, with_command));
    auto parsed =
        obcx::core::RuntimeGenerationBuilder{obcx::test::bot_platform_catalog()}
            .parse_config(config_path.string());
    EXPECT_TRUE(parsed);
    if (!parsed) {
      return nullptr;
    }
    if (!database_) {
      database_ = obcx::core::DbManager::shared_manager(
          parsed.snapshot->get_db_instance_configs());
    }

    obcx::core::RuntimeGenerationBuildRequest request{
        .purpose =
            active ? obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate
                   : obcx::core::RuntimeGenerationBuildPurpose::Startup,
        .generation_id = id,
        .snapshot = parsed.snapshot,
        .actor_search_directories =
            {fs::path{OBCX_RELOAD_LIFECYCLE_ACTOR}.parent_path()},
        .staging_root = root_ / "staging",
        .configured_io_sources = 1,
        .db_manager = database_,
        .bot_operation_client = operation_client_};
    if (active) {
      request.active_process_owned_fingerprint =
          active->process_owned_fingerprint();
      request.active_process_owned_dependencies =
          active->process_owned_dependencies();
      request.blocking_executor = active->blocking_executor();
    }
    auto built = builder_.build(std::move(request));
    EXPECT_TRUE(built.ready())
        << (built.failure ? built.failure->code + ": " + built.failure->message
                          : "missing generation");
    if (built.generation) {
      EXPECT_EQ(built.generation->bot_operation_client(), operation_client_);
      EXPECT_EQ(built.generation->services()
                    ->get_service<obcx::bot::BotOperationGateway>(),
                operation_client_);
    }
    return std::move(built.generation);
  }

  auto dependency_config_document(const std::string &rebuilt_actor,
                                  const std::string &private_actor) const
      -> std::string {
    return "[bots.primary]\n"
           "enabled = true\n"
           "surface = \"onebot11.qq\"\n"
           "transport = \"http\"\n"
           "[bots.primary.connection]\n"
           "host = \"localhost\"\n"
           "port = 3000\n"
           "access_token = \"\"\n"
           "use_tls = false\n"
           "connect_timeout_ms = 5000\n"
           "action_timeout_ms = 30000\n"
           "poll_interval_ms = 1000\n\n"
           "[db.instances.main]\n"
           "type = \"sqlite\"\n"
           "path = \"" +
           (root_ / "private-runtime.sqlite3").string() +
           "\"\n\n"
           "[actor_runtime]\n"
           "workers = 2\n"
           "blocking_workers = 1\n\n"
           "[actors.test_actor_v2]\n"
           "library = \"" +
           rebuilt_actor +
           "\"\n"
           "enabled = true\n"
           "db = \"main\"\n\n"
           "[actors.test_actor_v2.config]\n"
           "label = \"reload\"\n"
           "target_installation = \"primary\"\n\n"
           "[actors.private_dependency_actor]\n"
           "library = \"" +
           private_actor +
           "\"\n"
           "enabled = true\n"
           "partition = \"conversation_id\"\n"
           "db = \"main\"\n\n"
           "[pipelines.private_dependency]\n"
           "source = \"obcx::tests::events::PrivateDependencyProbe\"\n\n"
           "[[pipelines.private_dependency.stages]]\n"
           "name = \"observe\"\n"
           "actor = \"private_dependency_actor\"\n"
           "input = \"obcx::tests::events::PrivateDependencyProbe\"\n"
           "output = \"PrivateDependencyObserved\"\n"
           "mode = \"await\"\n";
  }

  auto build_dependency_generation(
      const std::string &rebuilt_actor, const std::string &private_actor,
      const std::uint64_t id,
      const std::shared_ptr<obcx::core::RuntimeGeneration> &active = nullptr)
      -> std::shared_ptr<obcx::core::RuntimeGeneration> {
    const auto config_path =
        root_ / ("private-generation-" + std::to_string(id) + ".toml");
    write_file(config_path,
               dependency_config_document(rebuilt_actor, private_actor));
    auto parsed =
        obcx::core::RuntimeGenerationBuilder{obcx::test::bot_platform_catalog()}
            .parse_config(config_path.string());
    EXPECT_TRUE(parsed);
    if (!parsed) {
      return nullptr;
    }
    if (!database_) {
      database_ = obcx::core::DbManager::shared_manager(
          parsed.snapshot->get_db_instance_configs());
    }

    obcx::core::RuntimeGenerationBuildRequest request{
        .purpose =
            active ? obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate
                   : obcx::core::RuntimeGenerationBuildPurpose::Startup,
        .generation_id = id,
        .snapshot = parsed.snapshot,
        .actor_search_directories = {fs::path{rebuilt_actor}.parent_path(),
                                     fs::path{private_actor}.parent_path()},
        .staging_root = root_ / "staging",
        .configured_io_sources = 1,
        .db_manager = database_,
        .bot_operation_client = operation_client_};
    if (active) {
      request.active_process_owned_fingerprint =
          active->process_owned_fingerprint();
      request.active_process_owned_dependencies =
          active->process_owned_dependencies();
      request.blocking_executor = active->blocking_executor();
    }
    auto built = builder_.build(std::move(request));
    EXPECT_TRUE(built.ready())
        << (built.failure ? built.failure->code + ": " + built.failure->message
                          : "missing dependency generation");
    return std::move(built.generation);
  }

  auto message(std::string id, std::string conversation,
               const fs::path &gate = {}, const fs::path &completion = {},
               const fs::path &sink = {}) const -> obcx::core::MessageEnvelope {
    obcx::core::MessageEnvelope envelope;
    envelope.id = std::move(id);
    envelope.type = "obcx::tests::events::ReloadProbe";
    envelope.conversation_id = std::move(conversation);
    envelope.payload = {{"gate_path", gate.string()},
                        {"completion_path", completion.string()},
                        {"sink_path", sink.string()}};
    return envelope;
  }

  auto private_dependency_message(std::string id,
                                  const fs::path &gate = {}) const
      -> obcx::core::MessageEnvelope {
    obcx::core::MessageEnvelope envelope;
    envelope.id = std::move(id);
    envelope.type = "obcx::tests::events::PrivateDependencyProbe";
    envelope.conversation_id = "private-dependency";
    envelope.payload = {{"gate_path", gate.string()}};
    return envelope;
  }

  auto command_message(std::string id, const fs::path &gate = {},
                       const fs::path &completion = {},
                       const fs::path &sink = {}) const
      -> obcx::core::MessageEnvelope {
    obcx::core::MessageEnvelope envelope;
    envelope.id = std::move(id);
    envelope.type = "obcx::core::events::RawMessageEvent";
    envelope.source_platform = "qq";
    envelope.source_bot = "primary";
    envelope.conversation_id = "group:42";
    envelope.payload = {{"source_bot_configured", true},
                        {"sender", "7"},
                        {"group_id", "42"},
                        {"chat_id", ""},
                        {"message_type", "group"},
                        {"gate_path", gate.string()},
                        {"completion_path", completion.string()},
                        {"sink_path", sink.string()}};
    envelope.raw = {{"raw_message", "/reload_alias consume"}};
    return envelope;
  }

  auto process(obcx::core::MessageEnvelope envelope)
      -> std::future<obcx::core::OrchestratorResult> {
    return asio::co_spawn(io_, controller_->process(envelope),
                          asio::use_future);
  }

  auto reload(std::shared_ptr<obcx::core::RuntimeGeneration> candidate,
              const std::chrono::milliseconds timeout)
      -> std::future<obcx::core::RuntimeReloadResult> {
    return asio::co_spawn(io_,
                          controller_->reload_to(std::move(candidate), timeout),
                          asio::use_future);
  }

  fs::path root_;
  asio::io_context io_;
  std::optional<WorkGuard> work_guard_;
  std::thread io_thread_;
  obcx::core::RuntimeGenerationBuilder builder_{
      obcx::test::bot_platform_catalog()};
  std::shared_ptr<obcx::core::DbManager> database_;
  std::shared_ptr<obcx::bot::BotOperationGateway> operation_client_ =
      std::make_shared<obcx::core::BotOperationDispatcher>(
          [](const obcx::bot::SurfaceId &) { return false; });
  std::shared_ptr<obcx::core::ActorRuntimeReloadController> controller_;
};

struct BackgroundProbe {
  std::atomic_int starts{0};
  std::atomic_int stops{0};
  std::atomic<obcx::core::ActorGenerationLifecycle::TokenPtr> token;
};

auto observe_background(
    const std::shared_ptr<obcx::core::RuntimeGeneration> &generation)
    -> std::shared_ptr<BackgroundProbe> {
  auto probe = std::make_shared<BackgroundProbe>();
  auto lifecycle = generation->services()
                       ->get_service<obcx::core::ActorGenerationLifecycle>();
  lifecycle->subscribe(
      [probe](obcx::core::ActorGenerationLifecycle::TokenPtr token) noexcept {
        probe->token.store(std::move(token));
        ++probe->starts;
      },
      [probe]() noexcept { ++probe->stops; });
  return probe;
}

TEST_F(RuntimeReloadControllerTest,
       ActorOwnedWorkIsCancelledAndDrainedBeforeCandidateStarts) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("new", "await", 2, old);
  ASSERT_TRUE(candidate);
  auto old_probe = observe_background(old);
  auto new_probe = observe_background(candidate);
  EXPECT_EQ(old_probe->starts.load(), 0);
  EXPECT_EQ(new_probe->starts.load(), 0);
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);
  controller_->activate_command_catalogs();
  EXPECT_EQ(old_probe->starts.load(), 1);
  auto lifecycle =
      old->services()->get_service<obcx::core::ActorGenerationLifecycle>();
  const auto token = old_probe->token.load();
  auto work = lifecycle->acquire_work(token);
  ASSERT_TRUE(work);
  EXPECT_EQ(old->in_flight_routes(), 1U);

  auto cutover = reload(candidate, 2s);
  EXPECT_TRUE(wait_until([&] { return old_probe->stops.load() == 1; }));
  EXPECT_FALSE(token->valid());
  EXPECT_FALSE(lifecycle->acquire_work(token));
  EXPECT_EQ(new_probe->starts.load(), 0);
  EXPECT_EQ(cutover.wait_for(20ms), std::future_status::timeout);
  work.reset(); // Cancellation completion has now retired.
  EXPECT_TRUE(cutover.get().succeeded());
  EXPECT_EQ(new_probe->starts.load(), 1);
  controller_->begin_shutdown();
  EXPECT_FALSE(new_probe->token.load()->valid());
  EXPECT_EQ(new_probe->stops.load(), 1);
}

TEST_F(RuntimeReloadControllerTest,
       BackgroundDrainTimeoutUsesFreshTokenWithoutStartingCandidate) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("new", "await", 2, old);
  ASSERT_TRUE(candidate);
  auto old_probe = observe_background(old);
  auto new_probe = observe_background(candidate);
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);
  controller_->activate_command_catalogs();
  auto lifecycle =
      old->services()->get_service<obcx::core::ActorGenerationLifecycle>();
  const auto old_token = old_probe->token.load();
  auto work = lifecycle->acquire_work(old_token);
  ASSERT_TRUE(work);
  auto result = reload(candidate, 40ms).get();
  ASSERT_TRUE(result.failure);
  EXPECT_EQ(result.failure->code, "reload_drain_timeout");
  EXPECT_EQ(old_probe->starts.load(), 2);
  EXPECT_FALSE(old_token->valid());
  EXPECT_FALSE(lifecycle->acquire_work(old_token));
  EXPECT_NE(old_probe->token.load(), old_token);
  EXPECT_TRUE(old_probe->token.load()->valid());
  EXPECT_EQ(new_probe->starts.load(), 0);
  EXPECT_EQ(new_probe->stops.load(), 0);
  work.reset();
}

TEST_F(RuntimeReloadControllerTest,
       DiscardedBackgroundCandidateNeverStartsAndRetirementIsTerminal) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("new", "await", 2, old);
  ASSERT_TRUE(candidate);
  auto probe = observe_background(candidate);
  auto lifecycle = candidate->services()
                       ->get_service<obcx::core::ActorGenerationLifecycle>();
  candidate->shutdown();
  lifecycle->activate();
  EXPECT_EQ(probe->starts.load(), 0);
  EXPECT_EQ(probe->stops.load(), 0);
  EXPECT_THROW(
      lifecycle->subscribe(
          [](obcx::core::ActorGenerationLifecycle::TokenPtr) noexcept {},
          []() noexcept {}),
      std::logic_error);
}

TEST_F(RuntimeReloadControllerTest,
       BeforeWaitingAndAfterBoundarySelectExactlyOneGeneration) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("new", "await", 2, old);
  ASSERT_TRUE(candidate);
  std::weak_ptr old_lifetime = old;
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);
  old.reset();

  const auto gate = root_ / "release-old";
  auto before = process(message("before", "old-route", gate));
  ASSERT_TRUE(wait_until([&] {
    auto active = controller_->active_generation();
    return active && active->in_flight_routes() == 1 &&
           active->scheduler()->metrics().suspended_mailboxes == 1;
  }));

  auto cutover = reload(std::move(candidate), 2s);
  ASSERT_TRUE(wait_until([&] { return !controller_->gate_open(); }));
  auto waiting = process(message("waiting", "waiting-route"));
  EXPECT_EQ(waiting.wait_for(30ms), std::future_status::timeout);

  write_file(gate);
  EXPECT_EQ(observed_generation(before.get()), "old");
  const auto cutover_result = cutover.get();
  ASSERT_TRUE(cutover_result.succeeded());
  EXPECT_EQ(cutover_result.previous_generation_id, 1);
  EXPECT_EQ(cutover_result.active_generation_id, 2);
  EXPECT_EQ(observed_generation(waiting.get()), "new");
  EXPECT_EQ(observed_generation(process(message("after", "after-route")).get()),
            "new");
  EXPECT_TRUE(wait_until([&] { return old_lifetime.expired(); }));
}

TEST_F(RuntimeReloadControllerTest,
       DrainTimeoutReopensOnOldAndDestroysOnlyCandidate) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("candidate", "await", 2, old);
  ASSERT_TRUE(candidate);
  std::weak_ptr candidate_lifetime = candidate;
  const auto candidate_staging = candidate->staging_root();
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);

  const auto gate = root_ / "timeout-release";
  auto admitted = process(message("held", "held-route", gate));
  ASSERT_TRUE(wait_until([&] { return old->in_flight_routes() == 1; }));

  auto cutover = reload(std::move(candidate), 40ms);
  ASSERT_TRUE(wait_until([&] { return !controller_->gate_open(); }));
  auto waiting = process(message("waiting", "independent-route"));
  const auto result = cutover.get();
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "reload_drain_timeout");
  EXPECT_GE(result.drain_ms, 20);
  EXPECT_TRUE(controller_->gate_open());
  ASSERT_TRUE(controller_->active_generation());
  EXPECT_EQ(controller_->active_generation()->id(), 1);
  EXPECT_EQ(observed_generation(waiting.get()), "old");
  EXPECT_TRUE(wait_until([&] { return candidate_lifetime.expired(); }));
  EXPECT_FALSE(fs::exists(candidate_staging));
  const auto timeout_metrics = controller_->metrics();
  EXPECT_EQ(timeout_metrics.requests, 1);
  EXPECT_EQ(timeout_metrics.accepted, 1);
  EXPECT_EQ(timeout_metrics.failed, 1);
  EXPECT_EQ(timeout_metrics.drain_timeouts, 1);

  write_file(gate);
  EXPECT_EQ(observed_generation(admitted.get()), "old");
}

TEST_F(RuntimeReloadControllerTest,
       CommandTransactionSurvivesRejectedThenSuccessfulReload) {
  auto old = build_generation("old", "await", 1, nullptr, true);
  ASSERT_TRUE(old);
  auto rejected_candidate = build_generation("rejected", "await", 2, old, true);
  ASSERT_TRUE(rejected_candidate);
  auto accepted_candidate = build_generation("new", "await", 3, old, true);
  ASSERT_TRUE(accepted_candidate);
  std::weak_ptr rejected_lifetime = rejected_candidate;
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);

  const auto gate = root_ / "command-release";
  const auto completion = root_ / "command-completion";
  const auto sink = root_ / "command-sink";
  auto command = process(command_message("command", gate, completion, sink));
  ASSERT_TRUE(wait_until([&] {
    return old->in_flight_routes() == 1 &&
           old->scheduler()->metrics().suspended_mailboxes == 1;
  }));

  const auto rejected = reload(std::move(rejected_candidate), 40ms).get();
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "reload_drain_timeout");
  EXPECT_EQ(controller_->active_generation()->id(), 1U);
  EXPECT_TRUE(wait_until([&] { return rejected_lifetime.expired(); }));
  EXPECT_EQ(command.wait_for(20ms), std::future_status::timeout);

  auto accepted = reload(std::move(accepted_candidate), 2s);
  ASSERT_TRUE(wait_until([&] { return !controller_->gate_open(); }));
  write_file(gate);
  const auto command_result = command.get();
  ASSERT_TRUE(accepted.get().succeeded());
  ASSERT_TRUE(fs::exists(completion));
  ASSERT_TRUE(fs::exists(sink));
  EXPECT_EQ(read_file(completion), "old");
  EXPECT_EQ(read_file(sink), "old");
  EXPECT_EQ(observed_generation(command_result), "old");
  EXPECT_EQ(controller_->active_generation()->id(), 3U);
}

TEST_F(RuntimeReloadControllerTest,
       TerminalEmissionAndSuspendedActorRetainOldGenerationUntilCompletion) {
  auto old = build_generation("old", "async", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("new", "await", 2, old);
  ASSERT_TRUE(candidate);
  std::weak_ptr old_lifetime = old;
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);
  old.reset();

  const auto gate = root_ / "terminal-release";
  const auto completion = root_ / "terminal-complete";
  const auto sink = root_ / "terminal-emission-sink";
  auto terminal_root =
      process(message("terminal", "terminal-route", gate, completion, sink));
  EXPECT_TRUE(terminal_root.get().ok());
  ASSERT_TRUE(wait_until([&] {
    auto active = controller_->active_generation();
    return active && active->in_flight_routes() == 1 &&
           active->orchestrator()->pending_terminal_tasks() == 1 &&
           active->scheduler()->metrics().suspended_mailboxes == 1;
  }));

  auto cutover = reload(std::move(candidate), 2s);
  ASSERT_TRUE(wait_until([&] { return !controller_->gate_open(); }));
  EXPECT_FALSE(old_lifetime.expired());
  write_file(gate);

  ASSERT_TRUE(cutover.get().succeeded());
  ASSERT_TRUE(fs::exists(completion));
  ASSERT_TRUE(fs::exists(sink));
  EXPECT_EQ(read_file(completion), "old");
  EXPECT_EQ(read_file(sink), "old");
  EXPECT_TRUE(wait_until([&] { return old_lifetime.expired(); }));
}

TEST_F(RuntimeReloadControllerTest,
       ConcurrentReloadIsBusyAndShutdownReleasesWaitingIngress) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto candidate = build_generation("new", "await", 2, old);
  ASSERT_TRUE(candidate);
  auto duplicate_candidate = build_generation("duplicate", "await", 3, old);
  ASSERT_TRUE(duplicate_candidate);
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);

  const auto gate = root_ / "shutdown-release";
  auto admitted = process(message("held", "held-route", gate));
  ASSERT_TRUE(wait_until([&] { return old->in_flight_routes() == 1; }));
  auto first_reload = reload(std::move(candidate), 2s);
  ASSERT_TRUE(wait_until([&] { return !controller_->gate_open(); }));

  auto duplicate_reload = reload(std::move(duplicate_candidate), 2s);
  const auto busy = duplicate_reload.get();
  ASSERT_TRUE(busy.failure.has_value());
  EXPECT_EQ(busy.status, obcx::core::RuntimeReloadStatus::Busy);
  EXPECT_EQ(busy.failure->code, "reload_busy");

  auto waiting = process(message("waiting", "waiting-route"));
  EXPECT_EQ(waiting.wait_for(30ms), std::future_status::timeout);
  auto shutdown = std::async(std::launch::async, [controller = controller_] {
    controller->shutdown();
  });
  ASSERT_TRUE(
      wait_until([&] { return controller_->active_generation() == nullptr; }));
  write_file(gate);
  ASSERT_EQ(shutdown.wait_for(2s), std::future_status::ready);
  shutdown.get();

  const auto waiting_result = waiting.get();
  ASSERT_FALSE(waiting_result.failures.empty());
  EXPECT_EQ(waiting_result.failures.front().failure.code, "reload_shutdown");
  const auto reload_result = first_reload.get();
  ASSERT_TRUE(reload_result.failure.has_value());
  EXPECT_EQ(reload_result.status, obcx::core::RuntimeReloadStatus::Shutdown);
  EXPECT_EQ(reload_result.failure->code, "reload_shutdown");
  (void)admitted.get();
}

TEST_F(RuntimeReloadControllerTest,
       RepeatedSuccessfulReloadsRetireEachOldImage) {
  auto generation1 = build_generation("one", "await", 1);
  ASSERT_TRUE(generation1);
  auto generation2 = build_generation("two", "await", 2, generation1);
  ASSERT_TRUE(generation2);
  auto generation3 = build_generation("three", "await", 3, generation1);
  ASSERT_TRUE(generation3);
  std::weak_ptr lifetime1 = generation1;
  std::weak_ptr lifetime2 = generation2;
  controller_ =
      std::make_shared<obcx::core::ActorRuntimeReloadController>(generation1);
  generation1.reset();

  ASSERT_TRUE(reload(std::move(generation2), 1s).get().succeeded());
  EXPECT_TRUE(wait_until([&] { return lifetime1.expired(); }));
  ASSERT_TRUE(reload(std::move(generation3), 1s).get().succeeded());
  EXPECT_TRUE(wait_until([&] { return lifetime2.expired(); }));
  EXPECT_EQ(
      observed_generation(process(message("latest", "latest-route")).get()),
      "three");
}

TEST_F(RuntimeReloadControllerTest,
       RebuiltActorAndSameSonameDependencyRemainGenerationSafe) {
  ASSERT_NE(read_file(OBCX_REBUILT_ACTOR_V1), read_file(OBCX_REBUILT_ACTOR_V2));

  auto old = build_dependency_generation(OBCX_REBUILT_ACTOR_V1,
                                         OBCX_PRIVATE_ACTOR_V1, 1);
  ASSERT_TRUE(old);
  ASSERT_TRUE(old->actor_manager()->is_actor_loaded("test_actor_v2"));
  std::weak_ptr old_lifetime = old;
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);
  old.reset();

  const auto gate = root_ / "release-private-dependency";
  auto old_work = process(private_dependency_message("private-old", gate));
  ASSERT_TRUE(wait_until([&] {
    const auto active = controller_->active_generation();
    return active && active->in_flight_routes() == 1 &&
           active->scheduler()->metrics().suspended_mailboxes == 1;
  }));

  auto active = controller_->active_generation();
  ASSERT_TRUE(active);
  auto candidate = build_dependency_generation(
      OBCX_REBUILT_ACTOR_V2, OBCX_PRIVATE_ACTOR_V2, 2, active);
  ASSERT_TRUE(candidate);
  ASSERT_TRUE(candidate->actor_manager()->is_actor_loaded("test_actor_v2"));
  ASSERT_TRUE(
      candidate->actor_manager()->is_actor_loaded("private_dependency_actor"));

  auto cutover = reload(std::move(candidate), 2s);
  ASSERT_TRUE(wait_until([&] { return !controller_->gate_open(); }));
  EXPECT_FALSE(old_lifetime.expired());
  write_file(gate);

  EXPECT_EQ(observed_private_dependency(old_work.get()), 1);
  ASSERT_TRUE(cutover.get().succeeded());
  EXPECT_EQ(observed_private_dependency(
                process(private_dependency_message("private-new")).get()),
            2);
  active.reset();
  EXPECT_TRUE(wait_until([&] { return old_lifetime.expired(); }));
}

TEST_F(RuntimeReloadControllerTest,
       AsynchronousStartIsImmediatelyBusyAndPublishesMetrics) {
  auto old = build_generation("old", "await", 1);
  ASSERT_TRUE(old);
  auto process_blocking_executor = old->blocking_executor();
  ASSERT_TRUE(process_blocking_executor);
  controller_ = std::make_shared<obcx::core::ActorRuntimeReloadController>(old);

  const auto candidate_config = root_ / "start-reload.toml";
  write_file(candidate_config, config_document("started", "await"));
  obcx::core::RuntimeGenerationBuildRequest request{
      .purpose = obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate,
      .generation_id = 2,
      .config_path = candidate_config.string(),
      .actor_search_directories =
          {fs::path{OBCX_RELOAD_LIFECYCLE_ACTOR}.parent_path()},
      .staging_root = root_ / "staging",
      .configured_io_sources = 1};

  auto held_route = old->admit_route();
  ASSERT_TRUE(held_route);
  std::promise<obcx::core::RuntimeReloadResult> completion;
  auto completed = completion.get_future();
  EXPECT_EQ(controller_->start_reload(request, 2s,
                                      [&completion](const auto &result) {
                                        completion.set_value(result);
                                      }),
            obcx::core::RuntimeReloadStartStatus::Accepted);
  ASSERT_TRUE(wait_until([&] {
    return controller_->reload_in_progress() && !controller_->gate_open();
  }));
  EXPECT_EQ(controller_->start_reload(request, 2s),
            obcx::core::RuntimeReloadStartStatus::Busy);

  held_route.reset();
  ASSERT_EQ(completed.wait_for(3s), std::future_status::ready);
  const auto result = completed.get();
  ASSERT_TRUE(result.succeeded());
  EXPECT_EQ(result.attempt_id, 1);
  EXPECT_EQ(result.previous_generation_id, 1);
  EXPECT_EQ(result.active_generation_id, 2);
  EXPECT_GE(result.total_ms, result.preparation_ms);
  EXPECT_GE(result.total_ms, result.drain_ms);
  EXPECT_TRUE(wait_until([&] { return !controller_->reload_in_progress(); }));

  const auto metrics = controller_->metrics();
  EXPECT_EQ(metrics.requests, 2);
  EXPECT_EQ(metrics.accepted, 1);
  EXPECT_EQ(metrics.succeeded, 1);
  EXPECT_EQ(metrics.failed, 0);
  EXPECT_EQ(metrics.busy, 1);
  EXPECT_EQ(metrics.drain_timeouts, 0);
  ASSERT_TRUE(controller_->active_generation()->config_snapshot());
  EXPECT_EQ(controller_->active_generation()->config_snapshot()->config_path(),
            candidate_config.string());
  EXPECT_EQ(
      observed_generation(process(message("started", "started-route")).get()),
      "started");
  ASSERT_TRUE(controller_->active_generation());
  EXPECT_EQ(controller_->active_generation()->blocking_executor(),
            process_blocking_executor);
}

TEST_F(RuntimeReloadControllerTest,
       BeginShutdownClosesIngressBeforeDestroyingTheActiveGeneration) {
  auto active = build_generation("active", "await", 1);
  ASSERT_TRUE(active);
  controller_ =
      std::make_shared<obcx::core::ActorRuntimeReloadController>(active);

  controller_->begin_shutdown();
  EXPECT_FALSE(controller_->gate_open());
  EXPECT_EQ(controller_->active_generation(), active);

  controller_->shutdown();
  EXPECT_EQ(controller_->active_generation(), nullptr);
}

TEST_F(RuntimeReloadControllerTest,
       ShutdownDrainsActorDestructorCallbacksBeforeDsoUnload) {
  auto active = build_generation("active", "await", 1);
  ASSERT_TRUE(active);
  controller_ =
      std::make_shared<obcx::core::ActorRuntimeReloadController>(active);

  const auto cancellation_marker = root_ / "background-cancelled";
  auto start_background = message("background", "background-route");
  start_background.payload["background_cancel_path"] =
      cancellation_marker.string();
  ASSERT_TRUE(process(std::move(start_background)).get().ok());
  EXPECT_FALSE(fs::exists(cancellation_marker));

  controller_->shutdown();
  ASSERT_TRUE(fs::exists(cancellation_marker));
  EXPECT_EQ(read_file(cancellation_marker), "cancelled");
}

TEST_F(RuntimeReloadControllerTest,
       ReloadParseFailureIncludesSafeStructuredDiagnostic) {
  auto active = build_generation("active", "await", 1);
  ASSERT_TRUE(active);
  controller_ =
      std::make_shared<obcx::core::ActorRuntimeReloadController>(active);

  const auto invalid_config = root_ / "invalid-reload.toml";
  write_file(invalid_config, R"(
[bots.telegram.connection]
access_token = "do-not-log-this-token"
broken = [
)");
  obcx::core::RuntimeGenerationBuildRequest request{
      .purpose = obcx::core::RuntimeGenerationBuildPurpose::ReloadCandidate,
      .generation_id = 2,
      .config_path = invalid_config.string(),
  };

  auto reloaded = asio::co_spawn(
      io_, controller_->reload(std::move(request), 1s), asio::use_future);
  const auto result = reloaded.get();

  ASSERT_FALSE(result.succeeded());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "reload_parse_failed");
  EXPECT_NE(result.failure->message.find("diagnostic_code=config_parse_failed"),
            std::string::npos);
  EXPECT_NE(result.failure->message.find("path=" + invalid_config.string()),
            std::string::npos);
  EXPECT_NE(result.failure->message.find("line="), std::string::npos);
  EXPECT_NE(result.failure->message.find("column="), std::string::npos);
  EXPECT_EQ(result.failure->message.find("do-not-log-this-token"),
            std::string::npos);
}

} // namespace
