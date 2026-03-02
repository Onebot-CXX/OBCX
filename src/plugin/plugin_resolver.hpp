#pragma once

#include "plugin_installer.hpp"
#include "plugin_registry.hpp"

namespace obcx::plugin_cli {

class PluginResolver {
public:
  /// Check if a plugin version is compatible with current ABI
  static auto is_compatible(const PluginVersionInfo &info, int current_abi)
      -> bool;

  /// Check if all required plugins are installed or available
  static auto check_dependencies(const PluginVersionInfo &info,
                                 const PluginInstaller &installer) -> bool;
};

} // namespace obcx::plugin_cli
