#include "plugin_resolver.hpp"
#include "plugin_installer.hpp"

#include <algorithm>
#include <iostream>

namespace obcx::plugin_cli {

auto PluginResolver::is_compatible(const PluginVersionInfo &info,
                                   int current_abi) -> bool {
  return info.obcx_abi_version == current_abi;
}

auto PluginResolver::check_dependencies(const PluginVersionInfo &info,
                                        const PluginInstaller &installer)
    -> bool {
  if (info.required_plugins.empty())
    return true;

  auto installed = const_cast<PluginInstaller &>(installer).list_installed();
  for (const auto &dep : info.required_plugins) {
    bool found =
        std::any_of(installed.begin(), installed.end(),
                    [&dep](const InstalledPlugin &p) { return p.name == dep; });
    if (!found) {
      std::cerr << "Missing dependency: " << dep << "\n";
      return false;
    }
  }
  return true;
}

} // namespace obcx::plugin_cli
