#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace obcx::plugin_cli {

struct PluginVersionInfo {
  std::string version;
  int obcx_abi_version{0};
  std::string obcx_min_version;
  std::vector<std::string> required_plugins;
  std::string git_url;
  std::string git_tag;
};

struct PluginInfo {
  std::string name;
  std::string description;
  std::string homepage;
  std::vector<PluginVersionInfo> versions;
};

struct InstalledPlugin {
  std::string name;
  std::string version;
  std::string git_url;
  std::string git_tag;
  std::string installed_at;
  std::string path;
};

class PluginRegistry {
public:
  static constexpr auto DEFAULT_REGISTRY_URL =
      "https://raw.githubusercontent.com/vollate/obcx-plugin-registry/main/"
      "index.json";

  explicit PluginRegistry(std::string registry_url = DEFAULT_REGISTRY_URL);

  /// Fetch and parse the registry index
  auto fetch_index() -> bool;

  /// Search plugins by query (matches name and description)
  [[nodiscard]] auto search(const std::string &query) const
      -> std::vector<PluginInfo>;

  /// Get info for a specific plugin
  [[nodiscard]] auto get_plugin_info(const std::string &name) const
      -> std::optional<PluginInfo>;

  /// Get latest version compatible with given ABI version
  [[nodiscard]] auto get_latest_compatible_version(const std::string &name,
                                                   int abi_version) const
      -> std::optional<PluginVersionInfo>;

private:
  std::string registry_url_;
  std::unordered_map<std::string, PluginInfo> plugins_;
  bool fetched_{false};
};

} // namespace obcx::plugin_cli
