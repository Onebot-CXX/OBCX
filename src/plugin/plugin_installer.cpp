#include "plugin_installer.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <print>
#include <sstream>
#include <toml++/toml.hpp>

namespace obcx::plugin_cli {

auto PluginInstaller::get_plugins_dir() -> std::string {
  const char *home = std::getenv("HOME");
  if (!home) {
    return "./plugins";
  }
  return std::string(home) + "/.obcx/plugins";
}

auto PluginInstaller::get_lockfile_path() -> std::string {
  return get_plugins_dir() + "/installed.toml";
}

auto PluginInstaller::run_command(const std::string &cmd) -> bool {
  return std::system(cmd.c_str()) == 0;
}

auto PluginInstaller::install(const std::string &name,
                              const PluginVersionInfo &info) -> bool {
  std::println("Installing plugin '{}' version {}...", name, info.version);

  auto plugins_dir = get_plugins_dir();
  std::filesystem::create_directories(plugins_dir);

  // Create temp directory for building
  auto tmp_dir =
      std::filesystem::temp_directory_path() / ("obcx_build_" + name);
  if (std::filesystem::exists(tmp_dir)) {
    std::filesystem::remove_all(tmp_dir);
  }

  // Step 1: Clone
  std::println("  Cloning {}...", info.git_url);
  std::string clone_cmd = "git clone --branch " + info.git_tag +
                          " --depth 1 \"" + info.git_url + "\" \"" +
                          tmp_dir.string() + "\" 2>&1";
  if (!run_command(clone_cmd)) {
    std::cerr << "Failed to clone plugin repository\n";
    return false;
  }

  // Step 2: Configure
  std::println("  Configuring...");
  auto build_dir = tmp_dir / "build";
  std::string cmake_cmd = "cmake -B \"" + build_dir.string() + "\" -S \"" +
                          tmp_dir.string() + "\" -GNinja 2>&1";
  if (!run_command(cmake_cmd)) {
    std::cerr << "Failed to configure plugin\n";
    std::filesystem::remove_all(tmp_dir);
    return false;
  }

  // Step 3: Build
  std::println("  Building...");
  std::string build_cmd = "cmake --build \"" + build_dir.string() + "\" 2>&1";
  if (!run_command(build_cmd)) {
    std::cerr << "Failed to build plugin\n";
    std::filesystem::remove_all(tmp_dir);
    return false;
  }

  // Step 4: Copy built plugin to plugins directory
  auto built_plugins_dir = build_dir / "plugins";
  bool found = false;
  if (std::filesystem::exists(built_plugins_dir)) {
    for (const auto &entry :
         std::filesystem::directory_iterator(built_plugins_dir)) {
      auto ext = entry.path().extension().string();
      if (ext == ".so" || ext == ".dylib") {
        auto dest =
            std::filesystem::path(plugins_dir) / entry.path().filename();
        std::filesystem::copy_file(
            entry.path(), dest,
            std::filesystem::copy_options::overwrite_existing);
        std::println("  Installed: {}", dest.string());
        found = true;
      }
    }
  }

  if (!found) {
    std::cerr << "No plugin library found in build output\n";
    std::filesystem::remove_all(tmp_dir);
    return false;
  }

  // Step 5: Update lockfile
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream time_ss;
  time_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");

  InstalledPlugin installed;
  installed.name = name;
  installed.version = info.version;
  installed.git_url = info.git_url;
  installed.git_tag = info.git_tag;
  installed.installed_at = time_ss.str();
  installed.path = plugins_dir + "/" + name;
  update_lockfile(name, installed);

  // Cleanup
  std::filesystem::remove_all(tmp_dir);
  std::println("Plugin '{}' installed successfully!", name);
  return true;
}

auto PluginInstaller::remove(const std::string &name) -> bool {
  auto plugins_dir = get_plugins_dir();

  // Find and remove the plugin file
  bool found = false;
  for (const auto &ext : {".so", ".dylib"}) {
    auto path = std::filesystem::path(plugins_dir) / (name + ext);
    if (std::filesystem::exists(path)) {
      std::filesystem::remove(path);
      found = true;
    }
  }

  if (!found) {
    std::cerr << "Plugin '" << name << "' not found in " << plugins_dir << "\n";
    return false;
  }

  remove_from_lockfile(name);
  std::println("Plugin '{}' removed.", name);
  return true;
}

auto PluginInstaller::list_installed() -> std::vector<InstalledPlugin> {
  std::vector<InstalledPlugin> result;
  auto lockfile = get_lockfile_path();

  if (!std::filesystem::exists(lockfile)) {
    return result;
  }

  try {
    auto tbl = toml::parse_file(lockfile);
    for (auto &[name, value] : tbl) {
      if (value.is_table()) {
        auto &t = *value.as_table();
        InstalledPlugin p;
        p.name = std::string(name.str());
        p.version = t["version"].value_or(std::string("unknown"));
        p.git_url = t["git_url"].value_or(std::string(""));
        p.git_tag = t["git_tag"].value_or(std::string(""));
        p.installed_at = t["installed_at"].value_or(std::string(""));
        p.path = t["path"].value_or(std::string(""));
        result.push_back(std::move(p));
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to parse " << lockfile << ": " << e.what() << "\n";
  }

  return result;
}

auto PluginInstaller::update(const std::string &name,
                             const PluginVersionInfo &new_info) -> bool {
  remove(name);
  return install(name, new_info);
}

void PluginInstaller::update_lockfile(const std::string &name,
                                      const InstalledPlugin &info) {
  auto lockfile = get_lockfile_path();
  toml::table tbl;

  if (std::filesystem::exists(lockfile)) {
    try {
      tbl = toml::parse_file(lockfile);
    } catch (...) {
    }
  }

  tbl.insert_or_assign(name, toml::table{
                                 {"version", info.version},
                                 {"git_url", info.git_url},
                                 {"git_tag", info.git_tag},
                                 {"installed_at", info.installed_at},
                                 {"path", info.path},
                             });

  std::filesystem::create_directories(
      std::filesystem::path(lockfile).parent_path());
  std::ofstream f(lockfile);
  f << tbl;
}

void PluginInstaller::remove_from_lockfile(const std::string &name) {
  auto lockfile = get_lockfile_path();
  if (!std::filesystem::exists(lockfile))
    return;

  try {
    auto tbl = toml::parse_file(lockfile);
    tbl.erase(name);
    std::ofstream f(lockfile);
    f << tbl;
  } catch (...) {
  }
}

} // namespace obcx::plugin_cli
