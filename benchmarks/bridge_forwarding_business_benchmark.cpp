#include "common/config_loader.hpp"
#include "common/logger.hpp"
#include "core/bot_registry.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "core/qq_bot.hpp"
#include "core/runtime/runtime_generation.hpp"
#include "core/tg_bot.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace asio = boost::asio;
namespace fs = std::filesystem;

struct Options {
  fs::path message_store_actor;
  fs::path bridge_actor;
  std::string label = "candidate";
  std::size_t messages = 10000;
  std::size_t concurrency = 256;
  std::size_t warmup = 512;
  std::size_t groups = 8;
  std::size_t users = 64;
  std::size_t reply_every = 5;
  std::optional<std::size_t> expected_post_send_recovery_reads;
  std::optional<std::size_t> expected_mapping_upserts;
  fs::path sandbox_parent = fs::temp_directory_path();
};

struct MappingOperationCounts {
  std::atomic_size_t target_selects = 0;
  std::atomic_size_t upserts = 0;
};

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

auto parse_positive(const std::string &value, const char *name) -> std::size_t {
  std::size_t consumed = 0;
  const auto parsed = std::stoull(value, &consumed);
  if (consumed != value.size() || parsed == 0 ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    throw std::invalid_argument(std::string{name} + " must be positive");
  }
  return static_cast<std::size_t>(parsed);
}

auto parse_nonnegative(const std::string &value, const char *name)
    -> std::size_t {
  std::size_t consumed = 0;
  const auto parsed = std::stoull(value, &consumed);
  if (consumed != value.size() ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    throw std::invalid_argument(std::string{name} + " must be nonnegative");
  }
  return static_cast<std::size_t>(parsed);
}

auto parse_options(const int argc, char **argv) -> Options {
  if (argc < 3) {
    throw std::invalid_argument(
        "usage: bridge_forwarding_business_benchmark "
        "MESSAGE_STORE_ACTOR BRIDGE_ACTOR "
        "[--label NAME] [--messages N] [--concurrency N] [--warmup N] "
        "[--groups N] [--users N] [--reply-every N] "
        "[--expect-post-send-recovery-reads N] "
        "[--expect-mapping-upserts N] "
        "[--sandbox-parent PATH]");
  }

  Options options{
      .message_store_actor = argv[1],
      .bridge_actor = argv[2],
  };
  for (int index = 3; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("benchmark option is missing its value");
    }
    const std::string option = argv[index];
    const std::string value = argv[index + 1];
    if (option == "--label") {
      options.label = value;
    } else if (option == "--messages") {
      options.messages = parse_positive(value, "messages");
    } else if (option == "--concurrency") {
      options.concurrency = parse_positive(value, "concurrency");
    } else if (option == "--warmup") {
      options.warmup = parse_positive(value, "warmup");
    } else if (option == "--groups") {
      options.groups = parse_positive(value, "groups");
    } else if (option == "--users") {
      options.users = parse_positive(value, "users");
    } else if (option == "--reply-every") {
      options.reply_every = parse_positive(value, "reply-every");
    } else if (option == "--expect-post-send-recovery-reads") {
      options.expected_post_send_recovery_reads =
          parse_nonnegative(value, "expect-post-send-recovery-reads");
    } else if (option == "--expect-mapping-upserts") {
      options.expected_mapping_upserts =
          parse_nonnegative(value, "expect-mapping-upserts");
    } else if (option == "--sandbox-parent") {
      options.sandbox_parent = value;
    } else {
      throw std::invalid_argument("unknown benchmark option: " + option);
    }
  }
  options.concurrency = std::min(options.concurrency, options.messages);
  return options;
}

