#include "common/config_loader.hpp"
#include "common/i18n_log_messages.hpp"
#include "common/logger.hpp"
#include "common/plugin_manager.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "interfaces/connection_manager.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <atomic>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <boost/program_options.hpp>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <print>
#include <spdlog/common.h>
#include <string>
#include <thread>
#include <vector>

using namespace obcx;
namespace po = boost::program_options;

namespace {
std::atomic_bool g_should_stop = false;

std::mutex g_stop_mtx;
std::condition_variable g_stop_cv;

const uint16_t DEFAULT_PORT = 8080;
constexpr int BOT_SHUTDOWN_TIMEOUT_SECONDS = 5;

void print_version() {
  std::println("OBCX Robot Framework v1.0.0");
  std::println("A modular bot framework supporting QQ and Telegram");
}

void print_help(const po::options_description &desc) {
  std::println("Usage: OBCX [OPTIONS] [CONFIG_FILE]");
  std::println("");
  std::println("OPTIONS:");
  std::cout << desc << "\n";
  std::println("");
  std::println("CONFIG_FILE:");
  std::println("  Path to TOML configuration file (default: config.toml)");
}

void signal_handler(int signal) {
  bool expected = false;
  if (!g_should_stop.compare_exchange_strong(expected, true)) {
    OBCX_I18N_WARN(common::LogMessageKey::SHUTDOWN_IN_PROGRESS, signal);
    return;
  }
  OBCX_I18N_INFO(common::LogMessageKey::SHUTDOWN_SIGNAL_RECEIVED, signal);
  g_stop_cv.notify_one();
}
} // namespace

class ComponentManager {
public:
  static auto instance() -> ComponentManager & {
    static ComponentManager instance;
    return instance;
  }

  static auto create_bot(const common::BotConfig &config)
      -> std::unique_ptr<core::IBot> {
    if (config.type == "qq") {
      return std::make_unique<core::QQBot>(
          adapter::onebot11::ProtocolAdapter{});
    }
    if (config.type == "telegram") {
      return std::make_unique<core::TGBot>(
          adapter::telegram::ProtocolAdapter{});
    }

    OBCX_I18N_ERROR(common::LogMessageKey::UNKNOWN_BOT_TYPE, config.type);
    return nullptr;
  }

  static auto get_connection_type(const std::string &type,
                                  const std::string &bot_type)
      -> network::ConnectionManagerFactory::ConnectionType {
    if (bot_type == "qq") {
      if (type == "websocket" || type == "ws") {
        return network::ConnectionManagerFactory::ConnectionType::
            Onebot11WebSocket;
      }
      if (type == "http") {
        return network::ConnectionManagerFactory::ConnectionType::Onebot11HTTP;
      }
    } else if (bot_type == "telegram") {
      if (type == "websocket" || type == "ws") {
        return network::ConnectionManagerFactory::ConnectionType::
            TelegramWebsocket;
      }
      if (type == "http") {
        return network::ConnectionManagerFactory::ConnectionType::TelegramHTTP;
      }
    }

    OBCX_I18N_ERROR(common::LogMessageKey::UNKNOWN_CONNECTION_TYPE, type,
                    bot_type);
    return network::ConnectionManagerFactory::ConnectionType::Onebot11HTTP;
  }

