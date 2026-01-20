#include "common/plugin_manager.hpp"
#include "common/config_loader.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <unordered_map>
#include <unordered_set>

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
    if (!initialize_plugin(name)) {
      // FIXME: add error log
    }
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
      plugin_name, "lib" + plugin_name + ".so", plugin_name + ".so",
      "lib" + plugin_name + ".dylib", plugin_name + ".dylib"};

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
  // Use RTLD_NOW to immediately resolve all symbols, ensuring new symbols
  // added during hot reload are properly loaded
  void *handle = dlopen(plugin_path.c_str(), RTLD_NOW);
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

auto PluginManager::sort_plugins_by_priority_and_dependencies(
    const std::vector<std::string> &plugin_names) const
    -> std::vector<std::string> {

  // Build plugin config map
  std::unordered_map<std::string, common::PluginConfig> plugin_configs;
  auto &config_loader = common::ConfigLoader::instance();

  for (const auto &name : plugin_names) {
    if (auto config = config_loader.get_plugin_config(name)) {
      plugin_configs[name] = *config;
    } else {
      // Plugin has no config, create default
      common::PluginConfig default_config;
      default_config.name = name;
      default_config.enabled = true;
      default_config.priority = 0;
      plugin_configs[name] = default_config;
    }
  }

  // Build adjacency graph and in-degree map
  std::unordered_map<std::string, std::vector<std::string>> graph;
  std::unordered_map<std::string, int> in_degree;

  for (const auto &name : plugin_names) {
    in_degree[name] = 0;
    graph[name] = {};
  }

  // Build edges: if B requires A, then A -> B
  for (const auto &[name, config] : plugin_configs) {
    for (const auto &required : config.required) {
      // Check if required plugin is in the list
      if (std::find(plugin_names.begin(), plugin_names.end(), required) ==
          plugin_names.end()) {
        OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_DEPENDENCY_MISSING, name,
                        required);
        throw std::runtime_error("Plugin '" + name + "' requires '" + required +
                                 "' which is not in the plugin list");
      }

      graph[required].push_back(name);
      in_degree[name]++;
    }
  }

  // Kahn's algorithm with priority queue
  // Use max heap: higher priority comes first
  auto cmp = [&plugin_configs](const std::string &a, const std::string &b) {
    return plugin_configs[a].priority < plugin_configs[b].priority;
  };
  std::priority_queue<std::string, std::vector<std::string>, decltype(cmp)> pq(
      cmp);

  // Add all nodes with in-degree 0
  for (const auto &[name, degree] : in_degree) {
    if (degree == 0) {
      pq.push(name);
    }
  }

  std::vector<std::string> sorted_plugins;

  while (!pq.empty()) {
    std::string current = pq.top();
    pq.pop();
    sorted_plugins.push_back(current);

    // Reduce in-degree for neighbors
    for (const auto &neighbor : graph[current]) {
      in_degree[neighbor]--;
      if (in_degree[neighbor] == 0) {
        pq.push(neighbor);
      }
    }
  }

  // Check for circular dependencies
  if (sorted_plugins.size() != plugin_names.size()) {
    // Find plugins involved in the cycle
    std::vector<std::string> cycle_plugins;
    for (const auto &[name, degree] : in_degree) {
      if (degree > 0) {
        cycle_plugins.push_back(name);
      }
    }

    std::string cycle_info;
    for (size_t i = 0; i < cycle_plugins.size(); ++i) {
      cycle_info += cycle_plugins[i];
      if (i < cycle_plugins.size() - 1) {
        cycle_info += ", ";
      }
    }

    // Log circular dependency error
    OBCX_I18N_ERROR(common::LogMessageKey::PLUGIN_CIRCULAR_DEPENDENCY,
                    cycle_info);

    // Log detailed dependency information
    std::string detail_info;
    for (const auto &name : cycle_plugins) {
      const auto &config = plugin_configs[name];
      if (!config.required.empty()) {
        std::string deps;
        for (size_t i = 0; i < config.required.size(); ++i) {
          deps += config.required[i];
          if (i < config.required.size() - 1) {
            deps += ", ";
          }
        }
        std::string line = "Plugin '" + name + "' requires: " + deps;
        OBCX_I18N_ERROR(common::LogMessageKey::CONFIG_INVALID_VALUE, line);
        if (!detail_info.empty()) {
          detail_info += "; ";
        }
        detail_info += line;
      }
    }

    throw std::runtime_error("Circular dependency detected among plugins: " +
                             cycle_info + ". " + detail_info);
  }

  return sorted_plugins;
}

} // namespace obcx::common