class Sandbox {
public:
  explicit Sandbox(fs::path parent)
      : parent_(fs::absolute(std::move(parent)).lexically_normal()) {
    std::error_code parent_error;
    fs::create_directories(parent_, parent_error);
    if (parent_error) {
      throw std::runtime_error("cannot create benchmark sandbox parent: " +
                               parent_error.message());
    }
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    for (std::size_t attempt = 0; attempt < 100; ++attempt) {
      root_ = parent_ /
              ("obcx-bridge-business-benchmark-" + std::to_string(stamp) +
               "-" + std::to_string(attempt));
      std::error_code error;
      if (fs::create_directory(root_, error)) {
        fs::create_directory(root_ / "files");
        fs::create_directory(root_ / "staging");
        return;
      }
      if (error) {
        throw std::runtime_error("cannot create benchmark sandbox: " +
                                 error.message());
      }
    }
    throw std::runtime_error("cannot allocate unique benchmark sandbox");
  }

  Sandbox(const Sandbox &) = delete;
  auto operator=(const Sandbox &) -> Sandbox & = delete;

  ~Sandbox() {
    const auto normalized = root_.lexically_normal();
    if (normalized.parent_path() != parent_ ||
        !normalized.filename().string().starts_with(
            "obcx-bridge-business-benchmark-")) {
      return;
    }
    std::error_code error;
    fs::remove_all(normalized, error);
  }

  [[nodiscard]] auto root() const -> const fs::path & { return root_; }
  [[nodiscard]] auto database() const -> fs::path {
    return root_ / "bridge-business.sqlite3";
  }
  [[nodiscard]] auto config() const -> fs::path {
    return root_ / "bridge-business.toml";
  }

private:
  fs::path parent_;
  fs::path root_;
};

class CountingDbConnection final : public obcx::core::IDbConnection {
public:
  CountingDbConnection(
      std::shared_ptr<obcx::core::IDbConnection> delegate,
      std::shared_ptr<MappingOperationCounts> mapping_operations)
      : delegate_(std::move(delegate)),
        mapping_operations_(std::move(mapping_operations)) {}

  void execute(const std::string &sql,
               const obcx::core::DbParams &params = {}) override {
    if (sql.find("INSERT OR REPLACE INTO bridge_message_mappings") !=
        std::string::npos) {
      mapping_operations_->upserts.fetch_add(1, std::memory_order_relaxed);
    }
    delegate_->execute(sql, params);
  }

  [[nodiscard]] auto query(const std::string &sql,
                           const obcx::core::DbParams &params = {})
      -> std::vector<obcx::core::DbRow> override {
    if (sql.find("SELECT target_message_id FROM bridge_message_mappings") !=
        std::string::npos) {
      mapping_operations_->target_selects.fetch_add(
          1, std::memory_order_relaxed);
    }
    return delegate_->query(sql, params);
  }

  void run_write_task(
      std::function<void(obcx::core::IDbConnection &)> work) override {
    delegate_->run_write_task(
        [this, work = std::move(work)](obcx::core::IDbConnection &) mutable {
          work(*this);
        });
  }

  void run_transaction_task(
      std::function<void(obcx::core::IDbConnection &)> work) override {
    delegate_->run_transaction_task(
        [this, work = std::move(work)](obcx::core::IDbConnection &) mutable {
          work(*this);
        });
  }

  void with_migration_lock(
      const std::string &namespace_name,
      std::function<void(obcx::core::IDbConnection &)> work) override {
    delegate_->with_migration_lock(
        namespace_name,
        [this, work = std::move(work)](obcx::core::IDbConnection &) mutable {
          work(*this);
        });
  }

private:
  std::shared_ptr<obcx::core::IDbConnection> delegate_;
  std::shared_ptr<MappingOperationCounts> mapping_operations_;
};

class CountingDbProvider final : public obcx::core::IDbProvider {
public:
  CountingDbProvider(
      std::shared_ptr<obcx::core::DbManager> delegate,
      std::shared_ptr<MappingOperationCounts> mapping_operations)
      : delegate_(std::move(delegate)),
        mapping_operations_(std::move(mapping_operations)) {}

