#include "core/actor/actor_messages.hpp"
#include "core/actor/reflected_actor.hpp"
#include "core/bot/messaging.hpp"
#include "core/command/command_coordinator.hpp"
#include "support/bot_platform_fixture.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace obcx::tests::command_runtime {

struct TestCommand final : obcx::command::RequestMessage<TestCommand> {};

class CommandActor final : public obcx::core::ReflectedActor<CommandActor> {
public:
  static constexpr std::string_view actor_name = "command_actor";
  static constexpr std::string_view actor_version = "1.0.0";

  static constexpr auto command_contract() {
    return obcx::command::catalog(obcx::command::observe<TestCommand>(
        "test", "Run a command coordinator test"));
  }

  auto handle(const TestCommand &request,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorTask<obcx::core::ActorResult> {
    ++command_count;
    if (request.invocation.arguments == "timeout") {
      for (;;) {
        co_await context.yield();
      }
    }
    if (request.invocation.arguments == "failure") {
      co_return obcx::core::ActorResult::failed(
          "test_actor_failure", "test command actor failed", true);
    }

    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope business;
    business.id = message.id + ":business";
    business.type = "CommandBusiness";
    business.source_platform = message.source_platform;
    business.source_bot = message.source_bot;
    business.conversation_id = message.conversation_id;
    business.headers = message.headers;
    business.payload = {
        {"command", request.invocation.name},
        {"arguments", request.invocation.arguments},
    };
    result.emit(std::move(business));

    if (request.invocation.arguments == "missing") {
      co_return result;
    }
    const auto propagation = request.invocation.arguments == "continue"
                                 ? obcx::command::Propagation::Continue
                                 : obcx::command::Propagation::Consume;
    result.emit(
        obcx::command::CommandCompleted{
            .transaction_id = request.invocation.transaction_id,
            .propagation = propagation,
        },
        message);
    if (request.invocation.arguments == "duplicate") {
      result.emit(
          obcx::command::CommandCompleted{
              .transaction_id = request.invocation.transaction_id,
              .propagation = obcx::command::Propagation::Consume,
          },
          message);
    } else if (request.invocation.arguments == "wrong_generation") {
      result.emitted.back().headers.insert_or_assign(
          std::string{obcx::core::command_generation_header}, "999");
    } else if (request.invocation.arguments == "malformed") {
      result.emitted.back().payload = "invalid";
    }
    co_return result;
  }

  auto handle(const obcx::core::events::RawMessageEvent &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &) -> obcx::core::ActorResult {
    ++raw_count;
    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope observed;
    observed.id = message.id + ":raw";
    observed.type = "RawObserved";
    observed.headers = message.headers;
    result.emit(std::move(observed));
    return result;
  }

  std::atomic_int command_count = 0;
  std::atomic_int raw_count = 0;
};

} // namespace obcx::tests::command_runtime

namespace {

namespace asio = boost::asio;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

template <typename T>
auto run_awaitable(asio::io_context &ioc, asio::awaitable<T> awaitable) -> T {
  std::optional<T> result;
  std::exception_ptr exception;
  asio::co_spawn(
      ioc,
      [&]() -> asio::awaitable<void> {
        try {
          result = co_await std::move(awaitable);
        } catch (...) {
          exception = std::current_exception();
        }
      },
      asio::detached);
  ioc.run();
  ioc.restart();
  if (exception) {
    std::rethrow_exception(exception);
  }
  return std::move(*result);
}

auto command_contract(const bool accept_request = true,
                      std::optional<std::string> pattern = std::nullopt)
    -> obcx::core::ActorInputContract {
  const auto request_type = std::string{obcx::core::canonical_message_type_name<
      obcx::tests::command_runtime::TestCommand>()};
  auto contract = obcx::core::ActorInputContract{
      .schema_version = 2,
      .actor = "command_actor",
      .accepted_inputs =
          {
              std::string{obcx::core::canonical_message_type_name<
                  obcx::core::events::RawMessageEvent>()},
          },
      .accepted_input_set =
          {
              std::string{obcx::core::canonical_message_type_name<
                  obcx::core::events::RawMessageEvent>()},
          },
      .commands =
          {
              {.name = "test",
               .description = "Run a command coordinator test",
               .request_type = request_type},
          },
  };
  if (accept_request) {
    contract.accepted_inputs.push_back(request_type);
    contract.accepted_input_set.insert(request_type);
  }
  if (pattern) {
    contract.commands.front().matcher = obcx::core::ActorCommandMatcher{
        .kind = "re2",
        .pattern = std::move(*pattern),
        .mode = "full",
    };
  }
  return contract;
}

class RecordingGateway final : public obcx::bot::BotOperationGateway {
public:
  [[nodiscard]] auto supported_actions(
      const obcx::bot::BotInstallationRef &installation) const
      -> obcx::bot::BotOperationResult<obcx::bot::SupportedActions> override {
    return obcx::bot::BotOperationResult<obcx::bot::SupportedActions>::success(
        {.installation = installation, .actions = {}});
  }

