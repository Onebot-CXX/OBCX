#include "config.hpp"

#include <common/logger.hpp>

namespace mc_rcon {

// Global plugin configuration
PluginConfig PLUGIN_CONFIG;

auto load_config(const toml::table &config) -> bool {
  try {
    PLUGIN_CONFIG = PluginConfig{}; // Reset to defaults

    // Load allowed_groups
    if (config.contains("allowed_groups")) {
      const auto &groups = config["allowed_groups"];
      if (groups.is_array()) {
        for (const auto &item : *groups.as_array()) {
          if (item.is_string()) {
            PLUGIN_CONFIG.allowed_groups.insert(item.value_or<std::string>(""));
          }
        }
      }
    }
    PLUGIN_INFO("mc_rcon", "Loaded {} allowed groups",
                PLUGIN_CONFIG.allowed_groups.size());

    // Load admin_users
    if (config.contains("admin_users")) {
      const auto &admins = config["admin_users"];
      if (admins.is_array()) {
        for (const auto &item : *admins.as_array()) {
          if (item.is_string()) {
            PLUGIN_CONFIG.admin_users.insert(item.value_or<std::string>(""));
          }
        }
      }
    }
    PLUGIN_INFO("mc_rcon", "Loaded {} admin users",
                PLUGIN_CONFIG.admin_users.size());

    // Load whitelist_commands
    if (config.contains("whitelist_commands")) {
      const auto &commands = config["whitelist_commands"];
      if (commands.is_array()) {
        for (const auto &item : *commands.as_array()) {
          if (item.is_string()) {
            PLUGIN_CONFIG.whitelist_commands.insert(
                item.value_or<std::string>(""));
          }
        }
      }
    }
    PLUGIN_INFO("mc_rcon", "Loaded {} whitelisted commands",
                PLUGIN_CONFIG.whitelist_commands.size());

    // Load default_server
    PLUGIN_CONFIG.default_server =
        config["default_server"].value_or<std::string>("");
    PLUGIN_INFO("mc_rcon", "Default server: {}", PLUGIN_CONFIG.default_server);

    // Load connection_timeout_ms
    PLUGIN_CONFIG.connection_timeout_ms =
        config["connection_timeout_ms"].value_or(5000);

    // Load servers
    if (config.contains("servers")) {
      const auto &servers = config["servers"];
      if (servers.is_array()) {
        for (const auto &item : *servers.as_array()) {
          if (!item.is_table()) {
            continue;
          }

          const auto &server_table = *item.as_table();

          ServerConfig server;
          server.name = server_table["name"].value_or<std::string>("");
          server.host = server_table["host"].value_or<std::string>("127.0.0.1");
          server.port = server_table["port"].value_or<uint16_t>(25575);
          server.password = server_table["password"].value_or<std::string>("");

          if (!server.name.empty()) {
            PLUGIN_CONFIG.servers.push_back(server);
            PLUGIN_INFO("mc_rcon", "Loaded server: {} ({}:{})", server.name,
                        server.host, server.port);
          }
        }
      }
    }
    PLUGIN_INFO("mc_rcon", "Loaded {} servers", PLUGIN_CONFIG.servers.size());

    // Validate configuration
    if (PLUGIN_CONFIG.servers.empty()) {
      PLUGIN_WARN("mc_rcon", "No servers configured");
      return false;
    }

    if (PLUGIN_CONFIG.default_server.empty() &&
        !PLUGIN_CONFIG.servers.empty()) {
      PLUGIN_CONFIG.default_server = PLUGIN_CONFIG.servers[0].name;
      PLUGIN_INFO("mc_rcon", "Using first server as default: {}",
                  PLUGIN_CONFIG.default_server);
    }

    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR("mc_rcon", "Failed to load config: {}", e.what());
    return false;
  }
}

} // namespace mc_rcon