  [[nodiscard]] auto create_connection(
      const obcx::common::DbInstanceConfig &config)
      -> std::shared_ptr<obcx::core::IDbConnection> override {
    return std::make_shared<CountingDbConnection>(
        delegate_->connection(config.name), mapping_operations_);
  }

private:
  std::shared_ptr<obcx::core::DbManager> delegate_;
  std::shared_ptr<MappingOperationCounts> mapping_operations_;
};

class MockQQBot final : public obcx::core::QQBot {
public:
  MockQQBot() : QQBot(obcx::adapter::onebot11::ProtocolAdapter{}) {}

  auto get_group_member_info(std::string_view group_id,
                             std::string_view user_id, bool)
      -> asio::awaitable<std::string> override {
    member_lookups_.fetch_add(1, std::memory_order_relaxed);
    co_return "{\"status\":\"ok\",\"data\":{\"group_id\":\"" +
        std::string{group_id} + "\",\"user_id\":\"" + std::string{user_id} +
        "\",\"nickname\":\"benchmark-" + std::string{user_id} +
        "\",\"card\":\"\"}}";
  }

  [[nodiscard]] auto member_lookups() const noexcept -> std::size_t {
    return member_lookups_.load(std::memory_order_relaxed);
  }

private:
  std::atomic_size_t member_lookups_ = 0;
};

class MockTelegramBot final : public obcx::core::TGBot {
public:
  MockTelegramBot() : TGBot(obcx::adapter::telegram::ProtocolAdapter{}) {}

  auto send_group_message(std::string_view group_id,
                          const obcx::common::Message &message)
      -> asio::awaitable<std::string> override {
    if (!group_id.starts_with("benchmark-tg-group-") || message.empty()) {
      invalid_sends_.fetch_add(1, std::memory_order_relaxed);
      co_return "{}";
    }
    const auto sequence =
        sends_.fetch_add(1, std::memory_order_relaxed) + 1000000;
    co_return "{\"ok\":true,\"result\":{\"message_id\":" +
        std::to_string(sequence) + "}}";
  }

