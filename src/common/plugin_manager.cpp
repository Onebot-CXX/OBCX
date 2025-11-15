#include "common/plugin_manager.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <filesystem>

namespace obcx::common {

PluginManager::~PluginManager() {
  shutdown_all_plugins();
  unload_all_plugins();
}

void PluginManager::add_plugin_directory(const std::string &directory) {
  if (std::filesystem::exists(directory) &&
      std::filesystem::is_directory(directory)) {
    plugin_directories_.push_back(directory);
    OBCX_INFO("Added plugin directory: {}", directory);
  } else {
    OBCX_WARN("Plugin directory does not exist: {}", directory);
  }
}

bool PluginManager::load_plugin(const std::string &plugin_name) {
  if (is_plugin_loaded(plugin_name)) {
    OBCX_WARN("Plugin {} is already loaded", plugin_name);
    return true;
  }

  std::string plugin_path = find_plugin_file(plugin_name);
  if (plugin_path.empty()) {
    OBCX_ERROR("Plugin {} not found in plugin directories", plugin_name);
    return false;
  }

  return load_plugin_from_path(plugin_path);
}

bool PluginManager::load_plugin_from_path(const std::string &plugin_path) {
  std::filesystem::path path(plugin_path);
  std::string plugin_name = path.stem().string();

  if (plugin_name.starts_with("lib")) {
    plugin_name = plugin_name.substr(3);
  }

  if (is_plugin_loaded(plugin_name)) {
    OBCX_WARN("Plugin {} is already loaded", plugin_name);
    return true;
  }

  auto wrapper = load_plugin_library(plugin_path);
  if (!wrapper) {
    return false;
  }

  LoadedPlugin loaded_plugin;
  loaded_plugin.wrapper = std::move(wrapper);
  loaded_plugin.path = plugin_path;
  loaded_plugins_[plugin_name] = std::move(loaded_plugin);

  OBCX_INFO("Plugin {} loaded successfully from {}", plugin_name, plugin_path);
  return true;
}

void PluginManager::unload_plugin(const std::string &plugin_name) {
  auto it = loaded_plugins_.find(plugin_name);
  if (it != loaded_plugins_.end()) {
    shutdown_plugin(plugin_name);
    loaded_plugins_.erase(it);
    OBCX_INFO("Plugin {} unloaded", plugin_name);
  }
}

void PluginManager::unload_all_plugins() {
  shutdown_all_plugins();
  loaded_plugins_.clear();
  OBCX_INFO("All plugins unloaded");
}

bool PluginManager::is_plugin_loaded(const std::string &plugin_name) const {
  return loaded_plugins_.find(plugin_name) != loaded_plugins_.end();
}

interface::IPlugin *PluginManager::get_plugin(
    const std::string &plugin_name) const {
  auto it = loaded_plugins_.find(plugin_name);
  if (it != loaded_plugins_.end() && it->second.wrapper) {
    return it->second.wrapper->get();
  }
  return nullptr;
}

std::vector<std::string> PluginManager::get_loaded_plugin_names() const {
  std::vector<std::string> names;
  names.reserve(loaded_plugins_.size());

  for (const auto &[name, plugin] : loaded_plugins_) {
    names.push_back(name);
  }

  return names;
}

void PluginManager::deinitialize_plugin(const std::string &plugin_name) {
  auto *plugin = get_plugin(plugin_name);
  if (!plugin) {
    OBCX_ERROR("Plugin {} not found", plugin_name);
    return;
  }

  try {
    plugin->deinitialize();
    OBCX_INFO("Plugin {} deinitialized successfully", plugin_name);
  } catch (const std::exception &e) {
    OBCX_ERROR("Plugin {} failed to deinitialize: {}", plugin_name, e.what());
  }
}

bool PluginManager::initialize_plugin(const std::string &plugin_name) {
  auto *plugin = get_plugin(plugin_name);
  if (!plugin) {
    OBCX_ERROR("Plugin {} not found", plugin_name);
    return false;
  }

  try {
    if (plugin->initialize()) {
      OBCX_INFO("Plugin {} initialized successfully", plugin_name);
      return true;
    } else {
      OBCX_ERROR("Plugin {} failed to initialize", plugin_name);
      return false;
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during plugin {} initialization: {}", plugin_name,
               e.what());
    return false;
  }
}

void PluginManager::shutdown_plugin(const std::string &plugin_name) {
  auto *plugin = get_plugin(plugin_name);
  if (plugin) {
    try {
      plugin->shutdown();
      OBCX_INFO("Plugin {} shutdown successfully", plugin_name);
    } catch (const std::exception &e) {
      OBCX_ERROR("Exception during plugin {} shutdown: {}", plugin_name,
                 e.what());
    }
  }
}

void PluginManager::initialize_all_plugins() {
  for (const auto &[name, plugin] : loaded_plugins_) {
    initialize_plugin(name);
  }
}

void PluginManager::shutdown_all_plugins() {
  for (const auto &[name, plugin] : loaded_plugins_) {
    shutdown_plugin(name);
  }
}

std::string PluginManager::find_plugin_file(const std::string &plugin_name) {
  std::vector<std::string> possible_names = {
      plugin_name, "lib" + plugin_name + ".so", plugin_name + ".so"};

  for (const auto &directory : plugin_directories_) {
    for (const auto &name : possible_names) {
      std::filesystem::path full_path = std::filesystem::path(directory) / name;
      if (std::filesystem::exists(full_path)) {
        return full_path.string();
      }
    }
  }

  return "";
}

std::unique_ptr<SafePluginWrapper> PluginManager::load_plugin_library(
    const std::string &plugin_path) {
  void *handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
  if (!handle) {
    OBCX_ERROR("Failed to load plugin library {}: {}", plugin_path, dlerror());
    return nullptr;
  }

  dlerror();

  using create_plugin_t = void *(*)();
  auto create_plugin =
      reinterpret_cast<create_plugin_t>(dlsym(handle, "obcx_create_plugin"));

  const char *dlsym_error = dlerror();
  if (dlsym_error) {
    OBCX_ERROR("Failed to load obcx_create_plugin symbol from {}: {}",
               plugin_path, dlsym_error);
    dlclose(handle);
    return nullptr;
  }

  using destroy_plugin_t = void (*)(void *);
  auto destroy_plugin =
      reinterpret_cast<destroy_plugin_t>(dlsym(handle, "obcx_destroy_plugin"));

  dlsym_error = dlerror();
  if (dlsym_error) {
    OBCX_ERROR("Failed to load obcx_destroy_plugin symbol from {}: {}",
               plugin_path, dlsym_error);
    dlclose(handle);
    return nullptr;
  }

  try {
    void *plugin_ptr = create_plugin();
    if (!plugin_ptr) {
      OBCX_ERROR("obcx_create_plugin returned nullptr for {}", plugin_path);
      dlclose(handle);
      return nullptr;
    }

    return std::make_unique<SafePluginWrapper>(plugin_ptr, handle,
                                               destroy_plugin);
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during plugin creation from {}: {}", plugin_path,
               e.what());
    dlclose(handle);
    return nullptr;
  }
}

} // namespace obcx::common