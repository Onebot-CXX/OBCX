#pragma once

#include "plugin_registry.hpp"
#include <string>
#include <vector>

namespace obcx::plugin_cli {

class PluginInstaller {
public:
  /// Get the plugins directory (~/.obcx/plugins/)
  static auto get_plugins_dir() -> std::string;

  /// Get the installed.toml path
  static auto get_lockfile_path() -> std::string;

  /// Install a plugin from source
  auto install(const std::string &name, const PluginVersionInfo &info) -> bool;

  /// Remove an installed plugin
  auto remove(const std::string &name) -> bool;

  /// List all installed plugins
  auto list_installed() -> std::vector<InstalledPlugin>;

  /// Update a plugin to a new version
  auto update(const std::string &name, const PluginVersionInfo &new_info)
      -> bool;

private:
  /// Run a shell command and return success/failure
  static auto run_command(const std::string &cmd) -> bool;

  /// Update the installed.toml lockfile
  void update_lockfile(const std::string &name, const InstalledPlugin &info);

  /// Remove an entry from the lockfile
  void remove_from_lockfile(const std::string &name);
};

} // namespace obcx::plugin_cli
