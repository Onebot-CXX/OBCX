#include "builtin_bot_platforms.hpp"
#include "common/cli_handler.hpp"
#include "common/config_snapshot.hpp"
#include "common/logger.hpp"
#include "core/bot/bot_event_components.hpp"
#include "core/bot/bot_installation_assembler.hpp"
#include "core/bot/bot_installation_directory.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "core/runtime/actor_runtime_reload_controller.hpp"
#include "core/runtime/message_event_ingress.hpp"
#include "core/runtime/orchestrator.hpp"
#include "core/runtime/process_configuration.hpp"
#include "core/runtime/runtime_generation.hpp"
#include "core/runtime/runtime_thread_budget.hpp"
#include "tui/tui_app.hpp"

#include <algorithm>
#include <atomic>
#include <boost/asio/thread_pool.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <boost/program_options.hpp>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <pthread.h>
#include <spdlog/common.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace po = boost::program_options;

namespace {
std::atomic_bool g_should_stop = false;

std::mutex g_stop_mtx;
std::condition_variable g_stop_cv;

constexpr int BOT_SHUTDOWN_TIMEOUT_SECONDS = 5;

class SignalMonitor final {
public:
  SignalMonitor() {
    ::sigemptyset(&signals_);
    ::sigaddset(&signals_, SIGINT);
    ::sigaddset(&signals_, SIGTERM);
    error_ = pthread_sigmask(SIG_BLOCK, &signals_, nullptr);
    if (error_ != 0) {
      return;
    }

    thread_ = std::jthread([this](const std::stop_token stop) {
      while (!stop.stop_requested()) {
        constexpr timespec timeout{
            .tv_sec = 0,
            .tv_nsec = 100'000'000,
        };
        const auto signal = ::sigtimedwait(&signals_, nullptr, &timeout);
        if (signal == SIGINT || signal == SIGTERM) {
          bool expected = false;
          if (g_should_stop.compare_exchange_strong(expected, true)) {
            g_stop_cv.notify_one();
          }
          return;
        }
        if (signal < 0 && errno != EAGAIN && errno != EINTR) {
          return;
        }
      }
    });
  }