  [[nodiscard]] auto sends() const noexcept -> std::size_t {
    return sends_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto invalid_sends() const noexcept -> std::size_t {
    return invalid_sends_.load(std::memory_order_relaxed);
  }

private:
  std::atomic_size_t sends_ = 0;
  std::atomic_size_t invalid_sends_ = 0;
};

void write_config(const fs::path &path, const fs::path &database,
                  const fs::path &files, const std::size_t groups) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot create benchmark configuration");
  }

  output
      << "[bots.qq_benchmark]\n"
         "type = \"qq\"\n"
         "enabled = true\n\n"
         "[bots.qq_benchmark.connection]\n"
         "type = \"websocket\"\n"
         "host = \"127.0.0.1\"\n"
         "port = 1\n"
         "access_token = \"mock-only\"\n\n"
         "[bots.telegram_benchmark]\n"
         "type = \"telegram\"\n"
         "enabled = true\n\n"
         "[bots.telegram_benchmark.connection]\n"
         "type = \"http\"\n"
         "host = \"mock.invalid\"\n"
         "port = 443\n"
         "access_token = \"mock-only\"\n\n"
         "[actor_runtime.scheduler]\n"
         "policy = \"stealing\"\n"
         "workers = 0\n"
         "blocking_workers = 0\n"
         "slow_resume_warning_ms = 0\n\n"
         "[actor_runtime.routing]\n"
         "hop_limit = 32\n\n"
         "[db.instances.main]\n"
         "type = \"sqlite\"\n"
         "path = \""
      << database.string()
      << "\"\n\n"
         "[actors.message_store]\n"
         "library = \"message_store\"\n"
         "enabled = true\n"
         "partition = \"source_platform:conversation_id\"\n"
         "db = \"main\"\n"
         "db_namespace = \"message_store\"\n\n"
         "[actors.bridge]\n"
         "library = \"bridge\"\n"
         "enabled = true\n"
         "requires = [\"message_store\"]\n"
         "partition = \"source_platform:conversation_id\"\n"
         "db = \"main\"\n"
         "db_namespace = \"bridge\"\n\n"
         "[actors.bridge.config]\n"
         "database_file = \""
      << database.string()
      << "\"\n"
         "enable_retry_queue = true\n"
         "message_retry_base_interval_sec = 300\n"
         "retry_queue_check_interval_sec = 300\n"
         "max_retry_interval_sec = 300\n"
         "bridge_files_dir = \""
      << files.string()
      << "\"\n"
         "bridge_files_container_dir = \"/tmp/mock-bridge-files\"\n"
         "ffmpeg_path = \"ffmpeg\"\n\n"
         "[pipelines.message]\n"
         "source = \"obcx::core::events::RawMessageEvent\"\n\n"
         "[[pipelines.message.stages]]\n"
         "name = \"persist\"\n"
         "actor = \"message_store\"\n"
         "input = \"obcx::core::events::RawMessageEvent\"\n"
         "output = \"obcx::message_store::events::MessageStored\"\n"
         "mode = \"await\"\n\n"
         "[[pipelines.message.stages]]\n"
         "name = \"forward\"\n"
         "actor = \"bridge\"\n"
         "input = \"obcx::message_store::events::MessageStored\"\n"
         "output = [\"bridge::events::MessageForwarded\", "
         "\"bridge::events::MessageForwardFailed\"]\n"
         "after = [\"persist\"]\n"
         "mode = \"await\"\n\n";

  for (std::size_t group = 0; group < groups; ++group) {
    output
        << "[[group_mappings.group_to_group]]\n"
           "telegram_group_id = \"benchmark-tg-group-"
        << group
        << "\"\n"
           "qq_group_id = \"benchmark-qq-group-"
        << group
        << "\"\n"
           "mode = \"group_to_group\"\n"
           "show_qq_to_tg_sender = true\n"
           "show_tg_to_qq_sender = false\n"
           "enable_qq_to_tg = true\n"
           "enable_tg_to_qq = true\n\n";
  }
  if (!output) {
    throw std::runtime_error("cannot write benchmark configuration");
  }
}

auto raw_message(const std::string &phase, const std::size_t sequence,
                 const Options &options, const bool reply)
    -> obcx::core::MessageEnvelope {
  const auto group = sequence % options.groups;
  const auto group_id = "benchmark-qq-group-" + std::to_string(group);
  const auto user_id =
      "benchmark-user-" + std::to_string(sequence % options.users);
  const auto message_id =
      phase == "seed" ? "seed-" + std::to_string(group)
                      : phase + "-" + std::to_string(sequence);
  const auto text = "bridge business benchmark " + message_id;

  obcx::common::json segments = obcx::common::json::array();
  if (reply) {
    segments.push_back(
        {{"type", "reply"}, {"data", {{"id", "seed-" + std::to_string(group)}}}});
  }
  segments.push_back({{"type", "text"}, {"data", {{"text", text}}}});

  obcx::core::MessageEnvelope message;
  message.id = "raw-" + message_id;
  message.type = "obcx::core::events::RawMessageEvent";
  message.source_platform = "qq";
  message.source_bot = "qq_benchmark";
  message.conversation_id = "group:" + group_id;
  message.correlation_id = "correlation-" + message_id;
  message.payload = {
      {"message_id", message_id},
      {"conversation_id", message.conversation_id},
      {"sender", user_id},
      {"group_id", group_id},
      {"message_type", "group"},
      {"payload", {{"text", text}, {"message", segments}}},
  };
  message.raw = {
      {"post_type", "message"},
      {"message_type", "group"},
      {"sub_type", "normal"},
      {"message_id", message_id},
      {"user_id", user_id},
      {"group_id", group_id},
      {"raw_message", text},
      {"message", std::move(segments)},
  };
  return message;
}