  auto invoke(obcx::bot::OperationEnvelope envelope)
      -> asio::awaitable<obcx::bot::OperationReply> override {
    operations.push_back(std::move(envelope));
    if (on_invoke) {
      on_invoke(operations.size());
    }
    if (!replies.empty()) {
      auto reply = std::move(replies.front());
      replies.pop_front();
      co_return reply;
    }
    co_return obcx::bot::OperationReply::success(obcx::bot::Json::object());
  }

  std::vector<obcx::bot::OperationEnvelope> operations;
  std::deque<obcx::bot::OperationReply> replies;
  std::function<void(std::size_t)> on_invoke;
};

auto config_document(std::string fallback = "continue",
                     std::string platform = "qq", std::string bot_type = "qq",
                     std::string actor = "command_actor",
                     std::string command = "test") -> std::string {
  const auto telegram = bot_type == "telegram";
  const auto surface =
      std::string{telegram ? "telegram.bot_api" : "onebot11.qq"};
  const auto connection =
      std::string{telegram ? "host = \"api.telegram.org\"\n"
                             "port = 443\n"
                             "access_token = \"YOUR_TELEGRAM_TOKEN\"\n"
                             "bot_username = \"fixture_bot\"\n"
                             "use_tls = true\n"
                             "connect_timeout_ms = 5000\n"
                             "action_timeout_ms = 30000\n"
                             "poll_timeout_ms = 25000\n"
                             "poll_force_close_ms = 30000\n"
                             "poll_retry_interval_ms = 3000\n\n"
                           : "host = \"localhost\"\n"
                             "port = 3000\n"
                             "access_token = \"\"\n"
                             "use_tls = false\n"
                             "connect_timeout_ms = 5000\n"
                             "action_timeout_ms = 30000\n"
                             "poll_interval_ms = 1000\n\n"};
  return "[bots.primary]\n"
         "enabled = true\n"
         "surface = \"" +
         surface +
         "\"\n"
         "transport = \"http\"\n\n"
         "[bots.primary.connection]\n" +
         connection +
         "[actors.command_actor]\n"
         "enabled = true\n"
         "partition = \"conversation_id\"\n\n"
         "[command_runtime]\n"
         "timeout_ms = 100\n\n"
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
         "actor = \"" +
         actor +
         "\"\n"
         "commands = [\"" +
         command +
         "\"]\n"
         "platforms = [\"" +
         platform +
         "\"]\n"
         "bots = [\"primary\"]\n"
         "fallback = \"" +
         fallback + "\"\n";
}

void replace_policy(std::string &document, const std::string_view dimension,
                    const std::string_view mode,
                    const std::string_view entries) {
  const auto original = "[command_runtime.access." + std::string{dimension} +
                        "]\nmode = \"unrestricted\"\nentries = []";
  const auto replacement = "[command_runtime.access." + std::string{dimension} +
                           "]\nmode = \"" + std::string{mode} +
                           "\"\nentries = " + std::string{entries};
  const auto offset = document.find(original);
  ASSERT_NE(offset, std::string::npos);
  document.replace(offset, original.size(), replacement);
}

class CommandCoordinatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = fs::temp_directory_path() /
            ("obcx-command-coordinator-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root_);
  }

  void TearDown() override { fs::remove_all(root_); }

  auto snapshot(const std::string &name, const std::string &document)
      -> std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> {
    const auto path = root_ / name;
    {
      std::ofstream output(path);
      output << document;
    }
    const auto built = obcx::common::ConfigLoader::build_snapshot(
        path, obcx::test::bot_platform_catalog());
    EXPECT_TRUE(built);
    return built.snapshot;
  }

  auto table(
      const std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> &config,
      const bool accept_request = true)
      -> obcx::core::CommandRoutingBuildResult {
    return obcx::core::build_command_routing_table(
        *config, {{"command_actor", command_contract(accept_request)}});
  }

  auto raw(std::string arguments) -> obcx::core::MessageEnvelope {
    obcx::core::MessageEnvelope message;
    message.id = "raw-" + arguments;
    message.type = obcx::core::canonical_message_type_name<
        obcx::core::events::RawMessageEvent>();
    message.source_platform = "qq";
    message.source_bot = "primary";
    message.conversation_id = "group:42";
    message.payload = {{"source_bot_configured", true},
                       {"sender", "7"},
                       {"group_id", "42"},
                       {"chat_id", ""},
                       {"message_type", "group"}};
    message.raw = {{"raw_message", "/test " + arguments}};
    return message;
  }

  auto raw_command(std::string command, std::string arguments)
      -> obcx::core::MessageEnvelope {
    auto message = raw(std::move(arguments));
    message.raw["raw_message"] = "/" + std::move(command) + " " +
                                 message.id.substr(std::string{"raw-"}.size());
    return message;
  }