  static auto create_connection_config(const toml::table &conn_table)
      -> common::ConnectionConfig {
    common::ConnectionConfig config;

    if (const auto *host = conn_table.get("host")) {
      config.host = host->value_or<std::string>("localhost");
    } else {
      config.host = "localhost";
    }

    if (const auto *port = conn_table.get("port")) {
      config.port = port->value_or(DEFAULT_PORT);
    } else {
      config.port = DEFAULT_PORT;
    }

    if (const auto *token = conn_table.get("access_token")) {
      config.access_token = token->value_or<std::string>("");
    } else {
      config.access_token = "";
    }

    if (const auto *secret = conn_table.get("secret")) {
      config.secret = secret->value_or<std::string>("");
    } else {
      config.secret = "";
    }

    if (const auto *ssl = conn_table.get("use_ssl")) {
      config.use_ssl = ssl->value_or<bool>(false);
    } else {
      config.use_ssl = false;
    }

    if (const auto *timeout = conn_table.get("timeout")) {
      if (auto timeout_ms = timeout->value<int64_t>()) {
        config.timeout = std::chrono::milliseconds(*timeout_ms);
      }
    }

    if (const auto *heartbeat_interval = conn_table.get("heartbeat_interval")) {
      if (auto interval_ms = heartbeat_interval->value<int64_t>()) {
        config.heartbeat_interval = std::chrono::milliseconds(*interval_ms);
      }
    }

    // Proxy configuration
    if (const auto *proxy_host = conn_table.get("proxy_host")) {
      config.proxy_host = proxy_host->value_or<std::string>("");
    } else {
      config.proxy_host = "";
    }

    if (const auto *proxy_port = conn_table.get("proxy_port")) {
      config.proxy_port = proxy_port->value_or<uint16_t>(0);
    } else {
      config.proxy_port = 0;
    }

    if (const auto *proxy_type = conn_table.get("proxy_type")) {
      config.proxy_type = proxy_type->value_or<std::string>("http");
    } else {
      config.proxy_type = "http";
    }

    // Debug logging for proxy configuration
    OBCX_I18N_INFO(common::LogMessageKey::PROXY_CONFIG_INFO, config.proxy_host,
                   config.proxy_port, config.proxy_type);

    if (const auto *proxy_username = conn_table.get("proxy_username")) {
      config.proxy_username = proxy_username->value_or<std::string>("");
    } else {
      config.proxy_username = "";
    }

    if (const auto *proxy_password = conn_table.get("proxy_password")) {
      config.proxy_password = proxy_password->value_or<std::string>("");
    } else {
      config.proxy_password = "";
    }

    return config;
  }

  auto setup_bot(core::IBot &bot, const common::BotConfig &config,
                 common::PluginManager &plugin_manager) -> bool {
    try {
      // Load and initialize plugins for this bot
      for (const auto &plugin_name : config.plugins) {
        if (!plugin_manager.load_plugin(plugin_name)) {
          OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_LOAD_WARN, plugin_name);
          continue;
        }

        if (!plugin_manager.initialize_plugin(plugin_name)) {
          OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_INIT_WARN, plugin_name);
          continue;
        }
      }

      // Setup connection
      auto connection_config = create_connection_config(config.connection);
      std::string conn_type =
          config.connection.get("type")->value_or<std::string>("http");

      bot.connect(get_connection_type(conn_type, config.type),
                  connection_config);

      OBCX_I18N_INFO(common::LogMessageKey::BOT_SETUP_SUCCESS);
      return true;

    } catch (const std::exception &e) {
      OBCX_I18N_ERROR(common::LogMessageKey::BOT_SETUP_FAILED, e.what());
      return false;
    }
  }

private:
  ComponentManager() = default;
};