struct BatchState {
  explicit BatchState(const std::size_t count) : latencies_us(count) {}

  std::vector<double> latencies_us;
  std::atomic_size_t completed = 0;
  std::atomic_size_t failures = 0;
  std::promise<void> done;
  std::mutex error_mutex;
  std::string first_error;
};

void record_error(const std::shared_ptr<BatchState> &state,
                  std::string message) {
  state->failures.fetch_add(1, std::memory_order_relaxed);
  std::scoped_lock lock(state->error_mutex);
  if (state->first_error.empty()) {
    state->first_error = std::move(message);
  }
}

auto run_batch(
    asio::io_context &io,
    const std::shared_ptr<obcx::core::RuntimeGeneration> &generation,
    const Options &options, const std::string &phase,
    const std::size_t first_sequence, const std::size_t count,
    const bool enable_replies) -> std::vector<double> {
  auto state = std::make_shared<BatchState>(count);
  auto done = state->done.get_future();

  for (std::size_t index = 0; index < count; ++index) {
    const auto sequence = first_sequence + index;
    const bool reply =
        enable_replies && sequence % options.reply_every == 0;
    auto admission = generation->admit_route();
    require(static_cast<bool>(admission),
            "runtime generation rejected benchmark route");
    const auto started = std::chrono::steady_clock::now();
    asio::co_spawn(
        io,
        generation->process(raw_message(phase, sequence, options, reply),
                            std::move(admission)),
        [state, index, started](std::exception_ptr error,
                                obcx::core::OrchestratorResult result) {
          state->latencies_us[index] =
              std::chrono::duration<double, std::micro>(
                  std::chrono::steady_clock::now() - started)
                  .count();
          if (error) {
            try {
              std::rethrow_exception(error);
            } catch (const std::exception &exception) {
              record_error(state, exception.what());
            } catch (...) {
              record_error(state, "unknown pipeline exception");
            }
          } else if (!result.ok() || result.stages.size() != 2 ||
                     result.emitted.size() != 2 ||
                     result.emitted.back().type !=
                         "bridge::events::MessageForwarded") {
            record_error(state, "pipeline result failed validation");
          }
          if (state->completed.fetch_add(1, std::memory_order_acq_rel) + 1 ==
              state->latencies_us.size()) {
            state->done.set_value();
          }
        });
  }

  require(done.wait_for(std::chrono::seconds{120}) ==
              std::future_status::ready,
          "bridge business batch timed out");
  require(state->failures.load(std::memory_order_relaxed) == 0,
          state->first_error.empty() ? "bridge business batch failed"
                                     : state->first_error);
  return std::move(state->latencies_us);
}

auto run_workload(
    asio::io_context &io,
    const std::shared_ptr<obcx::core::RuntimeGeneration> &generation,
    const Options &options, const std::string &phase,
    const std::size_t first_sequence, const std::size_t count,
    const std::size_t concurrency, const bool enable_replies)
    -> std::vector<double> {
  std::vector<double> latencies;
  latencies.reserve(count);
  for (std::size_t offset = 0; offset < count; offset += concurrency) {
    const auto batch_size = std::min(concurrency, count - offset);
    auto batch = run_batch(io, generation, options, phase,
                           first_sequence + offset, batch_size, enable_replies);
    latencies.insert(latencies.end(),
                     std::make_move_iterator(batch.begin()),
                     std::make_move_iterator(batch.end()));
  }
  return latencies;
}

struct Summary {
  double elapsed_ms = 0;
  double mean_us = 0;
  double p50_us = 0;
  double p95_us = 0;
  double operations_per_second = 0;
};

