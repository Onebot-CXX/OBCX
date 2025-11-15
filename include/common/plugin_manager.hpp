#pragma once

#include "interfaces/bot.hpp"
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
  SafePluginWrapper &operator=(const SafePluginWrapper &) = delete;

  SafePluginWrapper(SafePluginWrapper &&other) noexcept
      : plugin_ptr_(other.plugin_ptr_), handle_(other.handle_),
        destroy_func_(other.destroy_func_) {
    other.plugin_ptr_ = nullptr;
    other.handle_ = nullptr;
    other.destroy_func_ = nullptr;
  }

  SafePluginWrapper &operator=(SafePluginWrapper &&other) noexcept {
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

  interface::IPlugin *get() const {
    return static_cast<interface::IPlugin *>(plugin_ptr_);
  }

  interface::IPlugin *operator->() const { return get(); }

  interface::IPlugin &operator*() const { return *get(); }

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

  LoadedPlugin(const LoadedPlugin &) = delete;
  LoadedPlugin &operator=(const LoadedPlugin &) = delete;

  LoadedPlugin(LoadedPlugin &&other) noexcept = default;
  LoadedPlugin &operator=(LoadedPlugin &&other) noexcept = default;
};

class PluginManager {

public:
  PluginManager() = default;
  ~PluginManager();

  PluginManager(const PluginManager &) = delete;
  PluginManager &operator=(const PluginManager &) = delete;

  void add_plugin_directory(const std::string &directory);

  bool load_plugin(const std::string &plugin_name);

  bool load_plugin_from_path(const std::string &plugin_path);

  void unload_plugin(const std::string &plugin_name);

  void unload_all_plugins();

  bool is_plugin_loaded(const std::string &plugin_name) const;

  interface::IPlugin *get_plugin(const std::string &plugin_name) const;

  std::vector<std::string> get_loaded_plugin_names() const;

  bool initialize_plugin(const std::string &plugin_name);

  void deinitialize_plugin(const std::string &plugin_name);

  void shutdown_plugin(const std::string &plugin_name);

  void initialize_all_plugins();

  void shutdown_all_plugins();

private:
  std::string find_plugin_file(const std::string &plugin_name);

  std::unique_ptr<SafePluginWrapper> load_plugin_library(
      const std::string &plugin_path);

  std::unordered_map<std::string, LoadedPlugin> loaded_plugins_;
  std::vector<std::string> plugin_directories_;
};

} // namespace obcx::common