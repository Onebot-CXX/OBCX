#include "common/config_loader.hpp"
#include "common/logger.hpp"
#include <fstream>

namespace obcx::common {

bool ConfigLoader::load_config(const std::string &config_path) {
  std::lock_guard lock(mutex_);

  try {
    config_path_ = config_path;
    config_data_ = std::make_unique<toml::table>(toml::parse_file(config_path));
    OBCX_INFO("Config loaded successfully from: {}", config_path);
    return true;
  } catch (const toml::parse_error &e) {
    OBCX_INFO("Failed to parse config file {}: {}", config_path, e.what());
    config_data_.reset();
    return false;
  } catch (const std::exception &e) {
    OBCX_INFO("Failed to load config file {}: {}", config_path, e.what());
    config_data_.reset();
    return false;
  }
}

std::vector<BotConfig> ConfigLoader::get_bot_configs() const {
  std::lock_guard lock(mutex_);
  std::vector<BotConfig> bot_configs;

  if (!config_data_) {
    return bot_configs;
  }

  if (auto bots_section = config_data_->get("bots")) {
    if (auto bots_table = bots_section->as_table()) {
      for (const auto &[bot_name, bot_config] : *bots_table) {
        if (auto bot_table = bot_config.as_table()) {
          BotConfig config;
          config.type = bot_table->get("type")->value_or<std::string>("");
          config.enabled = bot_table->get("enabled")->value_or<bool>(false);

          if (auto conn_section = bot_table->get("connection")) {
            if (auto conn_table = conn_section->as_table()) {
              config.connection = *conn_table;
            }
          }

          if (auto plugins_section = bot_table->get("plugins")) {
            if (auto plugins_array = plugins_section->as_array()) {
              for (const auto &plugin : *plugins_array) {
                if (auto plugin_str = plugin.value<std::string>()) {
                  config.plugins.push_back(*plugin_str);
                }
              }
            }
          }

          bot_configs.push_back(std::move(config));
        }
      }
    }
  }

  return bot_configs;
}

std::optional<PluginConfig> ConfigLoader::get_plugin_config(
    const std::string &plugin_name) const {
  std::lock_guard lock(mutex_);

  if (!config_data_) {
    return std::nullopt;
  }

  std::string plugin_path = "plugins." + plugin_name;
  if (auto plugin_section = config_data_->at_path(plugin_path)) {
    if (auto plugin_table = plugin_section.as_table()) {
      PluginConfig config;
      config.name = plugin_name;
      config.enabled = plugin_table->get("enabled")->value_or<bool>(false);

      if (auto config_section = plugin_table->get("config")) {
        if (auto config_table = config_section->as_table()) {
          config.config = *config_table;
        }
      }

      if (auto callbacks_section = plugin_table->get("callbacks")) {
        if (auto callbacks_array = callbacks_section->as_array()) {
          for (const auto &callback : *callbacks_array) {
            if (auto callback_str = callback.value<std::string>()) {
              config.callbacks.push_back(*callback_str);
            }
          }
        }
      }

      return config;
    }
  }

  return std::nullopt;
}

std::vector<PluginConfig> ConfigLoader::get_all_plugin_configs() const {
  std::lock_guard lock(mutex_);
  std::vector<PluginConfig> plugin_configs;

  if (!config_data_) {
    return plugin_configs;
  }

  if (auto plugins_section = config_data_->get("plugins")) {
    if (auto plugins_table = plugins_section->as_table()) {
      for (const auto &[plugin_name, plugin_config] : *plugins_table) {
        if (auto plugin_table = plugin_config.as_table()) {
          PluginConfig config;
          config.name = std::string{plugin_name};
          config.enabled = plugin_table->get("enabled")->value_or<bool>(false);

          if (auto config_section = plugin_table->get("config")) {
            if (auto config_table = config_section->as_table()) {
              config.config = *config_table;
            }
          }

          if (auto callbacks_section = plugin_table->get("callbacks")) {
            if (auto callbacks_array = callbacks_section->as_array()) {
              for (const auto &callback : *callbacks_array) {
                if (auto callback_str = callback.value<std::string>()) {
                  config.callbacks.push_back(*callback_str);
                }
              }
            }
          }

          plugin_configs.push_back(std::move(config));
        }
      }
    }
  }

  return plugin_configs;
}

std::optional<toml::table> ConfigLoader::get_section(
    const std::string &section_name) const {
  std::lock_guard lock(mutex_);

  if (!config_data_) {
    return std::nullopt;
  }

  if (auto section = config_data_->get(section_name)) {
    if (auto section_table = section->as_table()) {
      return *section_table;
    }
  }

  return std::nullopt;
}

void ConfigLoader::reload_config() {
  if (!config_path_.empty()) {
    load_config(config_path_);
  }
}

} // namespace obcx::common