  ~SignalMonitor() {
    thread_.request_stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  SignalMonitor(const SignalMonitor &) = delete;
  auto operator=(const SignalMonitor &) -> SignalMonitor & = delete;

  [[nodiscard]] auto ready() const noexcept -> bool { return error_ == 0; }
  [[nodiscard]] auto error() const noexcept -> int { return error_; }

private:
  sigset_t signals_{};
  std::jthread thread_;
  int error_{0};
};

void print_version() {
  fmt::print("OBCX Robot Framework v1.1.0\n");
  fmt::print("An actor-based bot framework supporting QQ and Telegram\n");
}

void print_help(const po::options_description &desc) {
  fmt::print("Usage: obcx [OPTIONS] [CONFIG_FILE]\n");
  fmt::print("\n");
  fmt::print("OPTIONS:\n");
  std::cout << desc << "\n";
  fmt::print("\n");
  fmt::print("CONFIG_FILE:\n");
  fmt::print("  Path to TOML configuration file (default: config.toml)\n");
}

auto actor_search_directories(const char *argv0)
    -> std::vector<std::filesystem::path> {
  namespace fs = std::filesystem;
  std::error_code error;
  fs::path executable;
#if defined(__linux__)
  executable = fs::read_symlink("/proc/self/exe", error);
#endif
  if (executable.empty()) {
    error.clear();
    executable =
        fs::absolute(argv0 == nullptr ? fs::path{} : fs::path{argv0}, error);
  }
  if (!executable.empty()) {
    error.clear();
    auto canonical = fs::weakly_canonical(executable, error);
    if (!error) {
      executable = std::move(canonical);
    }
  }

  std::vector<fs::path> directories;
  std::unordered_set<std::string> seen;
  const auto add_directory = [&](fs::path directory) {
    error.clear();
    directory = fs::weakly_canonical(directory, error);
    if (error || !fs::is_directory(directory, error)) {
      return;
    }
    auto key = directory.string();
    if (seen.insert(key).second) {
      directories.push_back(std::move(directory));
    }
  };

  if (!executable.empty()) {
    const auto executable_directory = executable.parent_path();
    const auto install_prefix = executable_directory.parent_path();
    add_directory(install_prefix / "lib" / "obcx" / "actors");
    add_directory(install_prefix / "lib64" / "obcx" / "actors");

    auto ancestor = executable_directory;
    for (size_t depth = 0; depth < 6 && !ancestor.empty(); ++depth) {
      add_directory(ancestor / "actors");
      const auto parent = ancestor.parent_path();
      if (parent == ancestor) {
        break;
      }
      ancestor = parent;
    }
  }

  error.clear();
  const auto working_directory = fs::current_path(error);
  if (!error) {
    add_directory(working_directory / "actors");
    add_directory(working_directory / "build" / "actors");
  }
  return directories;
}

} // namespace

namespace obcx::common {

class MainApplication {
public:
  static auto run(int argc, char *argv[]) -> int {
    SignalMonitor signal_monitor;
    if (!signal_monitor.ready()) {
      fmt::print(std::cerr, "Failed to initialize signal monitor: {}\n",
                 signal_monitor.error());
      return 1;
    }

    std::string config_path = "config.toml";
    bool use_tui = true;
    bool validation_only = false;

    try {
      po::options_description desc("Options");
      desc.add_options()("help,h", "Show this help message")(
          "version,v",
          "Show version information")("config", po::value<std::string>(),
                                      "Path to TOML configuration file")(
          "validate-config", po::value<std::string>(),
          "Validate config and actor contracts without starting runtime "
          "activity")("no-tui",
                      "Disable TUI, use stdout logging (useful for debuggers)");

      po::positional_options_description p;
      p.add("config", -1);

      po::variables_map vm;
      po::store(
          po::command_line_parser(argc, argv).options(desc).positional(p).run(),
          vm);
      po::notify(vm);

      if (vm.count("help")) {
        print_help(desc);
        return 0;
      }

      if (vm.count("version")) {
        print_version();
        return 0;
      }

      if (vm.count("config")) {
        config_path = vm["config"].as<std::string>();
      }

      if (vm.count("validate-config")) {
        config_path = vm["validate-config"].as<std::string>();
        validation_only = true;
      }

      use_tui = !validation_only && !vm.count("no-tui");

    } catch (const po::error &e) {
      fmt::print(std::cerr, "Error parsing arguments: {}\n", e.what());
      return 1;
    }

    // Honors OBCX_LOG_LEVEL env var; defaults to info.
    auto log_level = common::Logger::get_level_from_env();
    common::Logger::initialize(
        log_level,
        fmt::format("logs/obcx-bridge-{}.log",
                    boost::posix_time::to_iso_extended_string(
                        boost::posix_time::second_clock::local_time())),
        use_tui);

    const auto platform_catalog =
        obcx::app::make_builtin_bot_platform_catalog();
    core::RuntimeGenerationBuilder generation_builder{platform_catalog};
    auto parsed = generation_builder.parse_config(config_path);
    if (!parsed) {
      fmt::print(std::cerr, "Failed to load configuration from: {}\n",
                 config_path);
      return 1;
    }
    auto config_snapshot = std::move(parsed.snapshot);
    auto bot_configs = config_snapshot->get_bot_configs();
    const auto &bot_plans = core::ProcessConfigAccess::plans(*config_snapshot);
    try {
      for (const auto &plan : bot_plans) {
        (void)core::BotInstallationAssembler::validate(*plan);
      }
    } catch (const std::exception &error) {
      OBCX_ERROR("Bot installation recipe validation failed: {}", error.what());
      return 1;
    }

    std::shared_ptr<core::DbManager> process_db_manager;
    try {
      process_db_manager = core::DbManager::shared_manager(
          config_snapshot->get_db_instance_configs());
    } catch (...) {
      OBCX_ERROR("Failed to initialize process-owned database services");
      return 1;
    }
    auto process_bot_installation_directory =
        std::make_shared<core::BotInstallationDirectory>();
    auto process_bot_operation_dispatcher =
        std::make_shared<core::BotOperationDispatcher>(
            [platform_catalog](const bot::SurfaceId &surface) {
              return platform_catalog->supports(surface);
            });

    OBCX_INFO("OBCX Robot Framework starting...");
    OBCX_INFO("Configuration loaded from: {}", config_path);

    const auto actor_directories = actor_search_directories(argv[0]);

    auto actor_runtime_build = generation_builder.build(
        {.purpose = validation_only
                        ? core::RuntimeGenerationBuildPurpose::ValidationOnly
                        : core::RuntimeGenerationBuildPurpose::Startup,
         .generation_id = 1,
         .snapshot = config_snapshot,
         .actor_search_directories = actor_directories,
         .configured_io_sources = std::max<size_t>(1, bot_configs.size()),
         .db_manager = process_db_manager,
         .bot_installation_directory = process_bot_installation_directory,
         .bot_operation_client = process_bot_operation_dispatcher});
    if (actor_runtime_build.status ==
        core::RuntimeGenerationBuildStatus::Failed) {
      const auto &failure = *actor_runtime_build.failure;
      OBCX_ERROR("Actor runtime build failed [{}]: {}", failure.code,
                 failure.message);
      return 1;
    }
    if (validation_only) {
      fmt::print("Configuration and actor contracts are valid: {}\n",
                 config_path);
      return 0;
    }

    auto actor_runtime = std::move(actor_runtime_build.generation);

    if (bot_configs.empty()) {
      OBCX_ERROR("No bot configurations found");
      return 1;
    }

    std::vector<std::unique_ptr<core::BotInstallation>> bots;
    std::vector<std::thread> bot_threads;
    std::vector<std::future<void>> bot_thread_completions;
    auto process_blocking_executor =
        actor_runtime ? actor_runtime->blocking_executor() : nullptr;
    if (process_blocking_executor) {
      OBCX_INFO("Created process BlockingExecutor with {} threads",
                process_blocking_executor->worker_count());
    }
    auto reload_controller =
        actor_runtime ? std::make_shared<core::ActorRuntimeReloadController>(
                            actor_runtime)
                      : nullptr;
    // From this point the controller is the sole owner of the active slot.
    // Route leases retain retired generations until their final descendant.
    actor_runtime.reset();

    for (const auto &plan : bot_plans) {
      const auto &config = plan->metadata();
      if (!config.enabled) {
        OBCX_INFO("Skipping disabled bot installation: {}",
                  config.installation_id);
        continue;
      }

      const auto &bot_platform = config.ingress_platform;
      const auto bot_instance = config.installation_id;
      try {
        auto installation = core::BotInstallationAssembler::assemble(*plan);
        auto events = installation->capability<core::BotEventCapability>(
            core::CapabilityId{"bot.events"});
        if (reload_controller && !bot_platform.empty()) {
          auto process_actor_event =
              [reload_controller, bot_platform, bot_instance](
                  std::string ingress_type, core::MessageEnvelope envelope)
              -> boost::asio::awaitable<void> {
            try {
              const auto result =
                  co_await reload_controller->process(std::move(envelope));
              if (!result.ok()) {
                const auto &first = result.failures.front();
                OBCX_ERROR(
                    "Actor runtime event ingress failed event_type={} "
                    "platform={} bot={} failures={} pipeline={} stage={} "
                    "actor={} code={} retryable={} message={}",
                    ingress_type, bot_platform, bot_instance,
                    result.failures.size(), first.pipeline, first.stage,
                    first.actor, first.failure.code, first.failure.retryable,
                    first.failure.message);
              }
            } catch (const std::exception &error) {
              OBCX_ERROR("Actor runtime failed to process {} event: {}",
                         ingress_type, error.what());
            }
            co_return;
          };
          events->subscribe_messages(
              [process_actor_event](const core::BotEventContext &context,
                                    const common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await process_actor_event(
                    "message",
                    core::raw_message_envelope_from_event(
                        context.platform, context.installation_id, event));
              });
          events->subscribe_notices(
              [process_actor_event](const core::BotEventContext &context,
                                    const common::NoticeEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await process_actor_event(
                    "notice",
                    core::raw_notice_envelope_from_event(
                        context.platform, context.installation_id, event));
              });
        }

        OBCX_INFO("Starting bot installation: {}", config.installation_id);
        installation->start();
        process_bot_installation_directory->register_installation(
            *installation, plan->recipe().command_publisher);
        process_bot_operation_dispatcher->register_endpoint(
            installation->capability<core::BotOperationEndpoint>(
                core::CapabilityId{"bot.operations"}));

        auto *installation_ptr = installation.get();
        bots.push_back(std::move(installation));
        auto thread_completion = std::make_shared<std::promise<void>>();
        bot_thread_completions.push_back(thread_completion->get_future());
        bot_threads.emplace_back([installation_ptr,
                                  thread_completion]() -> void {
          try {
            installation_ptr->run();
          } catch (const std::exception &error) {
            OBCX_ERROR("Bot installation runtime error: {}", error.what());
          } catch (...) {
            OBCX_ERROR(
                "Bot installation runtime failed with an unknown exception");
          }
          thread_completion->set_value();
        });
      } catch (const std::exception &error) {
        process_bot_installation_directory->unregister_installation(
            bot_instance);
        OBCX_ERROR("Failed to start bot installation {}: {}", bot_instance,
                   error.what());
      }
    }

    process_bot_operation_dispatcher->seal_registrations();
    if (bots.empty()) {
      OBCX_ERROR("No bot components started successfully");
      return 1;
    }

    if (reload_controller) {
      reload_controller->activate_command_catalogs();
    }

    OBCX_INFO("All components started successfully. OBCX Framework running...");

    {
      common::CliHandler::Context ctx{
          .should_stop = g_should_stop,
          .stop_cv = g_stop_cv,
      };

      if (reload_controller) {
        ctx.reload_cb = [reload_controller, config_path, actor_directories,
                         configured_io_sources =
                             std::max<size_t>(1, bot_configs.size())] {
          const auto active = reload_controller->active_generation();
          if (!active) {
            return common::CliHandler::ReloadRequestStatus::Unavailable;
          }
          const auto timeout =
              std::chrono::milliseconds{active->config_snapshot()
                                            ->get_actor_runtime_config()
                                            .reload_drain_timeout_ms};
          const auto status = reload_controller->start_reload(
              {.purpose = core::RuntimeGenerationBuildPurpose::ReloadCandidate,
               .generation_id = 0,
               .config_path = config_path,
               .actor_search_directories = actor_directories,
               .configured_io_sources = configured_io_sources},
              timeout);
          switch (status) {
          case core::RuntimeReloadStartStatus::Accepted:
            return common::CliHandler::ReloadRequestStatus::Accepted;
          case core::RuntimeReloadStartStatus::Busy:
            return common::CliHandler::ReloadRequestStatus::Busy;
          case core::RuntimeReloadStartStatus::Shutdown:
            return common::CliHandler::ReloadRequestStatus::Unavailable;
          }
          return common::CliHandler::ReloadRequestStatus::Unavailable;
        };
      }

      // Shutdown callback runs while the TUI is still alive so the user can
      // see the shutdown log lines in the pane.
      ctx.shutdown_cb = [&]() -> void {
        OBCX_INFO("Shutting down OBCX Framework...");

        if (process_blocking_executor) {
          process_blocking_executor->close_admission();
          const auto metrics = process_blocking_executor->metrics();
          OBCX_INFO(
              "BlockingExecutor lifecycle phase=admission_closed submitted={} "
              "running={} pending={} completed={} failed={} rejected={}",
              metrics.submitted, metrics.running, metrics.pending,
              metrics.completed, metrics.failed, metrics.rejected);
        }
        if (reload_controller) {
          reload_controller->begin_shutdown();
        }
        if (process_blocking_executor) {
          process_blocking_executor->shutdown();
          const auto metrics = process_blocking_executor->metrics();
          OBCX_INFO(
              "BlockingExecutor lifecycle phase=joined submitted={} running={} "
              "pending={} completed={} failed={} rejected={}",
              metrics.submitted, metrics.running, metrics.pending,
              metrics.completed, metrics.failed, metrics.rejected);
        }
        if (reload_controller) {
          reload_controller->shutdown();
        }

        process_bot_operation_dispatcher->clear_endpoints();
        for (auto &bot : bots) {
          bot->stop();
        }

        const auto bot_shutdown_deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(BOT_SHUTDOWN_TIMEOUT_SECONDS);
        bool bot_shutdown_timed_out = false;
        for (size_t i = 0; i < bot_thread_completions.size(); ++i) {
          OBCX_INFO("Waiting for bot thread {} to finish...", i);
          if (bot_thread_completions[i].wait_until(bot_shutdown_deadline) !=
              std::future_status::ready) {
            OBCX_ERROR("Bot thread {} did not finish within {} seconds", i,
                       BOT_SHUTDOWN_TIMEOUT_SECONDS);
            bot_shutdown_timed_out = true;
          }
        }
        if (bot_shutdown_timed_out) {
          OBCX_ERROR("Bot shutdown timed out; terminating to avoid running "
                     "threads outliving their bot instances");
          std::quick_exit(EXIT_FAILURE);
        }
        for (auto &bot_thread : bot_threads) {
          if (bot_thread.joinable()) {
            bot_thread.join();
          }
        }

        process_blocking_executor.reset();

        OBCX_INFO("OBCX Framework shutdown complete");
      };

      if (use_tui) {
        auto tui_sink = common::Logger::get_tui_sink();
        common::TuiApp tui_app(tui_sink, std::move(ctx));
        tui_app.run();
      } else {
        common::CliHandler cli_handler(ctx);
        cli_handler.run();
        // Block on signal in case stdin closed before SIGINT.
        {
          std::unique_lock lock(g_stop_mtx);
          g_stop_cv.wait(lock, []() -> bool { return g_should_stop.load(); });
        }
        if (ctx.shutdown_cb) {
          ctx.shutdown_cb();
        }
      }
    }

    return 0;
  }
};

} // namespace obcx::common

auto main(int argc, char *argv[]) -> int {
  return obcx::common::MainApplication::run(argc, argv);
}