  struct Runtime {
    std::shared_ptr<obcx::core::ActorServices> services;
    std::shared_ptr<obcx::core::NativeActorScheduler> scheduler;
    std::shared_ptr<obcx::core::Orchestrator> orchestrator;
    std::shared_ptr<obcx::tests::command_runtime::CommandActor> actor;
    std::shared_ptr<RecordingGateway> gateway;
    std::shared_ptr<obcx::core::CommandCoordinator> coordinator;

    ~Runtime() {
      coordinator->shutdown();
      orchestrator->shutdown();
    }
  };

  auto runtime(
      const std::shared_ptr<const obcx::common::RuntimeConfigSnapshot> &config,
      std::shared_ptr<const obcx::core::CommandRoutingTable> routing_table = {})
      -> Runtime {
    if (!routing_table) {
      auto built_table = table(config);
      EXPECT_TRUE(built_table);
      routing_table = std::move(built_table.table);
    }
    auto services = std::make_shared<obcx::core::ActorServices>();
    auto scheduler = std::make_shared<obcx::core::NativeActorScheduler>(
        obcx::core::NativeActorSchedulerOptions{.worker_count = 2}, services);
    auto orchestrator =
        std::make_shared<obcx::core::Orchestrator>(scheduler, services);
    auto actor = std::make_shared<obcx::tests::command_runtime::CommandActor>();
    orchestrator->register_actor(actor);
    orchestrator->configure_actors(config->get_actor_configs());
    orchestrator->configure_pipelines(
        {{.name = "raw",
          .source = std::string{obcx::core::canonical_message_type_name<
              obcx::core::events::RawMessageEvent>()},
          .stages = {
              {.name = "observe",
               .actor = "command_actor",
               .input = std::string{obcx::core::canonical_message_type_name<
                   obcx::core::events::RawMessageEvent>()},
               .outputs = {"RawObserved"},
               .mode = "await"}}}});
    auto gateway = std::make_shared<RecordingGateway>();
    auto coordinator = std::make_shared<obcx::core::CommandCoordinator>(
        7, std::move(routing_table), scheduler, orchestrator, gateway);
    return Runtime{
        .services = std::move(services),
        .scheduler = std::move(scheduler),
        .orchestrator = std::move(orchestrator),
        .actor = std::move(actor),
        .gateway = std::move(gateway),
        .coordinator = std::move(coordinator),
    };
  }

  fs::path root_;
};

TEST_F(CommandCoordinatorTest, BuildsImmutableRoutesAndDetectionOnlyCatalogs) {
  const auto config = snapshot("valid.toml", config_document());
  const auto built = table(config);
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  ASSERT_EQ(built.table->routes().size(), 1U);
  ASSERT_EQ(built.table->bots().size(), 1U);
  const auto &route = built.table->routes().begin()->second;
  EXPECT_EQ(route.actor, "command_actor");
  EXPECT_EQ(route.request_type,
            obcx::core::canonical_message_type_name<
                obcx::tests::command_runtime::TestCommand>());
  EXPECT_EQ(route.timeout, 100ms);
  const auto &bot = built.table->bots().begin()->second;
  ASSERT_EQ(bot.catalog.size(), 2U);
  EXPECT_EQ(bot.catalog[0].name, "help");
  EXPECT_EQ(bot.catalog[1].name, "test");
  EXPECT_FALSE(bot.adapter->supports_catalog_publication());
}

TEST_F(CommandCoordinatorTest,
       UsesConfiguredTelegramUsernameForExplicitCommandTargets) {
  auto document = config_document("continue", "telegram", "telegram");
  const auto username = document.find("bot_username = \"fixture_bot\"");
  ASSERT_NE(username, std::string::npos);
  document.replace(username,
                   std::string{"bot_username = \"fixture_bot\""}.size(),
                   "bot_username = \"my_bot\"");
  const auto config = snapshot("telegram-target.toml", document);
  const auto built = table(config);
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  ASSERT_EQ(built.table->bots().size(), 1U);
  const auto &bot = built.table->bots().begin()->second;
  EXPECT_EQ(bot.target, "my_bot");

  obcx::core::MessageEnvelope event;
  event.type = obcx::core::canonical_message_type_name<
      obcx::core::events::RawMessageEvent>();
  event.source_platform = "telegram";
  event.source_bot = "primary";
  event.raw = {
      {"text", "/test@my_bot"},
      {"entities",
       obcx::common::json::array(
           {{{"type", "bot_command"}, {"offset", 0}, {"length", 12}}})},
  };
  const auto detected = bot.adapter->detect(event);
  ASSERT_TRUE(detected.has_value());
  EXPECT_EQ(detected->name, "test");
  event.raw["text"] = "/test@other_bot";
  event.raw["entities"][0]["length"] = 15;
  EXPECT_FALSE(bot.adapter->detect(event).has_value());
}