auto summarize(std::vector<double> latencies,
               const std::chrono::steady_clock::duration elapsed) -> Summary {
  std::ranges::sort(latencies);
  const auto percentile = [&latencies](const double ratio) {
    const auto index = static_cast<std::size_t>(
        ratio * static_cast<double>(latencies.size() - 1));
    return latencies[index];
  };
  const auto elapsed_seconds = std::chrono::duration<double>(elapsed).count();
  return {
      .elapsed_ms = elapsed_seconds * 1000.0,
      .mean_us =
          std::accumulate(latencies.begin(), latencies.end(), 0.0) /
          static_cast<double>(latencies.size()),
      .p50_us = percentile(0.50),
      .p95_us = percentile(0.95),
      .operations_per_second =
          static_cast<double>(latencies.size()) / elapsed_seconds,
  };
}

auto scalar_count(obcx::core::DbManager &manager, const std::string &table)
    -> std::int64_t {
  return manager.run_read<std::int64_t>(
      "main", [&table](obcx::core::IDbConnection &connection) {
        const auto rows = connection.query("SELECT COUNT(*) AS count FROM \"" +
                                           table + "\";");
        require(!rows.empty(), "database count query returned no rows");
        return std::get<std::int64_t>(rows.front().at("count"));
      });
}

auto reply_count(const std::size_t first_sequence, const std::size_t count,
                 const std::size_t reply_every) -> std::size_t {
  std::size_t replies = 0;
  for (std::size_t sequence = first_sequence;
       sequence < first_sequence + count; ++sequence) {
    replies += sequence % reply_every == 0 ? 1U : 0U;
  }
  return replies;
}

} // namespace

