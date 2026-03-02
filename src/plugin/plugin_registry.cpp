#include "plugin_registry.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace obcx::plugin_cli {

PluginRegistry::PluginRegistry(std::string registry_url)
    : registry_url_(std::move(registry_url)) {}

auto PluginRegistry::fetch_index() -> bool {
  // Use curl to download index.json to a temp file
  auto tmp_path =
      std::filesystem::temp_directory_path() / "obcx_registry_index.json";
  std::string cmd = "curl -sL \"" + registry_url_ + "\" -o \"" +
                    tmp_path.string() + "\" 2>/dev/null";

  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    std::cerr << "Failed to fetch registry index from: " << registry_url_
              << "\n";
    return false;
  }

  // Parse JSON
  std::ifstream f(tmp_path);
  if (!f.is_open()) {
    std::cerr << "Failed to read downloaded index\n";
    return false;
  }

  try {
    nlohmann::json index;
    f >> index;

    if (!index.contains("plugins")) {
      std::cerr << "Invalid registry format: missing 'plugins' key\n";
      return false;
    }

    for (auto &[name, plugin_json] : index["plugins"].items()) {
      PluginInfo info;
      info.name = name;
      info.description = plugin_json.value("description", "");
      info.homepage = plugin_json.value("homepage", "");

      for (auto &ver_json :
           plugin_json.value("versions", nlohmann::json::array())) {
        PluginVersionInfo ver;
        ver.version = ver_json.value("version", "");
        ver.obcx_abi_version = ver_json.value("obcx_abi_version", 0);
        ver.obcx_min_version = ver_json.value("obcx_min_version", "");

        for (auto &dep :
             ver_json.value("required_plugins", nlohmann::json::array())) {
          ver.required_plugins.push_back(dep.get<std::string>());
        }

        if (ver_json.contains("source")) {
          ver.git_url = ver_json["source"].value("git", "");
          ver.git_tag = ver_json["source"].value("tag", "");
        }

        info.versions.push_back(std::move(ver));
      }

      plugins_[name] = std::move(info);
    }

    fetched_ = true;
    std::filesystem::remove(tmp_path);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to parse registry index: " << e.what() << "\n";
    std::filesystem::remove(tmp_path);
    return false;
  }
}

auto PluginRegistry::search(const std::string &query) const
    -> std::vector<PluginInfo> {
  std::vector<PluginInfo> results;
  std::string lower_query = query;
  std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(),
                 ::tolower);

  for (const auto &[name, info] : plugins_) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   ::tolower);
    std::string lower_desc = info.description;
    std::transform(lower_desc.begin(), lower_desc.end(), lower_desc.begin(),
                   ::tolower);

    if (lower_name.find(lower_query) != std::string::npos ||
        lower_desc.find(lower_query) != std::string::npos) {
      results.push_back(info);
    }
  }
  return results;
}

auto PluginRegistry::get_plugin_info(const std::string &name) const
    -> std::optional<PluginInfo> {
  if (auto it = plugins_.find(name); it != plugins_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto PluginRegistry::get_latest_compatible_version(const std::string &name,
                                                   int abi_version) const
    -> std::optional<PluginVersionInfo> {
  auto info = get_plugin_info(name);
  if (!info || info->versions.empty())
    return std::nullopt;

  // Find latest version with matching ABI
  for (auto it = info->versions.rbegin(); it != info->versions.rend(); ++it) {
    if (it->obcx_abi_version == abi_version) {
      return *it;
    }
  }
  return std::nullopt;
}

} // namespace obcx::plugin_cli