TEST_F(CommandCoordinatorTest, AggregatesCommandsFromMultipleActorsPerBot) {
  auto document = config_document();
  document += "\n[actors.other_actor]\n"
              "enabled = true\n\n"
              "[[command_runtime.routes]]\n"
              "actor = \"other_actor\"\n"
              "commands = [\"other\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("aggregate.toml", document);
  auto other = command_contract();
  other.actor = "other_actor";
  other.commands.front().name = "other";
  other.commands.front().description = "Run another actor command";
  const auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract()}, {"other_actor", other}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  ASSERT_EQ(built.table->routes().size(), 2U);
  ASSERT_EQ(built.table->bots().size(), 1U);
  const auto &catalog = built.table->bots().begin()->second.catalog;
  ASSERT_EQ(catalog.size(), 3U);
  EXPECT_EQ(catalog[0].name, "help");
  EXPECT_EQ(catalog[1].name, "other");
  EXPECT_EQ(catalog[2].name, "test");
}

TEST_F(CommandCoordinatorTest, RejectsInvalidActorCommandBotAndAdapterEdges) {
  const auto missing_actor = snapshot(
      "missing-actor.toml", config_document("continue", "qq", "qq", "missing"));
  auto built = table(missing_actor);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_actor_unavailable");

  const auto missing_command = snapshot(
      "missing-command.toml",
      config_document("continue", "qq", "qq", "command_actor", "missing"));
  built = table(missing_command);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_not_declared");

  const auto unsupported = snapshot("unsupported.toml", config_document());
  built = table(unsupported, false);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_request_unsupported");

  const auto mismatched = snapshot(
      "mismatched.toml", config_document("continue", "telegram", "qq"));
  built = table(mismatched);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_bot_platform_mismatch");

  const auto unavailable = snapshot(
      "unavailable.toml", config_document("continue", "matrix", "matrix"));
  built = table(unavailable);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_platform_adapter_unavailable");
}

TEST_F(CommandCoordinatorTest, RejectsScopedRouteConflictsBeforeActivation) {
  auto document = config_document();
  document += "\n[[command_runtime.routes]]\n"
              "actor = \"command_actor\"\n"
              "commands = [\"test\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("conflict.toml", document);
  const auto built = table(config);
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_route_conflict");
}

TEST_F(CommandCoordinatorTest,
       RoutesOneFullMatchWithCanonicalIdentityAndUnchangedArguments) {
  const auto config = snapshot("pattern.toml", config_document());
  const auto built = obcx::core::build_command_routing_table(
      *config,
      {{"command_actor", command_contract(true, R"(^(?:test|alias)$)")}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  const auto &bot = built.table->bots().begin()->second;
  ASSERT_EQ(bot.patterns.size(), 1U);
  EXPECT_EQ(bot.catalog.size(), 2U);
  EXPECT_EQ(bot.catalog[0].name, "help");
  EXPECT_EQ(bot.catalog[1].name, "test");

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("alias", "continue"),
                                       std::make_shared<int>(8)));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 1);
  const auto business =
      std::ranges::find(result.emitted, std::string{"CommandBusiness"},
                        &obcx::core::MessageEnvelope::type);
  ASSERT_NE(business, result.emitted.end());
  EXPECT_EQ(business->payload.at("command"), "test");
  EXPECT_EQ(business->payload.at("arguments"), "continue");
  const auto observed =
      std::ranges::find(result.emitted, std::string{"RawObserved"},
                        &obcx::core::MessageEnvelope::type);
  ASSERT_NE(observed, result.emitted.end());
  EXPECT_EQ(observed->headers.at(std::string{obcx::core::command_name_header}),
            "test");

  const auto unmatched = run_awaitable(
      ioc, active.coordinator->process(raw_command("prefixalias", "consume"),
                                       std::make_shared<int>(9)));
  EXPECT_TRUE(unmatched.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 2);
}

TEST_F(CommandCoordinatorTest,
       ExactCanonicalRouteWinsWithoutEvaluatingOverlappingPatterns) {
  auto document = config_document("consume");
  document += "\n[actors.other_actor]\n"
              "enabled = true\n\n"
              "[[command_runtime.routes]]\n"
              "actor = \"other_actor\"\n"
              "commands = [\"other\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("exact-precedence.toml", document);
  auto other = command_contract(true, R"(^test$)");
  other.actor = "other_actor";
  other.commands.front().name = "other";
  const auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")},
                {"other_actor", std::move(other)}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("test", "consume"),
                                       std::make_shared<int>(10)));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 0);
}