auto main(int argc, char **argv) -> int {
  try {
    obcx::common::Logger::initialize(spdlog::level::off, "", false);
    const auto options = parse_options(argc, argv);
    require(fs::is_regular_file(options.message_store_actor),
            "message-store actor library does not exist");
    require(fs::is_regular_file(options.bridge_actor),
            "bridge actor library does not exist");

    Sandbox sandbox(options.sandbox_parent);
    write_config(sandbox.config(), sandbox.database(),
                 sandbox.root() / "files", options.groups);
    const auto production_database =
        fs::absolute("bridge_bot.db").lexically_normal();
    require(sandbox.database().lexically_normal() != production_database,
            "benchmark database must not use the production path");

    auto parsed = obcx::core::RuntimeGenerationBuilder::parse_config(
        sandbox.config().string());
    require(static_cast<bool>(parsed), "benchmark configuration did not parse");

    const auto database_configs = parsed.snapshot->get_db_instance_configs();
    auto underlying_database = std::make_shared<obcx::core::DbManager>();
    underlying_database->configure(database_configs);
    auto mapping_operations = std::make_shared<MappingOperationCounts>();
    auto database = std::make_shared<obcx::core::DbManager>();
    database->register_provider(
        "sqlite", std::make_shared<CountingDbProvider>(
                      underlying_database, mapping_operations));
    database->configure(database_configs);
    auto registry = std::make_shared<obcx::core::BotRegistry>();
    auto qq = std::make_shared<MockQQBot>();
    auto telegram = std::make_shared<MockTelegramBot>();
    registry->register_bot("qq", "qq_benchmark", qq);
    registry->register_bot("telegram", "telegram_benchmark", telegram);

    obcx::core::RuntimeGenerationBuilder builder;
    auto built = builder.build({
        .purpose = obcx::core::RuntimeGenerationBuildPurpose::Startup,
        .generation_id = 1,
        .snapshot = parsed.snapshot,
        .actor_search_directories =
            {
                options.message_store_actor.parent_path(),
                options.bridge_actor.parent_path(),
            },
        .staging_root = sandbox.root() / "staging",
        .configured_io_sources = 1,
        .db_manager = database,
        .bot_registry = registry,
        .require_registered_bots = true,
    });
    require(built.ready(),
            built.failure ? built.failure->code + ": " + built.failure->message
                          : "runtime generation was not ready");
    auto generation = std::move(built.generation);

    asio::io_context io;
    auto work = asio::make_work_guard(io);
    std::jthread io_thread([&io] { io.run(); });

    (void)run_workload(io, generation, options, "seed", 0, options.groups,
                       1, false);
    (void)run_workload(io, generation, options, "warmup", options.groups,
                       options.warmup, options.concurrency, true);

    const auto started = std::chrono::steady_clock::now();
    auto latencies =
        run_workload(io, generation, options, "measured",
                     options.groups + options.warmup, options.messages,
                     options.concurrency, true);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto summary = summarize(std::move(latencies), elapsed);

    const auto expected =
        static_cast<std::int64_t>(options.groups + options.warmup +
                                  options.messages);
    const auto persisted =
        scalar_count(*database, "message_store_qq_messages");
    const auto mappings =
        scalar_count(*database, "bridge_message_mappings");
    require(persisted == expected,
            "message-store persistence count does not match workload");
    require(mappings == expected,
            "bridge mapping count does not match workload");
    require(telegram->sends() == static_cast<std::size_t>(expected),
            "mock Telegram send count does not match workload");
    require(telegram->invalid_sends() == 0,
            "bridge produced an invalid mock Telegram send");

    const auto mapping_target_selects =
        mapping_operations->target_selects.load(std::memory_order_relaxed);
    const auto mapping_upserts =
        mapping_operations->upserts.load(std::memory_order_relaxed);
    const auto expected_pre_send_deduplication_reads =
        static_cast<std::size_t>(expected);
    const auto expected_reply_mapping_reads =
        reply_count(options.groups, options.warmup, options.reply_every) +
        reply_count(options.groups + options.warmup, options.messages,
                    options.reply_every);
    require(mapping_target_selects >=
                expected_pre_send_deduplication_reads +
                    expected_reply_mapping_reads,
            "mapping SELECT count is below required pre-send and reply reads");
    const auto inferred_post_send_recovery_reads =
        mapping_target_selects - expected_pre_send_deduplication_reads -
        expected_reply_mapping_reads;
    if (options.expected_post_send_recovery_reads.has_value()) {
      require(inferred_post_send_recovery_reads ==
                  *options.expected_post_send_recovery_reads,
              "post-send mapping recovery SELECT count does not match "
              "expectation");
    }
    if (options.expected_mapping_upserts.has_value()) {
      require(mapping_upserts == *options.expected_mapping_upserts,
              "primary mapping upsert count does not match expectation");
    }

    const auto budget = generation->thread_budget();
    generation->shutdown();
    work.reset();
    io.stop();
    io_thread.join();

    std::cout << "bridge_business"
              << " label=" << options.label
              << " messages=" << options.messages
              << " concurrency=" << options.concurrency
              << " groups=" << options.groups
              << " users=" << options.users
              << " reply_percent=" << (100 / options.reply_every)
              << " actor_workers=" << budget.actor_workers
              << " io_workers=" << budget.io_workers
              << " blocking_workers=" << budget.blocking_workers
              << " elapsed_ms=" << summary.elapsed_ms
              << " mean_us=" << summary.mean_us
              << " p50_us=" << summary.p50_us
              << " p95_us=" << summary.p95_us
              << " ops_per_second=" << summary.operations_per_second
              << " persisted=" << persisted
              << " mappings=" << mappings
              << " mapping_target_selects=" << mapping_target_selects
              << " expected_pre_send_deduplication_reads="
              << expected_pre_send_deduplication_reads
              << " expected_reply_mapping_reads="
              << expected_reply_mapping_reads
              << " inferred_post_send_recovery_reads="
              << inferred_post_send_recovery_reads
              << " mapping_upserts=" << mapping_upserts
              << " mock_sends=" << telegram->sends()
              << " mock_member_lookups=" << qq->member_lookups()
              << " database_scope=unique_sandbox"
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "bridge_forwarding_business_benchmark failed: "
              << error.what() << '\n';
    return 1;
  }
}