auto main(int argc, char *argv[]) -> int {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  common::Logger::initialize(
      spdlog::level::info,
      fmt::format("logs/obcx-bridge-{}.log",
                  boost::posix_time::to_iso_extended_string(
                      boost::posix_time::second_clock::local_time())));

  std::string config_path = "config.toml";

  // Parse command line arguments using boost-program-options
  try {
    po::options_description desc("Options");
    desc.add_options()("help,h", "Show this help message")(
        "version,v", "Show version information")(
        "config", po::value<std::string>(), "Path to TOML configuration file");

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

  } catch (const po::error &e) {
    std::println(std::cerr, "Error parsing arguments: {}", e.what());
    return 1;
  }

  // Initialize configuration
  auto &config_loader = common::ConfigLoader::instance();
  if (!config_loader.load_config(config_path)) {
    std::println(std::cerr, "Failed to load configuration from: {}",
                 config_path);
    return 1;
  }

  // Set log locale from configuration (default: en_US)
  auto locale = config_loader.get_value<std::string>("global.locale");
  if (locale.has_value()) {
    common::I18nLogMessages::set_locale(*locale);
    OBCX_I18N_INFO(common::LogMessageKey::LOG_LOCALE_SET, *locale);
  }
  // If not specified, keep default en_US (no need to explicitly set)

  OBCX_I18N_INFO(common::LogMessageKey::FRAMEWORK_STARTING);
  OBCX_I18N_INFO(common::LogMessageKey::CONFIG_LOADED_FROM, config_path);

  // Initialize plugin manager
  common::PluginManager plugin_manager;
  auto &component_manager = ComponentManager::instance();

  // Add plugin directories
  if (auto plugin_dirs = config_loader.get_section("plugin_directories")) {
    for (const auto &[key, value] : *plugin_dirs) {
      if (auto dir = value.value<std::string>()) {
        plugin_manager.add_plugin_directory(*dir);
      }
    }
  } else {
    // Default plugin directories
    plugin_manager.add_plugin_directory("./plugins");
    plugin_manager.add_plugin_directory("./build/plugins");
    plugin_manager.add_plugin_directory("/usr/local/lib/obcx/plugins");
  }

  // Load bot configurations
  auto bot_configs = config_loader.get_bot_configs();
  if (bot_configs.empty()) {
    OBCX_I18N_ERROR(common::LogMessageKey::NO_BOT_CONFIGS);
    return 1;
  }

  // Create and setup bot components
  std::vector<std::unique_ptr<core::IBot>> bots;
  std::vector<std::thread> bot_threads;
  std::mutex bots_mutex;

  interface::IPlugin::set_bots(&bots, &bots_mutex);

  for (const auto &config : bot_configs) {
    if (!config.enabled) {
      OBCX_I18N_INFO(common::LogMessageKey::SKIPPING_DISABLED_BOT, config.type);
      continue;
    }

    auto bot = ComponentManager::create_bot(config);
    if (!bot) {
      OBCX_I18N_ERROR(common::LogMessageKey::BOT_CREATE_FAILED, config.type);
      continue;
    }

    // Move bot to bots vector first so plugins can access it during
    // initialization
    bots.push_back(std::move(bot));
    size_t bot_index = bots.size() - 1;

    if (!component_manager.setup_bot(*bots[bot_index], config,
                                     plugin_manager)) {
      OBCX_I18N_ERROR(common::LogMessageKey::BOT_SETUP_FAILED_TYPE,
                      config.type);
      // Remove the bot from vector since setup failed
      bots.pop_back();
      continue;
    }

    OBCX_I18N_INFO(common::LogMessageKey::STARTING_BOT, config.type);

    // Start bot component in separate thread, capturing the specific bot index
    bot_threads.emplace_back([&bots, bot_index]() {
      try {
        bots[bot_index]->run();
      } catch (const std::exception &e) {
        OBCX_I18N_ERROR(common::LogMessageKey::BOT_RUNTIME_ERROR, e.what());
      }
    });
  }

  if (bots.empty()) {
    OBCX_I18N_ERROR(common::LogMessageKey::NO_BOTS_STARTED);
    return 1;
  }

  OBCX_I18N_INFO(common::LogMessageKey::ALL_COMPONENTS_STARTED);

  // Start CLI input thread
  std::thread cli_thread([]() {
    std::string line;
    while (!g_should_stop.load(std::memory_order_acquire) &&
           std::getline(std::cin, line)) {
      if (line == "\x03" || line == "exit" || line == "quit") {
        bool expected = false;
        if (g_should_stop.compare_exchange_strong(expected, true)) {
          OBCX_I18N_INFO(common::LogMessageKey::SHUTDOWN_SIGNAL_RECEIVED, 0);
          g_stop_cv.notify_one();
        }
        break;
      }
      std::println(std::cout, "Input: {}", line);
      // Placeholder for future command processing
    }
  });
  cli_thread.detach();

  // Wait for shutdown signal
  {
    std::unique_lock lock(g_stop_mtx);
    g_stop_cv.wait(
        lock, []() { return g_should_stop.load(std::memory_order_acquire); });
  }

  OBCX_I18N_INFO(common::LogMessageKey::FRAMEWORK_SHUTDOWN);

  // Stop all bot components
  for (auto &bot : bots) {
    bot->stop();
  }

  // Wait for bot threads to finish with timeout
  for (size_t i = 0; i < bot_threads.size(); ++i) {
    if (bot_threads[i].joinable()) {
      OBCX_I18N_INFO(common::LogMessageKey::WAITING_BOT_THREAD, i);
      // Use a detached thread to implement timeout
      bool thread_finished = false;
      std::thread timeout_thread([&]() {
        std::this_thread::sleep_for(
            std::chrono::seconds(BOT_SHUTDOWN_TIMEOUT_SECONDS));
        if (!thread_finished) {
          OBCX_I18N_WARN(common::LogMessageKey::BOT_THREAD_TIMEOUT, i);
          bot_threads[i].detach();
        }
      });

      bot_threads[i].join();
      thread_finished = true;

      if (timeout_thread.joinable()) {
        timeout_thread.join();
      }
    }
  }

  // Shutdown all plugins
  plugin_manager.shutdown_all_plugins();

  OBCX_I18N_INFO(common::LogMessageKey::FRAMEWORK_SHUTDOWN_COMPLETE);
  return 0;
}