TEST_F(CommandCoordinatorTest,
       RejectsIdenticalPatternsAndContinuesAmbiguousMessagesExactlyOnce) {
  auto document = config_document("consume");
  document += "\n[actors.other_actor]\n"
              "enabled = true\n\n"
              "[[command_runtime.routes]]\n"
              "actor = \"other_actor\"\n"
              "commands = [\"other\"]\n"
              "platforms = [\"qq\"]\n"
              "bots = [\"primary\"]\n"
              "fallback = \"consume\"\n";
  const auto config = snapshot("pattern-conflicts.toml", document);
  auto other = command_contract(true, R"(^alias$)");
  other.actor = "other_actor";
  other.commands.front().name = "other";

  auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")},
                {"other_actor", other}});
  ASSERT_TRUE(built.failure);
  EXPECT_EQ(built.failure->code, "command_pattern_conflict");

  other.commands.front().matcher->pattern = R"(^(?:alias|other_alias)$)";
  built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")},
                {"other_actor", std::move(other)}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("alias", "consume"),
                                       std::make_shared<int>(11)));
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_match_ambiguous");
  EXPECT_EQ(active.actor->command_count, 0);
  EXPECT_EQ(active.actor->raw_count, 1);
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type, "RawObserved");
  EXPECT_FALSE(result.emitted.front().headers.contains(
      std::string{obcx::core::command_processed_header}));
}

TEST_F(CommandCoordinatorTest,
       RoutesTypedCompletionAndAppliesContinueOrConsumeOnce) {
  const auto config = snapshot("routing.toml", config_document());
  auto active = runtime(config);
  asio::io_context ioc;

  auto continued =
      run_awaitable(ioc, active.coordinator->process(raw("continue"),
                                                     std::make_shared<int>(1)));
  EXPECT_TRUE(continued.ok());
  EXPECT_EQ(active.actor->command_count, 1);
  EXPECT_EQ(active.actor->raw_count, 1);
  ASSERT_EQ(continued.emitted.size(), 2U);
  const auto observed =
      std::ranges::find(continued.emitted, std::string{"RawObserved"},
                        &obcx::core::MessageEnvelope::type);
  ASSERT_NE(observed, continued.emitted.end());
  EXPECT_EQ(
      observed->headers.at(std::string{obcx::core::command_processed_header}),
      "true");
  EXPECT_EQ(
      observed->headers.at(std::string{obcx::core::command_outcome_header}),
      "continue");
  EXPECT_EQ(
      observed->headers.at(std::string{obcx::core::command_generation_header}),
      "7");

  auto consumed =
      run_awaitable(ioc, active.coordinator->process(raw("consume"),
                                                     std::make_shared<int>(2)));
  EXPECT_TRUE(consumed.ok());
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 1);
  ASSERT_EQ(consumed.emitted.size(), 1U);
  EXPECT_EQ(consumed.emitted.front().type, "CommandBusiness");

  auto unmatched = raw("consume");
  unmatched.raw["raw_message"] = "/unknown consume";
  auto passed =
      run_awaitable(ioc, active.coordinator->process(std::move(unmatched),
                                                     std::make_shared<int>(3)));
  EXPECT_TRUE(passed.ok());
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 2);

  auto processed = raw("consume");
  processed.headers.emplace(std::string{obcx::core::command_processed_header},
                            "true");
  passed =
      run_awaitable(ioc, active.coordinator->process(std::move(processed),
                                                     std::make_shared<int>(4)));
  EXPECT_TRUE(passed.ok());
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 3);
}

TEST_F(CommandCoordinatorTest,
       RejectsMissingDuplicateMalformedAndWrongGenerationCompletions) {
  const auto config = snapshot("fallback.toml", config_document());
  auto active = runtime(config);
  asio::io_context ioc;

  const std::array cases = {
      std::pair{"missing", "command_completion_missing"},
      std::pair{"duplicate", "command_completion_duplicate"},
      std::pair{"malformed", "command_completion_malformed"},
      std::pair{"wrong_generation", "command_completion_mismatch"},
      std::pair{"failure", "command_actor_failure"},
  };
  for (const auto &[argument, code] : cases) {
    const auto result = run_awaitable(
        ioc,
        active.coordinator->process(raw(argument), std::make_shared<int>(5)));
    ASSERT_EQ(result.failures.size(), 1U);
    EXPECT_EQ(result.failures.front().failure.code, code);
  }
  EXPECT_EQ(active.actor->raw_count, cases.size());
}

