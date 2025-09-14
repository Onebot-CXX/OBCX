#pragma once

#include "common/ConfigLoader.hpp"
#include "common/Logger.hpp"
#include "interface/IBot.hpp"
#include "interface/IPlugin.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace obcx::common {

struct LoadedPlugin {
  void *handle;
  std::unique_ptr<interface::IPlugin> instance;
  std::string path;

  LoadedPlugin() : handle(nullptr) {}
  ~LoadedPlugin() {
    if (handle) {
      dlclose(handle);
    }
  }

  LoadedPlugin(const LoadedPlugin &) = delete;
  LoadedPlugin &operator=(const LoadedPlugin &) = delete;

  LoadedPlugin(LoadedPlugin &&other) noexcept
      : handle(other.handle), instance(std::move(other.instance)),
        path(std::move(other.path)) {
    other.handle = nullptr;
  }

  LoadedPlugin &operator=(LoadedPlugin &&other) noexcept {
    if (this != &other) {
      if (handle) {
        dlclose(handle);
      }
      handle = other.handle;
      instance = std::move(other.instance);
      path = std::move(other.path);
      other.handle = nullptr;
    }
    return *this;
  }
};

class PluginManager {
private:
  std::unordered_map<std::string, LoadedPlugin> loaded_plugins_;
  std::vector<std::string> plugin_directories_;

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

  interface::IPlugin *get_plugin(const std::string &plugin_name);

  std::vector<std::string> get_loaded_plugin_names() const;

  bool initialize_plugin(const std::string &plugin_name);

  void deinitialize_plugin(const std::string &plugin_name);

  void shutdown_plugin(const std::string &plugin_name);

  void initialize_all_plugins();

  void shutdown_all_plugins();

private:
  std::string find_plugin_file(const std::string &plugin_name);

  bool load_plugin_library(const std::string &plugin_path,
                           LoadedPlugin &loaded_plugin);
};

} // namespace obcx::common