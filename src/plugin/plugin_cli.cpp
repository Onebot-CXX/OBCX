#include "plugin_cli.hpp"
#include "obcx/version.hpp"
#include "plugin_installer.hpp"
#include "plugin_registry.hpp"
#include "plugin_resolver.hpp"

#include <cstring>
#include <iostream>
#include <print>

namespace obcx::plugin_cli {

namespace {

void print_usage() {
  std::println("Usage: obcx plugin <command> [args]");
  std::println("");
  std::println("Commands:");
  std::println("  search <query>    Search the plugin registry");
  std::println("  list              List installed plugins");
  std::println("  install <name>    Install a plugin from registry");
  std::println("  update [name]     Update installed plugin(s)");
  std::println("  remove <name>     Remove an installed plugin");
  std::println("  info <name>       Show plugin details from registry");
  std::println("");
  std::println("Options:");
  std::println("  --registry <url>  Use custom registry URL");
  std::println("  --help            Show this help message");
}

auto cmd_search(PluginRegistry &registry, const std::string &query) -> int {
  if (!registry.fetch_index())
    return 1;

  auto results = registry.search(query);
  if (results.empty()) {
    std::println("No plugins found matching '{}'", query);
    return 0;
  }

  std::println("Found {} plugin(s):", results.size());
  for (const auto &p : results) {
    std::println("  {} - {}", p.name, p.description);
    if (!p.versions.empty()) {
      std::println("    Latest: v{}", p.versions.back().version);
    }
    if (!p.homepage.empty()) {
      std::println("    Homepage: {}", p.homepage);
    }
  }
  return 0;
}

auto cmd_list([[maybe_unused]] PluginRegistry &registry) -> int {
  PluginInstaller installer;
  auto installed = installer.list_installed();

  if (installed.empty()) {
    std::println("No plugins installed.");
    std::println("Use 'obcx plugin install <name>' to install plugins.");
    return 0;
  }

  std::println("Installed plugins:");
  for (const auto &p : installed) {
    std::println("  {} v{} ({})", p.name, p.version, p.git_tag);
    std::println("    Installed: {}", p.installed_at);
  }
  return 0;
}

auto cmd_install(PluginRegistry &registry, const std::string &name) -> int {
  if (!registry.fetch_index())
    return 1;

  auto ver = registry.get_latest_compatible_version(name, OBCX_ABI_VERSION);
  if (!ver) {
    std::cerr << "No compatible version found for plugin '" << name
              << "' (ABI version " << OBCX_ABI_VERSION << ")\n";

    // Show available versions
    auto info = registry.get_plugin_info(name);
    if (!info) {
      std::cerr << "Plugin '" << name << "' not found in registry\n";
    } else if (!info->versions.empty()) {
      std::cerr << "Available versions:\n";
      for (const auto &v : info->versions) {
        std::cerr << "  v" << v.version << " (ABI " << v.obcx_abi_version
                  << ")\n";
      }
    }
    return 1;
  }

  // Check dependencies
  PluginInstaller installer;
  if (!PluginResolver::check_dependencies(*ver, installer)) {
    std::cerr << "Dependency check failed. Install required plugins first.\n";
    return 1;
  }

  return installer.install(name, *ver) ? 0 : 1;
}

auto cmd_update(PluginRegistry &registry, const std::string &name) -> int {
  if (!registry.fetch_index())
    return 1;

  PluginInstaller installer;
  auto installed = installer.list_installed();

  if (name.empty()) {
    // Update all
    bool any_updated = false;
    for (const auto &p : installed) {
      auto ver =
          registry.get_latest_compatible_version(p.name, OBCX_ABI_VERSION);
      if (ver && ver->version != p.version) {
        std::println("Updating {} from v{} to v{}...", p.name, p.version,
                     ver->version);
        installer.update(p.name, *ver);
        any_updated = true;
      }
    }
    if (!any_updated) {
      std::println("All plugins are up to date.");
    }
  } else {
    auto ver = registry.get_latest_compatible_version(name, OBCX_ABI_VERSION);
    if (!ver) {
      std::cerr << "No compatible version found for '" << name << "'\n";
      return 1;
    }
    return installer.update(name, *ver) ? 0 : 1;
  }
  return 0;
}

auto cmd_remove(const std::string &name) -> int {
  PluginInstaller installer;
  return installer.remove(name) ? 0 : 1;
}

auto cmd_info(PluginRegistry &registry, const std::string &name) -> int {
  if (!registry.fetch_index())
    return 1;

  auto info = registry.get_plugin_info(name);
  if (!info) {
    std::cerr << "Plugin '" << name << "' not found in registry\n";
    return 1;
  }

  std::println("Plugin: {}", info->name);
  std::println("Description: {}", info->description);
  if (!info->homepage.empty()) {
    std::println("Homepage: {}", info->homepage);
  }
  std::println("Versions:");
  for (const auto &v : info->versions) {
    std::println("  v{} (ABI {}{})", v.version, v.obcx_abi_version,
                 v.obcx_abi_version == OBCX_ABI_VERSION ? ", compatible"
                                                        : ", incompatible");
    if (!v.required_plugins.empty()) {
      std::print("    Requires: ");
      for (size_t i = 0; i < v.required_plugins.size(); ++i) {
        if (i > 0)
          std::print(", ");
        std::print("{}", v.required_plugins[i]);
      }
      std::println("");
    }
  }
  return 0;
}

} // namespace

auto run_plugin_command(int argc, char *argv[]) -> int {
  // argv[0] = "obcx", argv[1] = "plugin", argv[2] = subcommand
  if (argc < 3) {
    print_usage();
    return 1;
  }

  std::string subcmd = argv[2];
  std::string registry_url = PluginRegistry::DEFAULT_REGISTRY_URL;

  // Check for --registry flag
  for (int i = 3; i < argc - 1; ++i) {
    if (std::strcmp(argv[i], "--registry") == 0) {
      registry_url = argv[i + 1];
      break;
    }
  }

  if (subcmd == "--help" || subcmd == "-h" || subcmd == "help") {
    print_usage();
    return 0;
  }

  PluginRegistry registry(registry_url);

  if (subcmd == "search") {
    if (argc < 4) {
      std::cerr << "Usage: obcx plugin search <query>\n";
      return 1;
    }
    return cmd_search(registry, argv[3]);
  } else if (subcmd == "list" || subcmd == "ls") {
    return cmd_list(registry);
  } else if (subcmd == "install" || subcmd == "add") {
    if (argc < 4) {
      std::cerr << "Usage: obcx plugin install <name>\n";
      return 1;
    }
    return cmd_install(registry, argv[3]);
  } else if (subcmd == "update" || subcmd == "upgrade") {
    std::string name = (argc >= 4) ? argv[3] : "";
    return cmd_update(registry, name);
  } else if (subcmd == "remove" || subcmd == "rm" || subcmd == "uninstall") {
    if (argc < 4) {
      std::cerr << "Usage: obcx plugin remove <name>\n";
      return 1;
    }
    return cmd_remove(argv[3]);
  } else if (subcmd == "info") {
    if (argc < 4) {
      std::cerr << "Usage: obcx plugin info <name>\n";
      return 1;
    }
    return cmd_info(registry, argv[3]);
  } else {
    std::cerr << "Unknown command: " << subcmd << "\n\n";
    print_usage();
    return 1;
  }
}

} // namespace obcx::plugin_cli