TEST_F(CommandCoordinatorTest,
       ExactScopedPoliciesGateAliasesGroupsUsersAndPrivateCallers) {
  auto document = config_document("consume");
  replace_policy(
      document, "groups", "allowlist",
      R"([{ platform = "qq", bot = "primary", native_group_id = "42" }])");
  replace_policy(
      document, "users", "denylist",
      R"([{ platform = "qq", bot = "primary", native_user_id = "8" }])");
  const auto config = snapshot("access.toml", document);
  const auto built = obcx::core::build_command_routing_table(
      *config,
      {{"command_actor", command_contract(true, R"(^(?:test|alias)$)")}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  EXPECT_TRUE(built.table->permits(
      "test", {.platform = "qq",
               .bot = "primary",
               .conversation = obcx::core::CommandConversationKind::Group,
               .group_id = "42",
               .user_id = "7"}));
  EXPECT_FALSE(built.table->permits(
      "test", {.platform = "qq",
               .bot = "secondary",
               .conversation = obcx::core::CommandConversationKind::Group,
               .group_id = "42",
               .user_id = "7"}));

  auto active = runtime(config, built.table);
  asio::io_context ioc;
  auto allowed = run_awaitable(
      ioc, active.coordinator->process(raw_command("alias", "consume"),
                                       std::make_shared<int>(20)));
  EXPECT_TRUE(allowed.ok());
  EXPECT_EQ(active.actor->command_count, 1);

  auto denied_group = raw_command("test", "consume");
  denied_group.raw["raw_message"] = "/test do-not-log-this";
  denied_group.conversation_id = "group:43";
  denied_group.payload["group_id"] = "43";
  auto denied = run_awaitable(
      ioc, active.coordinator->process(std::move(denied_group),
                                       std::make_shared<int>(21)));
  ASSERT_EQ(denied.failures.size(), 1U);
  EXPECT_EQ(denied.failures.front().failure.code, "command_access_denied");
  EXPECT_EQ(denied.failures.front().failure.message,
            "command access was denied");
  EXPECT_EQ(denied.failures.front().failure.message.find("do-not-log-this"),
            std::string::npos);

  auto denied_user = raw_command("test", "consume");
  denied_user.payload["sender"] = "8";
  denied = run_awaitable(
      ioc, active.coordinator->process(std::move(denied_user),
                                       std::make_shared<int>(22)));
  ASSERT_EQ(denied.failures.size(), 1U);
  EXPECT_EQ(denied.failures.front().failure.code, "command_access_denied");

  auto private_message = raw_command("test", "consume");
  private_message.conversation_id = "private:7";
  private_message.payload["message_type"] = "private";
  private_message.payload["group_id"] = "";
  private_message.payload["sender"] = "7";
  allowed = run_awaitable(
      ioc, active.coordinator->process(std::move(private_message),
                                       std::make_shared<int>(23)));
  EXPECT_TRUE(allowed.ok());
  EXPECT_EQ(active.actor->command_count, 2);

  auto missing_sender = raw_command("test", "consume");
  missing_sender.payload["sender"] = "";
  denied = run_awaitable(
      ioc, active.coordinator->process(std::move(missing_sender),
                                       std::make_shared<int>(24)));
  ASSERT_EQ(denied.failures.size(), 1U);
  EXPECT_EQ(denied.failures.front().failure.code, "command_access_denied");

  auto fallback_bot = raw_command("test", "consume");
  fallback_bot.payload["source_bot_configured"] = false;
  denied = run_awaitable(
      ioc, active.coordinator->process(std::move(fallback_bot),
                                       std::make_shared<int>(25)));
  ASSERT_EQ(denied.failures.size(), 1U);
  EXPECT_EQ(denied.failures.front().failure.code, "command_access_denied");
  EXPECT_EQ(active.actor->command_count, 2);
  EXPECT_EQ(active.actor->raw_count, 0);
  EXPECT_TRUE(active.gateway->operations.empty());
}

TEST_F(CommandCoordinatorTest, CanonicalOverrideReplacesBothGlobalPolicies) {
  auto document = config_document("consume");
  replace_policy(
      document, "groups", "denylist",
      R"([{ platform = "qq", bot = "primary", native_group_id = "42" }])");
  replace_policy(
      document, "users", "denylist",
      R"([{ platform = "qq", bot = "primary", native_user_id = "7" }])");
  document += R"(

[[command_runtime.access.overrides]]
command = "test"
groups = { mode = "allowlist", entries = [{ platform = "qq", bot = "primary", native_group_id = "42" }] }
users = { mode = "allowlist", entries = [{ platform = "qq", bot = "primary", native_user_id = "7" }] }
)";
  const auto config = snapshot("override.toml", document);
  const auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", command_contract(true, R"(^alias$)")}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  auto active = runtime(config, built.table);
  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("alias", "consume"),
                                       std::make_shared<int>(25)));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(active.actor->command_count, 1);

  auto inactive_document = config_document();
  inactive_document += R"(

[[command_runtime.access.overrides]]
command = "inactive"
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
)";
  const auto inactive = snapshot("inactive-override.toml", inactive_document);
  const auto rejected = table(inactive);
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "command_access_override_inactive");
}

