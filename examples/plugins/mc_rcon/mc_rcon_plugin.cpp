#include "mc_rcon_plugin.hpp"
#include "common/logger.hpp"
#include "config.hpp"
#include "core/qq_bot.hpp"
#include "rcon_client.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <sstream>

namespace mc_rcon {

McRconPlugin::McRconPlugin() {
  PLUGIN_DEBUG(get_name(), "McRconPlugin constructor called");
}

McRconPlugin::~McRconPlugin() {
  shutdown();
  PLUGIN_DEBUG(get_name(), "McRconPlugin destructor called");
}

auto McRconPlugin::get_name() const -> std::string { return "mc_rcon"; }

auto McRconPlugin::get_version() const -> std::string { return "1.0.0"; }

auto McRconPlugin::get_description() const -> std::string {
  return "Minecraft RCON command execution plugin for QQ";
}

auto McRconPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing MC RCON Plugin...");

    // Load configuration
    if (!load_configuration()) {
      PLUGIN_ERROR(get_name(), "Failed to load plugin configuration");
      return false;
    }

    // Register event callbacks
    try {
      auto [lock, bots] = get_bots();

      for (auto &bot_ptr : bots) {
        if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
          qq_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_qq_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered QQ message callback");
          break;
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "MC RCON Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during initialization: {}", e.what());
    return false;
  }
}

void McRconPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing MC RCON Plugin...");
    PLUGIN_INFO(get_name(), "MC RCON Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during deinitialization: {}", e.what());
  }
}

void McRconPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down MC RCON Plugin...");
    PLUGIN_INFO(get_name(), "MC RCON Plugin shutdown complete");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during shutdown: {}", e.what());
  }
}

auto McRconPlugin::load_configuration() -> bool {
  try {
    // Use the config system
    std::string config_path =
        get_config_value<std::string>("config_file").value_or("");
    if (!load_config(config_path)) {
      PLUGIN_WARN(get_name(), "Config file not specified or load failed, "
                              "using default empty config");
    }

    PLUGIN_INFO(get_name(),
                "Configuration loaded: {} servers, {} allowed "
                "groups, {} admin users",
                PLUGIN_CONFIG.servers.size(),
                PLUGIN_CONFIG.allowed_groups.size(),
                PLUGIN_CONFIG.admin_users.size());
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load configuration: {}", e.what());
    return false;
  }
}

auto McRconPlugin::handle_qq_message(obcx::core::IBot &bot,
                                     const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  const std::string &group_id = event.group_id.value();
  const std::string &user_id = event.user_id;
  const std::string &raw_message = event.raw_message;

  // Check if message starts with /mc
  if (!raw_message.starts_with("/mc")) {
    co_return;
  }

  PLUGIN_DEBUG(get_name(), "Received /mc command from user {} in group {}",
               user_id, group_id);

  // Check if group is allowed
  if (!is_allowed_group(group_id)) {
    PLUGIN_DEBUG(get_name(), "Group {} is not allowed to use mc_rcon",
                 group_id);
    co_return;
  }

  // Parse the command
  auto [server_name, command] = parse_mc_command(raw_message);

  if (command.empty()) {
    co_await send_response(bot, group_id,
                           "Usage: /mc [server] <command>\n"
                           "Example: /mc list\n"
                           "Example: /mc survival list");
    co_return;
  }

  // Get the first word of command for whitelist check
  std::string first_word = command;
  size_t space_pos = command.find(' ');
  if (space_pos != std::string::npos) {
    first_word = command.substr(0, space_pos);
  }

  // Check permissions
  bool is_user_admin = is_admin(user_id);
  if (!is_user_admin && !is_whitelisted_command(first_word)) {
    PLUGIN_WARN(get_name(),
                "User {} attempted to execute non-whitelisted command: {}",
                user_id, first_word);
    co_await send_response(bot, group_id,
                           "Permission denied: command '" + first_word +
                               "' is not whitelisted");
    co_return;
  }

  // Check if server exists
  const ServerConfig *server = get_server_config(server_name);
  if (!server) {
    co_await send_response(bot, group_id, "Unknown server: " + server_name);
    co_return;
  }

  PLUGIN_INFO(get_name(), "Executing command '{}' on server '{}' for user {}",
              command, server_name, user_id);

  // Execute the command
  std::string response = execute_command(server_name, command);

  // Send response
  co_await send_response(bot, group_id, response);
}

std::pair<std::string, std::string> McRconPlugin::parse_mc_command(
    const std::string &raw_message) {
  // Expected format: /mc [server] <command>
  // If first word after /mc matches a server name, use that server
  // Otherwise use default server

  std::string message = raw_message;

  // Remove "/mc" prefix
  if (message.starts_with("/mc")) {
    message = message.substr(3);
  }

  // Trim leading spaces
  size_t start = message.find_first_not_of(' ');
  if (start == std::string::npos) {
    return {PLUGIN_CONFIG.default_server, ""};
  }
  message = message.substr(start);

  if (message.empty()) {
    return {PLUGIN_CONFIG.default_server, ""};
  }

  // Split into words
  std::istringstream iss(message);
  std::string first_word;
  iss >> first_word;

  // Check if first word is a server name
  if (get_server_config(first_word) != nullptr) {
    // First word is a server name, rest is the command
    std::string rest;
    std::getline(iss >> std::ws, rest);
    return {first_word, rest};
  }

  // First word is not a server name, use default server
  return {PLUGIN_CONFIG.default_server, message};
}

std::string McRconPlugin::execute_command(const std::string &server_name,
                                          const std::string &command) {
  const ServerConfig *server = get_server_config(server_name);
  if (!server) {
    return "Error: Unknown server '" + server_name + "'";
  }

  auto result = execute_rcon_command(server->host, server->port,
                                     server->password, command);

  if (!result.has_value()) {
    return "Error: Failed to execute command on server '" + server_name + "'";
  }

  std::string response = result.value();

  // Trim trailing whitespace
  while (!response.empty() &&
         (response.back() == '\n' || response.back() == '\r' ||
          response.back() == ' ')) {
    response.pop_back();
  }

  if (response.empty()) {
    return "[" + server_name + "] Command executed (no output)";
  }

  return "[" + server_name + "] " + response;
}

auto McRconPlugin::send_response(obcx::core::IBot &bot,
                                 const std::string &group_id,
                                 const std::string &response)
    -> boost::asio::awaitable<void> {
  try {
    obcx::common::Message message;
    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = response;
    message.push_back(text_segment);

    co_await bot.send_group_message(group_id, message);
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send response: {}", e.what());
  }
}

} // namespace mc_rcon

// Export the plugin
OBCX_PLUGIN_EXPORT(mc_rcon::McRconPlugin)
