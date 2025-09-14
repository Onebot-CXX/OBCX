#pragma once

#include "common/ConfigLoader.hpp"
#include "common/MessageType.hpp"
#include "interface/IBot.hpp"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
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
  virtual ~IPlugin() = default;

  virtual auto get_name() const -> std::string = 0;

  virtual auto get_version() const -> std::string = 0;

  virtual auto get_description() const -> std::string = 0;

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
    }

    return std::nullopt;
  }

  std::optional<toml::table> get_config_section(
      const std::string &section_name) const;

protected:
  template <typename EventType>
  boost::asio::awaitable<void> handle_event_base(core::IBot &bot,
                                                 const EventType &event) {
    co_return;
  }

private:
  // 静态成员存储bot vector的引用和互斥锁
  static std::vector<std::unique_ptr<core::IBot>> *bots_;
  static std::mutex *bots_mutex_;
};

#define OBCX_PLUGIN_EXPORT(PluginClass)                                        \
  extern "C" {                                                                 \
  std::unique_ptr<obcx::interface::IPlugin> create_plugin() {                  \
    return std::make_unique<PluginClass>();                                    \
  }                                                                            \
  void destroy_plugin(obcx::interface::IPlugin *plugin) { delete plugin; }     \
  const char *get_plugin_name() {                                              \
    static auto instance = std::make_unique<PluginClass>();                    \
    return instance->get_name().c_str();                                       \
  }                                                                            \
  const char *get_plugin_version() {                                           \
    static auto instance = std::make_unique<PluginClass>();                    \
    return instance->get_version().c_str();                                    \
  }                                                                            \
  }
} // namespace obcx::interface