TEST_F(CommandCoordinatorTest,
       HelpIsActorFreeFilteredDeterministicAndUsesExactGroupOrPrivateTarget) {
  auto document = config_document("consume");
  replace_policy(
      document, "groups", "denylist",
      R"([{ platform = "qq", bot = "primary", native_group_id = "42" }])");
  document += R"(

[[command_runtime.access.overrides]]
command = "help"
groups = { mode = "unrestricted", entries = [] }
users = { mode = "unrestricted", entries = [] }
)";
  const auto config = snapshot("help-filter.toml", document);
  auto active = runtime(config);
  asio::io_context ioc;
  auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("help", ""),
                                       std::make_shared<int>(30)));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(active.actor->command_count, 0);
  EXPECT_EQ(active.actor->raw_count, 0);
  ASSERT_EQ(active.gateway->operations.size(), 1U);
  const auto &group_reply = active.gateway->operations.front();
  EXPECT_EQ(group_reply.action, obcx::bot::SendGroupMessageRequest::action);
  EXPECT_EQ(group_reply.payload.at("target").at("native_group_id"), "42");
  const auto text = group_reply.payload.at("message")
                        .at(0)
                        .at("data")
                        .at("text")
                        .get<std::string>();
  EXPECT_EQ(text, "/help - List commands available to you\n");
  EXPECT_EQ(text.find("/test"), std::string::npos);

  auto private_help = raw_command("help", "");
  private_help.conversation_id = "private:7";
  private_help.payload["message_type"] = "private";
  private_help.payload["group_id"] = "";
  result = run_awaitable(
      ioc, active.coordinator->process(std::move(private_help),
                                       std::make_shared<int>(31)));
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(active.gateway->operations.size(), 2U);
  EXPECT_EQ(active.gateway->operations.back().action,
            obcx::bot::SendPrivateMessageRequest::action);
  EXPECT_EQ(active.gateway->operations.back().payload.at("target").at(
                "native_user_id"),
            "7");

  result = run_awaitable(
      ioc, active.coordinator->process(raw_command("help", "unexpected"),
                                       std::make_shared<int>(32)));
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "invalid_help_arguments");
  EXPECT_EQ(active.gateway->operations.size(), 2U);
}

TEST_F(CommandCoordinatorTest,
       HelpPagesAreWholeBoundedSequentialAndStopAfterAmbiguity) {
  auto document = config_document("consume");
  const auto bytes = document.find("page_bytes = 3500");
  ASSERT_NE(bytes, std::string::npos);
  document.replace(bytes, std::string{"page_bytes = 3500"}.size(),
                   "page_bytes = 50");
  const auto config = snapshot("help-pages.toml", document);
  auto active = runtime(config);
  asio::io_context ioc;
  auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("help", ""),
                                       std::make_shared<int>(33)));
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(active.gateway->operations.size(), 2U);
  for (const auto &operation : active.gateway->operations) {
    const auto page = operation.payload.at("message")
                          .at(0)
                          .at("data")
                          .at("text")
                          .get<std::string>();
    EXPECT_LE(page.size(), 50U);
    EXPECT_EQ(page.back(), '\n');
  }

  auto failed = runtime(config);
  failed.gateway->replies.push_back(obcx::bot::OperationReply::failure(
      {.code = obcx::bot::BotOperationErrorCode::TransportFailure,
       .message = "not submitted",
       .retryable = true,
       .submission_safety =
           obcx::bot::SubmissionSafety::DefinitelyNotSubmitted}));
  result = run_awaitable(
      ioc, failed.coordinator->process(raw_command("help", ""),
                                       std::make_shared<int>(34)));
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code,
            "command_help_delivery_failed");
  EXPECT_EQ(failed.gateway->operations.size(), 1U);
  EXPECT_EQ(failed.actor->command_count, 0);

  auto uncertain = runtime(config);
  uncertain.gateway->replies.push_back(obcx::bot::OperationReply::failure(
      {.code = obcx::bot::BotOperationErrorCode::OutcomeUnknown,
       .message = "outcome unknown",
       .retryable = false,
       .submission_safety = obcx::bot::SubmissionSafety::PossiblySubmitted}));
  result = run_awaitable(
      ioc, uncertain.coordinator->process(raw_command("help", ""),
                                          std::make_shared<int>(35)));
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code,
            "command_help_delivery_uncertain");
  EXPECT_EQ(uncertain.gateway->operations.size(), 1U);
  EXPECT_EQ(uncertain.actor->command_count, 0);
}

TEST_F(CommandCoordinatorTest,
       HelpRenderingPreservesCompleteUnicodeEntriesAtPageBoundaries) {
  auto document = config_document("consume");
  const auto bytes = document.find("page_bytes = 3500");
  ASSERT_NE(bytes, std::string::npos);
  document.replace(bytes, std::string{"page_bytes = 3500"}.size(),
                   "page_bytes = 50");
  const auto config = snapshot("help-unicode.toml", document);
  auto contract = command_contract();
  contract.commands.front().description = "説明🙂";
  const auto built = obcx::core::build_command_routing_table(
      *config, {{"command_actor", std::move(contract)}});
  ASSERT_TRUE(built) << (built.failure ? built.failure->message : "");
  const auto *bot = built.table->find_bot({.platform = "qq", .bot = "primary"});
  ASSERT_NE(bot, nullptr);
  const auto rendered = built.table->render_help(
      *bot, {.platform = "qq",
             .bot = "primary",
             .conversation = obcx::core::CommandConversationKind::Group,
             .group_id = "42",
             .user_id = "7"});
  ASSERT_TRUE(rendered) << rendered.message;
  ASSERT_EQ(rendered.pages.size(), 2U);
  EXPECT_EQ(rendered.pages.back(), "/test - 説明🙂\n");
  EXPECT_LE(rendered.pages.front().size(), 50U);
  EXPECT_LE(rendered.pages.back().size(), 50U);
}

