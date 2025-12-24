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
    OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_DIR_ADDED, directory);
  } else {
    OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_DIR_NOT_EXIST, directory);
  }
}

auto PluginManager::load_plugin(const std::string &plugin_name) -> bool {
  if (is_plugin_loaded(plugin_name)) {
    OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_ALREADY_LOADED, plugin_name);
    return true;
  }

  std::string plugin_path = find_plugin_file(plugin_name);
  if (plugin_path.empty()) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_NOT_FOUND_IN_DIRS,
                    plugin_name);
    return false;
  }

  return load_plugin_from_path(plugin_path);
}

auto PluginManager::load_plugin_from_path(const std::string &plugin_path)
    -> bool {
  std::filesystem::path path(plugin_path);
  std::string plugin_name = path.stem().string();

  if (plugin_name.starts_with("lib")) {
    plugin_name = plugin_name.substr(3);
  }

  if (is_plugin_loaded(plugin_name)) {
    OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_ALREADY_LOADED, plugin_name);
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

  OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_LOAD_SUCCESS, plugin_name,
                 plugin_path);
  return true;
}

void PluginManager::unload_plugin(const std::string &plugin_name) {
  auto it = loaded_plugins_.find(plugin_name);
  if (it != loaded_plugins_.end()) {
    shutdown_plugin(plugin_name);
    loaded_plugins_.erase(it);
    OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_UNLOADED, plugin_name);
  }
}

void PluginManager::unload_all_plugins() {
  shutdown_all_plugins();
  loaded_plugins_.clear();
  OBCX_I18N_INFO(common::LogMessageKey::ALL_PLUGINS_UNLOADED);
}

auto PluginManager::is_plugin_loaded(const std::string &plugin_name) const
    -> bool {
  return loaded_plugins_.find(plugin_name) != loaded_plugins_.end();
}

auto PluginManager::get_plugin(const std::string &plugin_name) const
    -> interface::IPlugin * {
  if (auto it = loaded_plugins_.find(plugin_name);
      it != loaded_plugins_.end() && it->second.wrapper) {
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

void PluginManager::deinitialize_plugin(const std::string &plugin_name) const {
  auto *plugin = get_plugin(plugin_name);
  if (!plugin) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_NOT_FOUND, plugin_name);
    return;
  }

  try {
    plugin->deinitialize();
    OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_DEINIT_SUCCESS, plugin_name);
  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_DEINIT_FAILED, plugin_name,
                    e.what());
  }
}

auto PluginManager::initialize_plugin(const std::string &plugin_name) const
    -> bool {
  auto *plugin = get_plugin(plugin_name);
  if (!plugin) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_NOT_FOUND, plugin_name);
    return false;
  }

  try {
    if (plugin->initialize()) {
      OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_INIT_SUCCESS, plugin_name);
      return true;
    } else {
      OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_INIT_FAILED_MSG,
                      plugin_name);
      return false;
    }
  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_INIT_EXCEPTION, plugin_name,
                    e.what());
    return false;
  }
}

void PluginManager::shutdown_plugin(const std::string &plugin_name) const {
  if (auto *plugin = get_plugin(plugin_name)) {
    try {
      plugin->shutdown();
      OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_SHUTDOWN_SUCCESS,
                     plugin_name);
    } catch (const std::exception &e) {
      OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_SHUTDOWN_EXCEPTION,
                      plugin_name, e.what());
    }
  }
}

void PluginManager::initialize_all_plugins() {
  for (const auto &name : loaded_plugins_ | std::views::keys) {
    initialize_plugin(name); // FIXME: check the initialization!
  }
}

void PluginManager::shutdown_all_plugins() {
  for (const auto &name : loaded_plugins_ | std::views::keys) {
    shutdown_plugin(name);
  }
}

auto PluginManager::find_plugin_file(const std::string &plugin_name) const
    -> std::string {
  // Platform-specific library extensions
  const std::vector possible_names = {
      plugin_name,
      "lib" + plugin_name + ".so",
      plugin_name + ".so",
      "lib" + plugin_name + ".dylib",
      plugin_name + ".dylib"
  };

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

auto PluginManager::load_plugin_library(const std::string &plugin_path)
    -> std::unique_ptr<SafePluginWrapper> {
  void *handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
  if (!handle) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_LIB_LOAD_FAILED, plugin_path,
                    dlerror());
    return nullptr;
  }

  dlerror();

  using create_plugin_t = void *(*)();
  auto create_plugin =
      reinterpret_cast<create_plugin_t>(dlsym(handle, "obcx_create_plugin"));

  const char *dlsym_error = dlerror();
  if (dlsym_error) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_SYMBOL_CREATE_FAILED,
                    plugin_path, dlsym_error);
    dlclose(handle);
    return nullptr;
  }

  using destroy_plugin_t = void (*)(void *);
  auto destroy_plugin =
      reinterpret_cast<destroy_plugin_t>(dlsym(handle, "obcx_destroy_plugin"));

  dlsym_error = dlerror();
  if (dlsym_error) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_SYMBOL_DESTROY_FAILED,
                    plugin_path, dlsym_error);
    dlclose(handle);
    return nullptr;
  }

  try {
    void *plugin_ptr = create_plugin();
    if (!plugin_ptr) {
      OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_CREATE_NULL, plugin_path);
      dlclose(handle);
      return nullptr;
    }

    return std::make_unique<SafePluginWrapper>(plugin_ptr, handle,
                                               destroy_plugin);
  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_CREATE_EXCEPTION, plugin_path,
                    e.what());
    dlclose(handle);
    return nullptr;
  }
}

} // namespace obcx::common