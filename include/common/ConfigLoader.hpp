#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <toml++/toml.hpp>
#include <unordered_map>
#include <vector>

namespace obcx::common {

struct BotConfig {
  std::string type;
  bool enabled;
  toml::table connection;
  std::vector<std::string> plugins;
};

struct PluginConfig {
  std::string name;
  bool enabled;
  toml::table config;
  std::vector<std::string> callbacks;
};

class ConfigLoader {
  ConfigLoader() = default;
  mutable std::mutex mutex_;
  std::unique_ptr<toml::table> config_data_;
  std::string config_path_;

public:
  static ConfigLoader &instance() {
    static ConfigLoader instance;
    return instance;
  }

  ConfigLoader(const ConfigLoader &) = delete;
  ConfigLoader &operator=(const ConfigLoader &) = delete;
  ConfigLoader(ConfigLoader &&) = delete;
  ConfigLoader &operator=(ConfigLoader &&) = delete;

  bool load_config(const std::string &config_path);

  std::vector<BotConfig> get_bot_configs() const;

  std::optional<PluginConfig> get_plugin_config(
      const std::string &plugin_name) const;

  std::vector<PluginConfig> get_all_plugin_configs() const;

  template <typename T>
  std::optional<T> get_value(const std::string &key) const {
    std::lock_guard lock(mutex_);
    if (!config_data_) {
      return std::nullopt;
    }

    auto node = config_data_->at_path(key);
    if (!node) {
      return std::nullopt;
    }

    if constexpr (std::is_same_v<T, std::string>) {
      if (auto val = node.value<std::string>()) {
        return *val;
      }
    } else if constexpr (std::is_same_v<T, int64_t>) {
      if (auto val = node.value<int64_t>()) {
        return *val;
      }
    } else if constexpr (std::is_same_v<T, double>) {
      if (auto val = node.value<double>()) {
        return *val;
      }
    } else if constexpr (std::is_same_v<T, bool>) {
      if (auto val = node.value<bool>()) {
        return *val;
      }
    }

    return std::nullopt;
  }

  std::optional<toml::table> get_section(const std::string &section_name) const;

  void reload_config();

  bool is_loaded() const {
    std::lock_guard lock(mutex_);
    return config_data_ != nullptr;
  }

  const std::string &get_config_path() const {
    std::lock_guard lock(mutex_);
    return config_path_;
  }
};

} // namespace obcx::common