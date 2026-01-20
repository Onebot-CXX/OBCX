#include "common/cli_handler.hpp"
#include "common/i18n_log_messages.hpp"
#include "common/logger.hpp"

#include <iostream>
#include <print>
#include <spdlog/common.h>
#include <string_view>

namespace obcx::common {

CliHandler::CliHandler(Context ctx) : ctx_(ctx) { register_default_handlers(); }

void CliHandler::register_default_handlers() {
  handlers_["exit"] = handle_exit;
  handlers_["quit"] = handle_exit;
  handlers_["\x03"] = handle_exit; // Ctrl+C
  handlers_["reload"] = handle_reload;
}

auto CliHandler::process_command(const std::string &line) -> bool {
  if (line.empty()) {
    return true;
  }

  // Check for log_level=xxx pattern
  constexpr std::string_view loglevel_prefix = "log_level=";
  if (line.starts_with(loglevel_prefix)) {
    std::string level_str =
        line.substr(loglevel_prefix.length()); // length of "log_level="
    return handle_log_level(ctx_, level_str);
  }

  // Check for exact command match
  auto it = handlers_.find(line);
  if (it != handlers_.end()) {
    return it->second(ctx_, "");
  }

  // Unknown command - just ignore
  return true;
}

void CliHandler::run() {
  std::string line;
  while (!ctx_.should_stop.load(std::memory_order_acquire) &&
         std::getline(std::cin, line)) {
    if (!process_command(line)) {
      break;
    }
  }
}

auto CliHandler::handle_exit(Context &ctx,
                             [[maybe_unused]] const std::string &args) -> bool {
  bool expected = false;
  if (ctx.should_stop.compare_exchange_strong(expected, true)) {
    OBCX_I18N_INFO(common::LogMessageKey::SHUTDOWN_SIGNAL_RECEIVED, 0);
    ctx.stop_cv.notify_one();
  }
  return false; // Stop the CLI loop
}

auto CliHandler::handle_reload(Context &ctx,
                               [[maybe_unused]] const std::string &args)
    -> bool {
  OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_RELOAD_START);

  // Step 1: Clear all event handlers from bots to prevent dangling
  // function pointers after plugin unload
  {
    std::lock_guard lock(ctx.bots_mutex);
    for (auto &bot : ctx.bots) {
      bot->clear_event_handlers();
    }
  }

  // Step 2: Shutdown and unload all plugins
  ctx.plugin_manager.shutdown_all_plugins();
  ctx.plugin_manager.unload_all_plugins();

  // Step 3: Reload configuration
  ctx.config_loader.reload_config();

  // Step 4: Update bot_configs from reloaded config
  ctx.bot_configs = ctx.config_loader.get_bot_configs();

  // Step 5: Load and initialize plugins based on new bot configs
  for (const auto &config : ctx.bot_configs) {
    if (!config.enabled) {
      continue;
    }
    for (const auto &plugin_name : config.plugins) {
      if (!ctx.plugin_manager.load_plugin(plugin_name)) {
        OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_LOAD_WARN, plugin_name);
        continue;
      }
      if (!ctx.plugin_manager.initialize_plugin(plugin_name)) {
        OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_INIT_WARN, plugin_name);
        continue;
      }
    }
  }

  OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_RELOAD_COMPLETE);
  return true; // Continue the CLI loop
}

auto CliHandler::handle_log_level([[maybe_unused]] Context &ctx,
                                  const std::string &level_str) -> bool {
  auto level = Logger::parse_level(level_str);
  if (level.has_value()) {
    Logger::set_level(*level);
    std::println("Log level changed to: {}", level_str);
  } else {
    OBCX_WARN("Invalid log level: {}. Valid levels: trace, debug, info, warn, "
              "error, critical, off",
              level_str);
  }
  return true; // Continue the CLI loop
}

} // namespace obcx::common
