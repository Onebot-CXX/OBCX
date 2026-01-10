#pragma once

#include "common/config_loader.hpp"
#include "interfaces/bot.hpp"

#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace obcx::interface {

enum class CallbackType {
  MESSAGE,
  NOTICE,
  REQUEST,
  META_EVENT,
  HEARTBEAT,
  ERROR
};

class IPlugin {
public:
  IPlugin() = default;
  IPlugin(const IPlugin &) = delete;
  auto operator=(const IPlugin &) -> IPlugin & = delete;
  IPlugin(IPlugin &&) = delete;
  auto operator=(IPlugin &&) -> IPlugin & = delete;
  virtual ~IPlugin() = default;

  [[nodiscard]] virtual auto get_name() const -> std::string = 0;

  [[nodiscard]] virtual auto get_version() const -> std::string = 0;

  [[nodiscard]] virtual auto get_description() const -> std::string = 0;

  virtual auto initialize() -> bool = 0;

  virtual void deinitialize() = 0;

  virtual void shutdown() = 0;

  static auto get_bots()
      -> std::tuple<std::unique_lock<std::mutex>,
                    std::vector<std::unique_ptr<core::IBot>> &>;
  static void set_bots(std::vector<std::unique_ptr<core::IBot>> *bots,
                       std::mutex *mutex);

  template <typename T>
  auto get_config_value(const std::string &key) const -> std::optional<T> {
    auto config =
        common::ConfigLoader::instance().get_plugin_config(get_name());
    if (!config)
      return std::nullopt;

    auto node = config->config.at_path(key);
    if (!node)
      return std::nullopt;

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
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      if (node.is_array()) {
        std::vector<std::string> result;
        for (const auto &item : *node.as_array()) {
          if (auto val = item.value<std::string>()) {
            result.push_back(*val);
          }
        }
        return result;
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] auto get_config_section(const std::string &section_name) const
      -> std::optional<toml::table>;

  /// Get the entire config table for this plugin ([plugins.<name>.config])
  [[nodiscard]] auto get_config_table() const -> std::optional<toml::table>;

protected:
  template <typename EventType>
  auto handle_event_base(core::IBot &bot, const EventType &event)
      -> boost::asio::awaitable<void> {
    co_return;
  }

private:
  static std::vector<std::unique_ptr<core::IBot>> *bots_;
  static std::mutex *bots_mutex_;
};

#define OBCX_PLUGIN_EXPORT(PluginClass)                                        \
  extern "C" {                                                                 \
  void *obcx_create_plugin() {                                                 \
    try {                                                                      \
      return new PluginClass();                                                \
    } catch (...) {                                                            \
      return nullptr;                                                          \
    }                                                                          \
  }                                                                            \
  void obcx_destroy_plugin(void *plugin) {                                     \
    if (plugin) {                                                              \
      try {                                                                    \
        delete static_cast<PluginClass *>(plugin);                             \
      } catch (...) {                                                          \
      }                                                                        \
    }                                                                          \
  }                                                                            \
  const char *obcx_get_plugin_name() {                                         \
    static thread_local std::string name;                                      \
    try {                                                                      \
      PluginClass temp;                                                        \
      name = temp.get_name();                                                  \
      return name.c_str();                                                     \
    } catch (...) {                                                            \
      return "unknown";                                                        \
    }                                                                          \
  }                                                                            \
  const char *obcx_get_plugin_version() {                                      \
    static thread_local std::string version;                                   \
    try {                                                                      \
      PluginClass temp;                                                        \
      version = temp.get_version();                                            \
      return version.c_str();                                                  \
    } catch (...) {                                                            \
      return "unknown";                                                        \
    }                                                                          \
  }                                                                            \
  }
} // namespace obcx::interface
