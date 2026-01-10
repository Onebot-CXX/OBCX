#pragma once

#include <cstdint>
#include <string>
#include <toml++/toml.hpp>
#include <unordered_set>
#include <vector>

namespace mc_rcon {

/**
 * @brief Minecraft RCON server configuration
 */
struct ServerConfig {
  std::string name;     // Server identifier (e.g., "survival")
  std::string host;     // RCON host address
  uint16_t port;        // RCON port
  std::string password; // RCON password
};

/**
 * @brief MC RCON Plugin configuration
 */
struct PluginConfig {
  std::unordered_set<std::string>
      allowed_groups; // QQ groups allowed to use commands
  std::unordered_set<std::string>
      admin_users; // Admin QQ user IDs (can run any command)
  std::unordered_set<std::string>
      whitelist_commands;            // Commands regular users can use
  std::vector<ServerConfig> servers; // Server configurations
  std::string default_server;        // Default server if not specified
  int connection_timeout_ms = 5000;  // Connection timeout in milliseconds
};

// Global plugin configuration
extern PluginConfig PLUGIN_CONFIG;

/**
 * @brief Load plugin configuration from TOML table
 * @param config The TOML table from [plugins.mc_rcon.config]
 * @return true if configuration loaded successfully
 */
bool load_config(const toml::table &config);

/**
 * @brief Check if a group is allowed to use the plugin
 * @param group_id QQ group ID
 * @return true if the group is allowed
 */
inline bool is_allowed_group(const std::string &group_id) {
  return PLUGIN_CONFIG.allowed_groups.contains(group_id);
}

/**
 * @brief Check if a user is an admin
 * @param user_id QQ user ID
 * @return true if the user is an admin
 */
inline bool is_admin(const std::string &user_id) {
  return PLUGIN_CONFIG.admin_users.contains(user_id);
}

/**
 * @brief Check if a command is whitelisted for regular users
 * @param command The command to check (first word only)
 * @return true if the command is whitelisted
 */
inline bool is_whitelisted_command(const std::string &command) {
  return PLUGIN_CONFIG.whitelist_commands.contains(command);
}

/**
 * @brief Get server configuration by name
 * @param name Server name
 * @return Pointer to server config or nullptr if not found
 */
inline const ServerConfig *get_server_config(const std::string &name) {
  for (const auto &server : PLUGIN_CONFIG.servers) {
    if (server.name == name) {
      return &server;
    }
  }
  return nullptr;
}

} // namespace mc_rcon
