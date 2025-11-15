#include "common/config_loader.hpp"
#include "common/logger.hpp"
#include "common/plugin_manager.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "interfaces/connection_manager.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include <boost/date_time/posix_time/time_formatters.hpp>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <spdlog/common.h>
#include <thread>
#include <vector>

using namespace obcx;

namespace {
volatile sig_atomic_t g_should_stop = 0;
volatile sig_atomic_t g_shutdown_started = 0;

const uint16_t DEFAULT_PORT = 8080;

void print_version() {
  std::cout << "OBCX Robot Framework v1.0.0" << '\n';
  std::cout << "A modular bot framework supporting QQ and Telegram" << '\n';
}

void print_help() {
  std::cout << "Usage: OBCX [OPTIONS] [CONFIG_FILE]" << '\n';
  std::cout << '\n';
  std::cout << "OPTIONS:" << '\n';
  std::cout << "  -h, --help     Show this help message" << '\n';
  std::cout << "  -v, --version  Show version information" << '\n';
  std::cout << '\n';
  std::cout << "CONFIG_FILE:" << '\n';
  std::cout << "  Path to TOML configuration file (default: config.toml)"
            << '\n';
}

void signal_handler(int signal) {
  // Use atomic flag to prevent multiple shutdown attempts
  if (g_shutdown_started == 1) {
    OBCX_WARN("Shutdown already in progress, ignoring signal {}", signal);
    return;
  }

  g_shutdown_started = 1;
  OBCX_INFO("Received signal {}, shutting down gracefully...", signal);
  g_should_stop = 1;
}
} // namespace

class ComponentManager {
public:
  static ComponentManager &instance() {
    static ComponentManager instance;
    return instance;
  }

  static std::unique_ptr<core::IBot> create_bot(
      const common::BotConfig &config) {
    if (config.type == "qq") {
      return std::make_unique<core::QQBot>(
          adapter::onebot11::ProtocolAdapter{});
    }
    if (config.type == "telegram") {
      return std::make_unique<core::TGBot>(
          adapter::telegram::ProtocolAdapter{});
    }

    OBCX_ERROR("Unknown bot type: {}", config.type);
    return nullptr;
  }

  static network::ConnectionManagerFactory::ConnectionType get_connection_type(
      const std::string &type, const std::string &bot_type) {
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

    OBCX_ERROR("Unknown connection type: {} for bot type: {}", type, bot_type);
    return network::ConnectionManagerFactory::ConnectionType::Onebot11HTTP;
  }

  static common::ConnectionConfig create_connection_config(
      const toml::table &conn_table) {
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
    OBCX_INFO("Proxy config - Host: '{}', Port: {}, Type: '{}'",
              config.proxy_host, config.proxy_port, config.proxy_type);

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

  bool setup_bot(core::IBot &bot, const common::BotConfig &config,
                 common::PluginManager &plugin_manager) {
    try {
      // Load and initialize plugins for this bot
      for (const auto &plugin_name : config.plugins) {
        if (!plugin_manager.load_plugin(plugin_name)) {
          OBCX_WARN("Failed to load plugin: {}", plugin_name);
          continue;
        }

        if (!plugin_manager.initialize_plugin(plugin_name)) {
          OBCX_WARN("Failed to initialize plugin: {}", plugin_name);
          continue;
        }
      }

      // Setup connection
      auto connection_config = create_connection_config(config.connection);
      std::string conn_type =
          config.connection.get("type")->value_or<std::string>("http");

      bot.connect(get_connection_type(conn_type, config.type),
                  connection_config);

      OBCX_INFO("Bot component setup completed successfully");
      return true;

    } catch (const std::exception &e) {
      OBCX_ERROR("Failed to setup bot component: {}", e.what());
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

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_help();
      return 0;
    }
    if (arg == "-v" || arg == "--version") {
      print_version();
      return 0;
    }
    if (arg.starts_with("-")) {
      std::cerr << "Unknown option: " << arg << '\n';
      print_help();
      return 1;
    }
    config_path = arg;
  }

  // Initialize configuration
  auto &config_loader = common::ConfigLoader::instance();
  if (!config_loader.load_config(config_path)) {
    std::cerr << "Failed to load configuration from: " << config_path << '\n';
    return 1;
  }

  OBCX_INFO("OBCX Robot Framework starting...");
  OBCX_INFO("Configuration loaded from: {}", config_path);

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
    OBCX_ERROR("No bot configurations found");
    return 1;
  }

  // Create and setup bot components
  std::vector<std::unique_ptr<core::IBot>> bots;
  std::vector<std::thread> bot_threads;
  std::mutex bots_mutex; // 互斥锁保护bot vector

  interface::IPlugin::set_bots(&bots, &bots_mutex);

  for (const auto &config : bot_configs) {
    if (!config.enabled) {
      OBCX_INFO("Skipping disabled bot component of type: {}", config.type);
      continue;
    }

    auto bot = ComponentManager::create_bot(config);
    if (!bot) {
      OBCX_ERROR("Failed to create bot component of type: {}", config.type);
      continue;
    }

    // Move bot to bots vector first so plugins can access it during
    // initialization
    bots.push_back(std::move(bot));
    size_t bot_index = bots.size() - 1;

    if (!component_manager.setup_bot(*bots[bot_index], config,
                                     plugin_manager)) {
      OBCX_ERROR("Failed to setup bot component of type: {}", config.type);
      // Remove the bot from vector since setup failed
      bots.pop_back();
      continue;
    }

    OBCX_INFO("Starting bot component of type: {}", config.type);

    // Start bot component in separate thread, capturing the specific bot index
    bot_threads.emplace_back([&bots, bot_index]() {
      try {
        bots[bot_index]->run();
      } catch (const std::exception &e) {
        OBCX_ERROR("Bot component runtime error: {}", e.what());
      }
    });
  }

  if (bots.empty()) {
    OBCX_ERROR("No bot components started successfully");
    return 1;
  }

  OBCX_INFO("All components started successfully. OBCX Framework running...");

  // Wait for shutdown signal
  while (g_should_stop == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  OBCX_INFO("Shutting down OBCX Framework...");

  // Stop all bot components
  for (auto &bot : bots) {
    bot->stop();
  }

  // Wait for bot threads to finish with timeout
  for (size_t i = 0; i < bot_threads.size(); ++i) {
    if (bot_threads[i].joinable()) {
      OBCX_INFO("Waiting for bot thread {} to finish...", i);
      // Use a detached thread to implement timeout
      bool thread_finished = false;
      std::thread timeout_thread([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!thread_finished) {
          OBCX_WARN("Bot thread {} did not finish within timeout, detaching",
                    i);
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

  OBCX_INFO("OBCX Framework shutdown complete");
  return 0;
}
