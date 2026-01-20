#pragma once

#include "interfaces/plugin.hpp"

#include <dlfcn.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obcx::common {

class SafePluginWrapper {
public:
  SafePluginWrapper(void *plugin_ptr, void *handle,
                    void (*destroy_func)(void *))
      : plugin_ptr_(plugin_ptr), handle_(handle), destroy_func_(destroy_func) {}

  ~SafePluginWrapper() {
    if (plugin_ptr_ && destroy_func_) {
      destroy_func_(plugin_ptr_);
    }
    if (handle_) {
      dlclose(handle_);
    }
  }

  SafePluginWrapper(const SafePluginWrapper &) = delete;
  auto operator=(const SafePluginWrapper &) -> SafePluginWrapper & = delete;

  SafePluginWrapper(SafePluginWrapper &&other) noexcept
      : plugin_ptr_(other.plugin_ptr_), handle_(other.handle_),
        destroy_func_(other.destroy_func_) {
    other.plugin_ptr_ = nullptr;
    other.handle_ = nullptr;
    other.destroy_func_ = nullptr;
  }

  auto operator=(SafePluginWrapper &&other) noexcept -> SafePluginWrapper & {
    if (this != &other) {
      reset();
      plugin_ptr_ = other.plugin_ptr_;
      handle_ = other.handle_;
      destroy_func_ = other.destroy_func_;
      other.plugin_ptr_ = nullptr;
      other.handle_ = nullptr;
      other.destroy_func_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] auto get() const -> interface::IPlugin * {
    return static_cast<interface::IPlugin *>(plugin_ptr_);
  }

  auto operator->() const -> interface::IPlugin * { return get(); }

  auto operator*() const -> interface::IPlugin & { return *get(); }

  explicit operator bool() const { return plugin_ptr_ != nullptr; }

private:
  void reset() {
    if (plugin_ptr_ && destroy_func_) {
      destroy_func_(plugin_ptr_);
    }
    if (handle_) {
      dlclose(handle_);
    }
    plugin_ptr_ = nullptr;
    handle_ = nullptr;
    destroy_func_ = nullptr;
  }

  void *plugin_ptr_;
  void *handle_;
  void (*destroy_func_)(void *);
};

struct LoadedPlugin {
  std::unique_ptr<SafePluginWrapper> wrapper;
  std::string path;

  LoadedPlugin() = default;
  ~LoadedPlugin() = default;

  LoadedPlugin(const LoadedPlugin &) = delete;
  auto operator=(const LoadedPlugin &) -> LoadedPlugin & = delete;

  LoadedPlugin(LoadedPlugin &&other) noexcept = default;
  auto operator=(LoadedPlugin &&other) noexcept -> LoadedPlugin & = default;
};

class PluginManager {

public:
  PluginManager() = default;
  ~PluginManager();

  PluginManager(const PluginManager &) = delete;
  auto operator=(const PluginManager &) -> PluginManager & = delete;

  void add_plugin_directory(const std::string &directory);

  auto load_plugin(const std::string &plugin_name) -> bool;

  auto load_plugin_from_path(const std::string &plugin_path) -> bool;

  void unload_plugin(const std::string &plugin_name);

  void unload_all_plugins();

  [[nodiscard]] auto is_plugin_loaded(const std::string &plugin_name) const
      -> bool;

  [[nodiscard]] auto get_plugin(const std::string &plugin_name) const
      -> interface::IPlugin *;

  [[nodiscard]] auto get_loaded_plugin_names() const
      -> std::vector<std::string>;

  [[nodiscard]] auto initialize_plugin(const std::string &plugin_name) const
      -> bool;

  void deinitialize_plugin(const std::string &plugin_name) const;

  void shutdown_plugin(const std::string &plugin_name) const;

  void initialize_all_plugins();

  void shutdown_all_plugins();

  /// Sort plugins by priority and topological order
  /// @param plugin_names Names of plugins to sort
  /// @return Sorted plugin names, or empty vector if circular dependency
  /// detected
  [[nodiscard]] auto sort_plugins_by_priority_and_dependencies(
      const std::vector<std::string> &plugin_names) const
      -> std::vector<std::string>;

private:
  [[nodiscard]] auto find_plugin_file(const std::string &plugin_name) const
      -> std::string;

  static auto load_plugin_library(const std::string &plugin_path)
      -> std::unique_ptr<SafePluginWrapper>;

  std::unordered_map<std::string, LoadedPlugin> loaded_plugins_;
  std::vector<std::string> plugin_directories_;
};

} // namespace obcx::common
