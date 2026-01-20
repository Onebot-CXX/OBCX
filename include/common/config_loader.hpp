#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <toml++/toml.hpp>
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
  uint8_t priority = 0; // 0 = lowest, 255 = highest
  std::vector<std::string>
      required; // plugins that must be loaded before this one
};

class ConfigLoader {
  ConfigLoader() = default;
  ~ConfigLoader() = default;
  mutable std::mutex mutex_;
  std::unique_ptr<toml::table> config_data_;
  std::string config_path_;

public:
  static auto instance() -> ConfigLoader & {
    static ConfigLoader instance;
    return instance;
  }

  ConfigLoader(const ConfigLoader &) = delete;
  auto operator=(const ConfigLoader &) -> ConfigLoader & = delete;
  ConfigLoader(ConfigLoader &&) = delete;
  auto operator=(ConfigLoader &&) -> ConfigLoader & = delete;

  auto load_config(const std::string &config_path) -> bool;

  auto get_bot_configs() const -> std::vector<BotConfig>;

  auto get_plugin_config(const std::string &plugin_name) const
      -> std::optional<PluginConfig>;

  auto get_all_plugin_configs() const -> std::vector<PluginConfig>;

  template <typename T>
  auto get_value(const std::string &key) const -> std::optional<T> {
    std::lock_guard lock(mutex_);
    if (!config_data_) {
      return std::nullopt;
    }

    const auto node = config_data_->at_path(key);
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

  auto get_section(const std::string &section_name) const
      -> std::optional<toml::table>;

  void reload_config();

  auto is_loaded() const -> bool {
    std::lock_guard lock(mutex_);
    return config_data_ != nullptr;
  }

  auto get_config_path() const -> const std::string & {
    std::lock_guard lock(mutex_);
    return config_path_;
  }
};

} // namespace obcx::common