TEST_F(CommandCoordinatorTest,
       ShutdownStopsRemainingHelpPagesWithinTheAdmittingGeneration) {
  auto document = config_document("consume");
  const auto bytes = document.find("page_bytes = 3500");
  ASSERT_NE(bytes, std::string::npos);
  document.replace(bytes, std::string{"page_bytes = 3500"}.size(),
                   "page_bytes = 50");
  const auto config = snapshot("help-shutdown.toml", document);
  auto active = runtime(config);
  active.gateway->on_invoke =
      [coordinator = active.coordinator](const std::size_t invocation) {
        if (invocation == 1U) {
          coordinator->shutdown();
        }
      };

  asio::io_context ioc;
  const auto result = run_awaitable(
      ioc, active.coordinator->process(raw_command("help", ""),
                                       std::make_shared<int>(35)));
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_cancelled");
  EXPECT_EQ(active.gateway->operations.size(), 1U);
  EXPECT_EQ(active.actor->command_count, 0);
  EXPECT_EQ(active.actor->raw_count, 0);
}

TEST_F(CommandCoordinatorTest,
       RejectsReservedNamesInvalidUtf8AndUnsatisfiedHelpBounds) {
  const auto config = snapshot("reserved.toml", config_document());
  auto reserved = command_contract();
  reserved.commands.front().name = "help";
  auto rejected = obcx::core::build_command_routing_table(
      *config, {{"command_actor", reserved}});
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "command_reserved_name");

  auto invalid_utf8 = command_contract();
  invalid_utf8.commands.front().description = std::string{"bad\xFF", 4};
  rejected = obcx::core::build_command_routing_table(
      *config, {{"command_actor", invalid_utf8}});
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "command_help_entry_invalid");

  auto control_text = command_contract();
  control_text.commands.front().description = "line one\nline two";
  rejected = obcx::core::build_command_routing_table(
      *config, {{"command_actor", control_text}});
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "command_help_entry_invalid");

  auto too_small = config_document();
  const auto bytes = too_small.find("page_bytes = 3500");
  ASSERT_NE(bytes, std::string::npos);
  too_small.replace(bytes, std::string{"page_bytes = 3500"}.size(),
                    "page_bytes = 10");
  const auto bounded = snapshot("too-small.toml", too_small);
  rejected = table(bounded);
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "command_help_entry_too_large");

  auto too_few_pages = config_document();
  const auto page_bytes = too_few_pages.find("page_bytes = 3500");
  too_few_pages.replace(page_bytes, std::string{"page_bytes = 3500"}.size(),
                        "page_bytes = 50");
  const auto page_count = too_few_pages.find("maximum_pages = 10");
  too_few_pages.replace(page_count, std::string{"maximum_pages = 10"}.size(),
                        "maximum_pages = 1");
  const auto paged = snapshot("too-few-pages.toml", too_few_pages);
  rejected = table(paged);
  ASSERT_TRUE(rejected.failure);
  EXPECT_EQ(rejected.failure->code, "command_help_page_limit_exceeded");
}

TEST_F(CommandCoordinatorTest, TimesOutCooperativeActorAndUsesFallback) {
  const auto config = snapshot("timeout.toml", config_document());
  auto active = runtime(config);
  asio::io_context ioc;
  const auto started = std::chrono::steady_clock::now();
  const auto result =
      run_awaitable(ioc, active.coordinator->process(raw("timeout"),
                                                     std::make_shared<int>(6)));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_timeout");
  EXPECT_EQ(active.actor->raw_count, 1);
  EXPECT_LT(elapsed, 2s);
}

TEST_F(CommandCoordinatorTest,
       ShutdownCancelsPendingCommandAndCompletesSourceOnce) {
  auto document = config_document("consume");
  document.replace(document.find("timeout_ms = 100"),
                   std::string{"timeout_ms = 100"}.size(), "timeout_ms = 5000");
  const auto config = snapshot("shutdown.toml", document);
  auto active = runtime(config);
  asio::io_context ioc;
  auto completed = asio::co_spawn(
      ioc,
      active.coordinator->process(raw("timeout"), std::make_shared<int>(7)),
      asio::use_future);
  std::jthread io_thread([&] { ioc.run(); });

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (active.actor->command_count == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_EQ(active.actor->command_count, 1);

  active.coordinator->shutdown();
  active.orchestrator->shutdown();
  ASSERT_EQ(completed.wait_for(2s), std::future_status::ready);
  const auto result = completed.get();
  ASSERT_EQ(result.failures.size(), 1U);
  EXPECT_EQ(result.failures.front().failure.code, "command_actor_failure");
  EXPECT_EQ(active.actor->raw_count, 0);
}

} // namespace
