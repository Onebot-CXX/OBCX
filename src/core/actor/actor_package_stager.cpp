#include "core/actor/actor_package_stager.hpp"

#include "common/logger.hpp"
#include "core/infrastructure/process_staging_uuid.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <spawn.h>
#else
#include <link.h>
#endif
#include <openssl/evp.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

#if defined(__APPLE__)
#ifndef OBCX_INSTALL_NAME_TOOL_EXECUTABLE
#define OBCX_INSTALL_NAME_TOOL_EXECUTABLE "install_name_tool"
#endif
#ifndef OBCX_OTOOL_EXECUTABLE
#define OBCX_OTOOL_EXECUTABLE "otool"
#endif
#ifndef OBCX_CODESIGN_EXECUTABLE
#define OBCX_CODESIGN_EXECUTABLE "codesign"
#endif

extern char **environ;
#else
#ifndef OBCX_PATCHELF_EXECUTABLE
#define OBCX_PATCHELF_EXECUTABLE "patchelf"
#endif
#endif

namespace obcx::core {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kDependencyIdentityFailure =
    "reload_dependency_identity_conflict";

struct CommandResult {
  int exit_code = -1;
  std::string output;
};

struct DependencyEdge {
  std::string original_identity;
  std::string private_source_key;
};

struct ClosureObject {
  fs::path source;
  fs::path staged;
  std::string digest;
  std::string original_soname;
  std::string staged_identity;
  std::vector<DependencyEdge> edges;
  bool actor_library = false;
};

auto run_command(const std::vector<std::string> &arguments) -> CommandResult {
  if (arguments.empty()) {
    return {};
  }

  std::array<int, 2> descriptors{};
  if (pipe(descriptors.data()) != 0) {
    return {.exit_code = -1,
            .output = "pipe failed: " + std::string{std::strerror(errno)}};
  }

#if defined(__APPLE__)
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto &argument : arguments) {
    argv.push_back(const_cast<char *>(argument.c_str()));
  }
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  int spawn_error = posix_spawn_file_actions_init(&actions);
  if (spawn_error != 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    return {.exit_code = -1, .output = std::strerror(spawn_error)};
  }
  spawn_error = posix_spawn_file_actions_addclose(&actions, descriptors[0]);
  if (spawn_error == 0) {
    spawn_error = posix_spawn_file_actions_adddup2(&actions, descriptors[1],
                                                   STDOUT_FILENO);
  }
  if (spawn_error == 0) {
    spawn_error = posix_spawn_file_actions_adddup2(&actions, descriptors[1],
                                                   STDERR_FILENO);
  }
  if (spawn_error == 0) {
    spawn_error = posix_spawn_file_actions_addclose(&actions, descriptors[1]);
  }
  pid_t child = -1;
  if (spawn_error == 0) {
    spawn_error = posix_spawn(&child, argv.front(), &actions, nullptr,
                              argv.data(), environ);
  }
  posix_spawn_file_actions_destroy(&actions);
  if (spawn_error != 0) {
    close(descriptors[0]);
    close(descriptors[1]);
    return {.exit_code = -1, .output = std::strerror(spawn_error)};
  }
#else
  const auto child = fork();
  if (child == -1) {
    close(descriptors[0]);
    close(descriptors[1]);
    return {.exit_code = -1,
            .output = "fork failed: " + std::string{std::strerror(errno)}};
  }
  if (child == 0) {
    close(descriptors[0]);
    if (dup2(descriptors[1], STDOUT_FILENO) == -1 ||
        dup2(descriptors[1], STDERR_FILENO) == -1) {
      _exit(126);
    }
    close(descriptors[1]);

    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto &argument : arguments) {
      argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);
    execv(argv.front(), argv.data());
    _exit(127);
  }

#endif

  close(descriptors[1]);
  std::string output;
  std::array<char, 4096> buffer{};
  while (true) {
    const auto count = read(descriptors[0], buffer.data(), buffer.size());
    if (count > 0) {
      output.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == -1 && errno == EINTR) {
      continue;
    }
    break;
  }
  close(descriptors[0]);

  int status = 0;
#if defined(__APPLE__)
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited == -1 && errno == EINTR);
  if (waited == -1) {
    return {.exit_code = -1, .output = std::strerror(errno)};
  }
#else
  while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
  }
#endif
  const auto exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return {.exit_code = exit_code, .output = std::move(output)};
}

#if !defined(__APPLE__)
auto patchelf(std::initializer_list<std::string> arguments) -> CommandResult {
  std::vector<std::string> command{OBCX_PATCHELF_EXECUTABLE};
  command.insert(command.end(), arguments.begin(), arguments.end());
  return run_command(command);
}
#endif

auto trim(std::string value) -> std::string {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  auto first = std::ranges::find_if(value, [](const unsigned char character) {
    return !std::isspace(character);
  });
  value.erase(value.begin(), first);
  return value;
}

auto lines(const std::string &output) -> std::vector<std::string> {
  std::vector<std::string> result;
  std::istringstream stream(output);
  for (std::string line; std::getline(stream, line);) {
    line = trim(std::move(line));
    if (!line.empty()) {
      result.push_back(std::move(line));
    }
  }
  return result;
}

auto canonical_existing(const fs::path &path) -> std::optional<fs::path> {
  std::error_code error;
  auto canonical = fs::canonical(path, error);
  if (error || !fs::is_regular_file(canonical, error)) {
    return std::nullopt;
  }
  return canonical;
}

auto is_within(const fs::path &root, const fs::path &candidate) -> bool {
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}

auto file_digest(const fs::path &path) -> std::optional<std::string> {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  auto context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>{
      EVP_MD_CTX_new(), &EVP_MD_CTX_free};
  if (!context ||
      EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    return std::nullopt;
  }
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), buffer.size());
    const auto count = input.gcount();
    if (count > 0 && EVP_DigestUpdate(context.get(), buffer.data(),
                                      static_cast<std::size_t>(count)) != 1) {
      return std::nullopt;
    }
  }
  if (!input.eof()) {
    return std::nullopt;
  }

  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0;
  if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) {
    return std::nullopt;
  }
  static constexpr std::array<char, 16> hexadecimal = {
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string result;
  result.reserve(digest_size * 2);
  for (unsigned int index = 0; index < digest_size; ++index) {
    result.push_back(hexadecimal[digest[index] >> 4U]);
    result.push_back(hexadecimal[digest[index] & 0x0FU]);
  }
  return result;
}

struct FileStamp {
  dev_t device;
  ino_t inode;
  off_t size;
  time_t modified_seconds;
  long modified_nanoseconds;
  time_t changed_seconds;
  long changed_nanoseconds;

  auto operator==(const FileStamp &) const -> bool = default;
};

auto file_stamp(const fs::path &path) -> std::optional<FileStamp> {
  struct stat metadata{};
  if (stat(path.c_str(), &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
    return std::nullopt;
  }
  return FileStamp{.device = metadata.st_dev,
                   .inode = metadata.st_ino,
                   .size = metadata.st_size,
#if defined(__APPLE__)
                   .modified_seconds = metadata.st_mtimespec.tv_sec,
                   .modified_nanoseconds = metadata.st_mtimespec.tv_nsec,
                   .changed_seconds = metadata.st_ctimespec.tv_sec,
                   .changed_nanoseconds = metadata.st_ctimespec.tv_nsec};
#else
                   .modified_seconds = metadata.st_mtim.tv_sec,
                   .modified_nanoseconds = metadata.st_mtim.tv_nsec,
                   .changed_seconds = metadata.st_ctim.tv_sec,
                   .changed_nanoseconds = metadata.st_ctim.tv_nsec};
#endif
}

#if !defined(__APPLE__)
auto print_needed(const fs::path &path)
    -> std::optional<std::vector<std::string>> {
  auto result = patchelf({"--print-needed", path.string()});
  if (result.exit_code != 0) {
    return std::nullopt;
  }
  return lines(result.output);
}

auto print_rpath(const fs::path &path) -> std::optional<std::string> {
  auto result = patchelf({"--print-rpath", path.string()});
  if (result.exit_code != 0) {
    return std::nullopt;
  }
  return trim(std::move(result.output));
}

auto print_soname(const fs::path &path) -> std::optional<std::string> {
  auto result = patchelf({"--print-soname", path.string()});
  if (result.exit_code != 0) {
    return std::nullopt;
  }
  return trim(std::move(result.output));
}

auto replace_all(std::string value, std::string_view needle,
                 const std::string &replacement) -> std::string {
  for (std::size_t position = 0;
       (position = value.find(needle, position)) != std::string::npos;) {
    value.replace(position, needle.size(), replacement);
    position += replacement.size();
  }
  return value;
}

auto dependency_directories(const fs::path &consumer) -> std::vector<fs::path> {
  std::vector<fs::path> directories{consumer.parent_path()};
  if (auto rpath = print_rpath(consumer)) {
    std::istringstream stream(*rpath);
    for (std::string entry; std::getline(stream, entry, ':');) {
      entry = replace_all(std::move(entry), "${ORIGIN}",
                          consumer.parent_path().string());
      entry = replace_all(std::move(entry), "$ORIGIN",
                          consumer.parent_path().string());
      if (!entry.empty()) {
        directories.emplace_back(entry);
      }
    }
  }
  return directories;
}

auto resolve_needed(const fs::path &consumer, std::string_view needed,
                    std::optional<std::vector<fs::path>> &directories)
    -> std::optional<fs::path> {
  if (needed.empty()) {
    return std::nullopt;
  }
  const fs::path needed_path{needed};
  if (needed_path.is_absolute()) {
    return canonical_existing(needed_path);
  }
  if (!directories) {
    directories = dependency_directories(consumer);
  }
  for (const auto &directory : *directories) {
    if (auto resolved = canonical_existing(directory / needed_path)) {
      return resolved;
    }
  }
  return std::nullopt;
}

auto loaded_library_path(const std::string &identity)
    -> std::optional<fs::path> {
  void *handle = dlopen(identity.c_str(), RTLD_NOW | RTLD_NOLOAD);
  if (handle == nullptr) {
    return std::nullopt;
  }
  link_map *mapping = nullptr;
  const auto found = dlinfo(handle, RTLD_DI_LINKMAP, &mapping) == 0 &&
                     mapping != nullptr && mapping->l_name != nullptr &&
                     mapping->l_name[0] != '\0';
  std::optional<fs::path> result;
  if (found) {
    result = canonical_existing(mapping->l_name);
  }
  dlclose(handle);
  return result;
}

auto is_intrinsically_process_owned(std::string_view identity) -> bool {
  static constexpr std::array<std::string_view, 9> prefixes = {
      "libobcx_core.so", "libstdc++.so", "libgcc_s.so", "libc.so", "libm.so",
      "libpthread.so",   "libdl.so",     "librt.so",    "ld-linux"};
  return std::ranges::any_of(prefixes, [identity](std::string_view prefix) {
    return identity.starts_with(prefix);
  });
}

#endif

#if defined(__APPLE__)
struct MachODependency {
  std::string kind;
  std::string identity;
  std::string current_version;
  std::string compatibility_version;

  auto operator==(const MachODependency &) const -> bool = default;
};

struct MachOInfo {
  std::optional<MachODependency> id;
  std::vector<MachODependency> dependencies;
  std::vector<std::string> rpaths;
};

auto macho_dependency_command(std::string_view command) -> bool {
  return command == "LC_LOAD_DYLIB" || command == "LC_LOAD_WEAK_DYLIB" ||
         command == "LC_REEXPORT_DYLIB" || command == "LC_LOAD_UPWARD_DYLIB" ||
         command == "LC_LAZY_LOAD_DYLIB";
}

auto inspect_macho(const fs::path &path) -> std::optional<MachOInfo> {
  mach_header_64 header{};
  std::ifstream input(path, std::ios::binary);
  input.read(reinterpret_cast<char *>(&header), sizeof(header));
  // Editing all slices requires inspecting them all. Only native thin images
  // are supported here; reject universal files rather than editing unseen code.
  if (!input || header.magic != MH_MAGIC_64 ||
      header.cputype != CPU_TYPE_ARM64 ||
      (header.filetype != MH_DYLIB && header.filetype != MH_BUNDLE &&
       header.filetype != MH_EXECUTE)) {
    return std::nullopt;
  }
  const auto commands =
      run_command({OBCX_OTOOL_EXECUTABLE, "-l", path.string()});
  if (commands.exit_code != 0) {
    return std::nullopt;
  }
  MachOInfo info;
  MachODependency command;
  std::string rpath;
  std::size_t count = 0;
  const auto finish_command = [&]() {
    if (command.kind.empty()) {
      return false;
    }
    if (command.kind == "LC_ID_DYLIB" ||
        macho_dependency_command(command.kind)) {
      if (command.identity.empty() || command.current_version.empty() ||
          command.compatibility_version.empty()) {
        return false;
      }
      if (command.kind == "LC_ID_DYLIB") {
        if (info.id) {
          return false;
        }
        info.id = command;
      } else {
        info.dependencies.push_back(command);
      }
    } else if (command.kind == "LC_RPATH") {
      if (rpath.empty()) {
        return false;
      }
      info.rpaths.push_back(rpath);
    }
    return true;
  };
  for (const auto &line : lines(commands.output)) {
    if (line.starts_with("Load command ")) {
      if (line != "Load command " + std::to_string(count) ||
          (count != 0 && !finish_command())) {
        return std::nullopt;
      }
      ++count;
      command = {};
      rpath.clear();
    } else if (count != 0 && line.starts_with("cmd ")) {
      if (!command.kind.empty()) {
        return std::nullopt;
      }
      command.kind = trim(line.substr(4));
    } else if ((command.kind == "LC_ID_DYLIB" ||
                macho_dependency_command(command.kind)) &&
               line.starts_with("name ")) {
      const auto offset = line.rfind(" (offset ");
      if (offset == std::string::npos || !line.ends_with(')') ||
          !command.identity.empty()) {
        return std::nullopt;
      }
      command.identity = line.substr(5, offset - 5);
    } else if (command.kind == "LC_RPATH" && line.starts_with("path ")) {
      const auto offset = line.rfind(" (offset ");
      if (offset == std::string::npos || !line.ends_with(')') ||
          !rpath.empty()) {
        return std::nullopt;
      }
      rpath = line.substr(5, offset - 5);
    } else if (line.starts_with("current version ")) {
      command.current_version = line.substr(16);
    } else if (line.starts_with("compatibility version ")) {
      command.compatibility_version = line.substr(22);
    }
  }
  const auto valid = count == header.ncmds && count != 0 && finish_command();
  if (!valid || (header.filetype == MH_DYLIB) != info.id.has_value()) {
    return std::nullopt;
  }

  const auto libraries =
      run_command({OBCX_OTOOL_EXECUTABLE, "-L", path.string()});
  if (libraries.exit_code != 0) {
    return std::nullopt;
  }
  std::vector<std::string> observed;
  auto library_lines = lines(libraries.output);
  if (library_lines.empty() || !library_lines.front().ends_with(':')) {
    return std::nullopt;
  }
  for (std::size_t index = 1; index < library_lines.size(); ++index) {
    const auto &line = library_lines[index];
    const auto version = line.rfind(" (compatibility version ");
    if (version == std::string::npos) {
      return std::nullopt;
    }
    observed.push_back(line.substr(0, version));
  }
  std::vector<std::string> expected;
  if (info.id) {
    expected.push_back(info.id->identity);
  }
  for (const auto &dependency : info.dependencies) {
    expected.push_back(dependency.identity);
  }
  std::ranges::sort(expected);
  std::ranges::sort(observed);
  if (observed != expected) {
    return std::nullopt;
  }
  return info;
}

auto apple_system_library(std::string_view identity) -> bool {
  return identity.starts_with("/usr/lib/") ||
         identity.starts_with("/System/Library/");
}

auto macho_process_owned(std::string_view identity) -> bool {
  if (apple_system_library(identity)) {
    return true;
  }
  const auto name = fs::path{identity}.filename().string();
  static constexpr std::array<std::string_view, 6> runtimes = {
      "libobcx_core", "libstdc++", "libgcc_s",
      "libc++",       "libc++abi", "libSystem"};
  return std::ranges::any_of(runtimes, [&name](std::string_view runtime) {
    return name == std::string{runtime} + ".so" ||
           (name.starts_with(std::string{runtime} + ".") &&
            (name.ends_with(".dylib") ||
             name.find(".so.") != std::string::npos));
  });
}

auto executable_path() -> std::optional<fs::path> {
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (buffer.empty() || _NSGetExecutablePath(buffer.data(), &size) != 0) {
    return std::nullopt;
  }
  return canonical_existing(buffer.data());
}

auto expand_macho_path(std::string_view identity, const fs::path &consumer,
                       const fs::path &executable) -> std::optional<fs::path> {
  if (identity == "@loader_path") {
    return consumer.parent_path();
  }
  if (identity.starts_with("@loader_path/")) {
    return (consumer.parent_path() / identity.substr(13)).lexically_normal();
  }
  if (identity == "@executable_path") {
    return executable;
  }
  if (identity.starts_with("@executable_path/")) {
    return (executable / identity.substr(17)).lexically_normal();
  }
  const fs::path path{identity};
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return std::nullopt;
}

void append_runpath(std::vector<fs::path> &paths, const fs::path &path) {
  if (std::ranges::find(paths, path) == paths.end()) {
    paths.push_back(path);
  }
}

auto macho_runpaths(const MachOInfo &info, const fs::path &consumer,
                    const fs::path &executable,
                    const std::vector<fs::path> &inherited)
    -> std::optional<std::vector<fs::path>> {
  std::vector<fs::path> result;
  for (const auto &rpath : info.rpaths) {
    if (const auto expanded = expand_macho_path(rpath, consumer, executable)) {
      append_runpath(result, *expanded);
    } else {
      return std::nullopt;
    }
  }
  for (const auto &parent : inherited) {
    append_runpath(result, parent);
  }
  return result;
}

auto resolve_macho_dependency(const fs::path &consumer,
                              const std::string &identity,
                              const fs::path &executable,
                              const std::vector<fs::path> &runpaths)
    -> std::optional<fs::path> {
  const auto existing = [](const fs::path &path) -> std::optional<fs::path> {
    if (const auto canonical = canonical_existing(path)) {
      return canonical;
    }
    if (apple_system_library(path.string())) {
      return path;
    }
    return std::nullopt;
  };
  if (const auto expanded = expand_macho_path(identity, consumer, executable)) {
    return existing(*expanded);
  }
  if (identity.starts_with("@rpath/")) {
    for (const auto &directory : runpaths) {
      if (const auto found = existing(directory / identity.substr(7))) {
        return found;
      }
    }
  } else if (!identity.starts_with('@') && fs::path{identity}.is_relative()) {
    return existing(consumer.parent_path() / identity);
  }
  return std::nullopt;
}

auto uuid_identity(const unsigned char *uuid) -> std::string {
  static constexpr char hexadecimal[] = "0123456789abcdef";
  std::string result;
  for (std::size_t index = 0; index < 16; ++index) {
    result.push_back(hexadecimal[uuid[index] >> 4U]);
    result.push_back(hexadecimal[uuid[index] & 15U]);
  }
  return result;
}

auto shared_cache_identity(const std::string &identity)
    -> std::optional<ProcessOwnedDependencyIdentity> {
  task_dyld_info_data_t task{};
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_DYLD_INFO,
                reinterpret_cast<task_info_t>(&task), &count) != KERN_SUCCESS ||
      task.all_image_info_addr == 0) {
    return std::nullopt;
  }
  const auto *images =
      reinterpret_cast<const dyld_all_image_infos *>(task.all_image_info_addr);
  if (images->version < 15 || images->sharedCacheBaseAddress == 0) {
    return std::nullopt;
  }
  // Cache-only images may not be loaded yet. The immutable cache UUID gives
  // them a stable identity before and after the actor's normal dlopen.
  return ProcessOwnedDependencyIdentity{
      .path = identity,
      .digest = "macho-shared-cache-arm64:" +
                uuid_identity(images->sharedCacheUUID) + ":" + identity};
}

auto macho_loaded_path(const std::string &identity,
                       const std::optional<fs::path> &resolved)
    -> std::optional<fs::path> {
  for (std::uint32_t index = 0; index < _dyld_image_count(); ++index) {
    const auto *name = _dyld_get_image_name(index);
    const auto *header = _dyld_get_image_header(index);
    if (name == nullptr || header == nullptr || header->magic != MH_MAGIC_64) {
      continue;
    }
    auto path = canonical_existing(name);
    bool matches = identity == name || (resolved && path && *resolved == *path);
    const auto *header64 = reinterpret_cast<const mach_header_64 *>(header);
    const auto *cursor = reinterpret_cast<const char *>(header64 + 1);
    const auto *end = cursor + header64->sizeofcmds;
    for (std::uint32_t command_index = 0;
         command_index < header64->ncmds &&
         static_cast<std::size_t>(end - cursor) >= sizeof(load_command);
         ++command_index) {
      const auto *command = reinterpret_cast<const load_command *>(cursor);
      if (command->cmdsize < sizeof(load_command) ||
          command->cmdsize > static_cast<std::size_t>(end - cursor)) {
        break;
      }
      if (command->cmd == LC_ID_DYLIB &&
          command->cmdsize >= sizeof(dylib_command)) {
        const auto *dylib = reinterpret_cast<const dylib_command *>(command);
        const auto offset = dylib->dylib.name.offset;
        if (offset < command->cmdsize &&
            std::memchr(cursor + offset, '\0', command->cmdsize - offset)) {
          matches = matches || identity == cursor + offset;
        }
      }
      cursor += command->cmdsize;
    }
    if (matches && path) {
      return path;
    }
  }
  return std::nullopt;
}

struct MachOObject {
  fs::path source;
  fs::path staged;
  std::string digest;
  MachOInfo info;
  std::vector<std::optional<fs::path>> resolved;
  std::vector<std::string> replacements;
  std::vector<std::string> staged_rpaths;
  std::vector<std::vector<fs::path>> contexts;
  bool discovered = false;
};
#endif

auto safe_component(std::string value) -> std::string {
  for (auto &character : value) {
    if (!(std::isalnum(static_cast<unsigned char>(character)) ||
          character == '-' || character == '_')) {
      character = '_';
    }
  }
  return value.empty() ? std::string{"actor"} : value;
}

auto versioned_identity(const fs::path &source, const std::string &digest)
    -> std::string {
  return source.filename().string() + ".obcx-" + digest;
}

auto failure(std::string message) -> ActorPackageStageResult {
  return {.package = nullptr,
          .code = std::string{kDependencyIdentityFailure},
          .message = std::move(message)};
}

} // namespace

struct ActorPackageStagingContext::Impl {
  struct CachedDigest {
    FileStamp stamp;
    std::string digest;
  };
  std::unordered_map<std::string, CachedDigest> process_owned_digests;

  auto digest(const fs::path &path) -> std::optional<std::string> {
    const auto canonical = canonical_existing(path);
    if (!canonical) {
      process_owned_digests.erase(path.string());
      return file_digest(path);
    }
    const auto key = canonical->string();
    const auto before = file_stamp(*canonical);
    const auto cached = process_owned_digests.find(key);
    if (before && cached != process_owned_digests.end() &&
        cached->second.stamp == *before) {
      return cached->second.digest;
    }
    // Evict stale entries even if this observation cannot be cached.
    process_owned_digests.erase(key);
    auto result = file_digest(*canonical);
    if (before && result && file_stamp(*canonical) == before) {
      process_owned_digests.emplace(key, CachedDigest{*before, *result});
    }
    return result;
  }
};

ActorPackageStagingContext::ActorPackageStagingContext()
    : impl_(std::make_unique<Impl>()) {}

ActorPackageStagingContext::~ActorPackageStagingContext() = default;

StagedActorPackage::StagedActorPackage(
    fs::path root, fs::path actor_library,
    std::vector<StagedPrivateDependency> private_dependencies,
    std::map<std::string, ProcessOwnedDependencyIdentity>
        process_owned_dependencies)
    : root_(std::move(root)), actor_library_(std::move(actor_library)),
      private_dependencies_(std::move(private_dependencies)),
      process_owned_dependencies_(std::move(process_owned_dependencies)) {}

StagedActorPackage::~StagedActorPackage() { cleanup(); }

StagedActorPackage::StagedActorPackage(StagedActorPackage &&other) noexcept
    : root_(std::move(other.root_)),
      actor_library_(std::move(other.actor_library_)),
      private_dependencies_(std::move(other.private_dependencies_)),
      process_owned_dependencies_(
          std::move(other.process_owned_dependencies_)) {
  other.root_.clear();
}

auto StagedActorPackage::operator=(StagedActorPackage &&other) noexcept
    -> StagedActorPackage & {
  if (this != &other) {
    cleanup();
    root_ = std::move(other.root_);
    actor_library_ = std::move(other.actor_library_);
    private_dependencies_ = std::move(other.private_dependencies_);
    process_owned_dependencies_ = std::move(other.process_owned_dependencies_);
    other.root_.clear();
  }
  return *this;
}

auto StagedActorPackage::root() const noexcept -> const fs::path & {
  return root_;
}

auto StagedActorPackage::actor_library() const noexcept -> const fs::path & {
  return actor_library_;
}

auto StagedActorPackage::private_dependencies() const noexcept
    -> const std::vector<StagedPrivateDependency> & {
  return private_dependencies_;
}

auto StagedActorPackage::process_owned_dependencies() const noexcept
    -> const std::map<std::string, ProcessOwnedDependencyIdentity> & {
  return process_owned_dependencies_;
}

void StagedActorPackage::cleanup() noexcept {
  if (root_.empty()) {
    return;
  }
  std::error_code error;
  fs::remove_all(root_, error);
  if (error) {
    OBCX_WARN("Failed to remove actor staging directory {}: {}", root_.string(),
              error.message());
  }
  root_.clear();
}

auto ActorPackageStager::stage(const ActorPackageStageRequest &request) const
    -> ActorPackageStageResult {
  ActorPackageStagingContext context;
  return stage(request, context);
}

auto ActorPackageStager::stage(const ActorPackageStageRequest &request,
                               ActorPackageStagingContext &context) const
    -> ActorPackageStageResult {
#if defined(__APPLE__)
  const auto actor_library = canonical_existing(request.actor_library);
  const auto executable = executable_path();
  if (!actor_library || !executable) {
    return failure("cannot resolve actor library or executable path");
  }
  const auto executable_info = inspect_macho(*executable);
  if (!executable_info) {
    return failure("cannot inspect native thin arm64 executable");
  }
  const auto executable_runpaths = macho_runpaths(
      *executable_info, *executable, executable->parent_path(), {});
  if (!executable_runpaths) {
    return failure("unsupported or cyclic executable runpath");
  }
  const auto package_root = actor_library->parent_path();
  static std::atomic_uint64_t sequence = 0;
  const auto staging_base =
      request.staging_root.empty()
          ? fs::temp_directory_path() / "obcx-actor-generations"
          : request.staging_root;
  auto stage_root =
      staging_base /
      (safe_component(request.actor_name) + "-g" +
       std::to_string(request.generation_id) + "-" +
       std::string{detail::process_staging_uuid()} + "-" +
       std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
  std::error_code error;
  if (!fs::create_directories(stage_root, error) || error) {
    return failure("cannot create actor staging directory");
  }
  const auto fail_stage = [&stage_root](std::string message) {
    std::error_code cleanup_error;
    fs::remove_all(stage_root, cleanup_error);
    return failure(std::move(message));
  };
  const auto canonical_root = fs::canonical(stage_root, error);
  if (error) {
    return fail_stage("cannot canonicalize actor staging directory");
  }
  stage_root = canonical_root;

  std::vector<MachOObject> objects;
  objects.push_back(MachOObject{.source = *actor_library});
  std::unordered_map<std::string, std::size_t> object_by_source;
  object_by_source.emplace(actor_library->string(), 0);
  struct WorkItem {
    std::size_t index;
    std::vector<fs::path> inherited;
  };
  std::vector<WorkItem> work{{0, *executable_runpaths}};
  std::map<std::string, ProcessOwnedDependencyIdentity> process_owned;
  for (std::size_t work_index = 0; work_index < work.size(); ++work_index) {
    const auto [object_index, inherited] = work[work_index];
    if (std::ranges::find(objects[object_index].contexts, inherited) !=
        objects[object_index].contexts.end()) {
      continue;
    }
    objects[object_index].contexts.push_back(inherited);
    if (!objects[object_index].discovered) {
      auto info = inspect_macho(objects[object_index].source);
      if (!info) {
        return fail_stage("cannot inspect native thin arm64 Mach-O image: " +
                          objects[object_index].source.string());
      }
      objects[object_index].info = std::move(*info);
    }
    // Keep local copies: discovering another image may reallocate objects.
    const auto source = objects[object_index].source;
    const auto info = objects[object_index].info;
    const auto runpaths =
        macho_runpaths(info, source, executable->parent_path(), inherited);
    if (!runpaths) {
      return fail_stage("unsupported or cyclic Mach-O runpath in " +
                        source.string());
    }
    std::vector<std::string> staged_rpaths;
    for (const auto &rpath : info.rpaths) {
      if (rpath.starts_with("@loader_path")) {
        const auto expanded =
            expand_macho_path(rpath, source, executable->parent_path());
        if (!expanded) {
          return fail_stage("unsupported loader-relative runpath: " + rpath);
        }
        staged_rpaths.push_back(expanded->string());
      } else {
        staged_rpaths.push_back(rpath);
      }
    }
    std::vector<std::optional<fs::path>> resolved_entries;
    std::vector<std::string> replacements;
    for (const auto &dependency : info.dependencies) {
      const auto &needed = dependency.identity;
      auto resolved = resolve_macho_dependency(
          source, needed, executable->parent_path(), *runpaths);
      if (!resolved) {
        resolved = macho_loaded_path(needed, std::nullopt);
      }
      resolved_entries.push_back(resolved);
      replacements.emplace_back();
      if (!resolved) {
        if (dependency.kind != "LC_LOAD_WEAK_DYLIB") {
          return fail_stage("cannot resolve Mach-O dependency " + needed +
                            " from " + source.string());
        }
        if (const auto expanded =
                expand_macho_path(needed, source, executable->parent_path())) {
          replacements.back() = expanded->string();
        }
        continue;
      }
      const auto private_candidate = is_within(package_root, *resolved) &&
                                     !macho_process_owned(needed) &&
                                     !macho_process_owned(resolved->string());
      if (private_candidate) {
        const auto key = resolved->string();
        auto found = object_by_source.find(key);
        if (found == object_by_source.end()) {
          found = object_by_source.emplace(key, objects.size()).first;
          objects.push_back(MachOObject{.source = *resolved});
        }
        work.push_back({found->second, *runpaths});
        continue;
      }

      auto identity_path = macho_loaded_path(needed, resolved);
      if (!identity_path) {
        identity_path = resolved;
      }
      ProcessOwnedDependencyIdentity identity;
      identity.path = identity_path->string();
      if (auto digest = context.impl_->digest(*identity_path)) {
        identity.digest = std::move(*digest);
      } else if (apple_system_library(identity.path)) {
        if (const auto cache = shared_cache_identity(identity.path)) {
          identity = *cache;
        }
      }
      if (identity.digest.empty()) {
        return fail_stage("cannot fingerprint process-owned dependency: " +
                          needed);
      }
      if (const auto expected =
              request.expected_process_owned_dependencies.find(needed);
          expected != request.expected_process_owned_dependencies.end() &&
          identity.digest != expected->second.digest) {
        return fail_stage("process-owned dependency identity changed: " +
                          needed);
      }
      const auto [existing, inserted] = process_owned.emplace(needed, identity);
      if (!inserted && existing->second != identity) {
        return fail_stage("conflicting process-owned dependency identity: " +
                          needed);
      }
      replacements.back() = identity.path;
    }
    auto &object = objects[object_index];
    if (object.discovered && (object.resolved != resolved_entries ||
                              object.staged_rpaths != staged_rpaths ||
                              object.replacements != replacements)) {
      return fail_stage("ambiguous Mach-O dependency loading context: " +
                        source.string());
    }
    object.resolved = std::move(resolved_entries);
    object.replacements = std::move(replacements);
    object.staged_rpaths = std::move(staged_rpaths);
    object.discovered = true;
  }

  for (std::size_t index = 0; index < objects.size(); ++index) {
    auto &object = objects[index];
    auto digest = file_digest(object.source);
    if (!digest) {
      return fail_stage("cannot fingerprint actor dependency closure");
    }
    object.digest = std::move(*digest);
    auto relative = object.source.lexically_relative(package_root);
    if (relative.empty() || *relative.begin() == "..") {
      return fail_stage("actor dependency escapes its package root");
    }
    if (index != 0) {
      relative.replace_filename(
          versioned_identity(object.source, object.digest));
    }
    object.staged = stage_root / relative;
    fs::create_directories(object.staged.parent_path(), error);
    if (error) {
      return fail_stage("cannot create staged dependency layout");
    }
    fs::copy_file(object.source, object.staged, fs::copy_options::none, error);
    if (error) {
      return fail_stage("cannot copy actor dependency closure");
    }
    fs::permissions(object.staged, fs::perms::owner_write,
                    fs::perm_options::add, error);
    if (error) {
      return fail_stage("cannot make staged Mach-O image writable");
    }
  }

  std::vector<StagedPrivateDependency> private_dependencies;
  for (std::size_t object_index = 0; object_index < objects.size();
       ++object_index) {
    auto &object = objects[object_index];
    auto expected = object.info;
    if (expected.id) {
      expected.id->identity = object.staged.string();
      const auto result =
          run_command({OBCX_INSTALL_NAME_TOOL_EXECUTABLE, "-id",
                       expected.id->identity, object.staged.string()});
      if (result.exit_code != 0) {
        return fail_stage(
            "cannot assign generation-unique Mach-O install id: " +
            object.staged.string() + ": " + trim(result.output));
      }
    }
    std::map<std::string, std::string> changes;
    for (std::size_t index = 0; index < expected.dependencies.size(); ++index) {
      auto &dependency = expected.dependencies[index];
      auto replacement = object.replacements[index];
      if (object.resolved[index]) {
        const auto target =
            object_by_source.find(object.resolved[index]->string());
        if (target != object_by_source.end()) {
          replacement = objects[target->second].staged.string();
        }
      }
      if (replacement.empty() || replacement == dependency.identity) {
        continue;
      }
      const auto [change, inserted] =
          changes.emplace(dependency.identity, replacement);
      if (!inserted && change->second != replacement) {
        return fail_stage("conflicting Mach-O install-name rewrites");
      }
      dependency.identity = std::move(replacement);
    }
    for (const auto &[original, replacement] : changes) {
      const auto result =
          run_command({OBCX_INSTALL_NAME_TOOL_EXECUTABLE, "-change", original,
                       replacement, object.staged.string()});
      if (result.exit_code != 0) {
        return fail_stage("cannot rewrite Mach-O dependency: " + original +
                          ": " + trim(result.output));
      }
    }
    if (object.info.rpaths != object.staged_rpaths) {
      std::vector<std::string> deleted;
      for (const auto &rpath : object.info.rpaths) {
        if (std::ranges::find(deleted, rpath) != deleted.end()) {
          continue;
        }
        const auto result =
            run_command({OBCX_INSTALL_NAME_TOOL_EXECUTABLE, "-delete_rpath",
                         rpath, object.staged.string()});
        if (result.exit_code != 0) {
          return fail_stage("cannot rebase Mach-O runpath: " + rpath + ": " +
                            trim(result.output));
        }
        deleted.push_back(rpath);
      }
      expected.rpaths.clear();
      for (const auto &rpath : object.staged_rpaths) {
        if (std::ranges::find(expected.rpaths, rpath) !=
            expected.rpaths.end()) {
          continue;
        }
        const auto result =
            run_command({OBCX_INSTALL_NAME_TOOL_EXECUTABLE, "-add_rpath", rpath,
                         object.staged.string()});
        if (result.exit_code != 0) {
          return fail_stage("cannot add rebased Mach-O runpath: " + rpath +
                            ": " + trim(result.output));
        }
        expected.rpaths.push_back(rpath);
      }
    }
    auto sign = run_command({OBCX_CODESIGN_EXECUTABLE, "--force", "--sign", "-",
                             object.staged.string()});
    if (sign.exit_code == 0) {
      sign = run_command({OBCX_CODESIGN_EXECUTABLE, "--verify", "--strict",
                          object.staged.string()});
    }
    if (sign.exit_code != 0) {
      return fail_stage("cannot ad-hoc sign staged Mach-O image: " +
                        object.staged.string() + ": " + trim(sign.output));
    }
    const auto observed = inspect_macho(object.staged);
    if (!observed || observed->id != expected.id ||
        observed->dependencies != expected.dependencies ||
        observed->rpaths != expected.rpaths) {
      return fail_stage("staged Mach-O identity verification failed: " +
                        object.staged.string());
    }
    if (object_index != 0) {
      private_dependencies.push_back(
          {.source_path = object.source,
           .staged_path = object.staged,
           .original_identity = object.info.id
                                    ? object.info.id->identity
                                    : object.source.filename().string(),
           .staged_identity = object.staged.string(),
           .digest = object.digest});
    }
  }
  OBCX_INFO("Staged actor {} generation {} with {} private dependencies",
            request.actor_name, request.generation_id,
            private_dependencies.size());
  auto package = std::make_unique<StagedActorPackage>(
      stage_root, objects.front().staged, std::move(private_dependencies),
      std::move(process_owned));
  return {.package = std::move(package)};
#else
  const auto actor_library = canonical_existing(request.actor_library);
  if (!actor_library) {
    return failure("actor library is not a regular file");
  }
  const auto package_root = actor_library->parent_path();

  static std::atomic_uint64_t sequence = 0;
  const auto staging_base =
      request.staging_root.empty()
          ? fs::temp_directory_path() / "obcx-actor-generations"
          : request.staging_root;
  const auto stage_root =
      staging_base /
      (safe_component(request.actor_name) + "-g" +
       std::to_string(request.generation_id) + "-" +
       std::string{detail::process_staging_uuid()} + "-" +
       std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
  std::error_code error;
  const auto stage_created = fs::create_directories(stage_root, error);
  if (error || !stage_created) {
    return failure("cannot create actor staging directory");
  }
  const auto cleanup_failed_stage = [&stage_root]() {
    std::error_code cleanup_error;
    fs::remove_all(stage_root, cleanup_error);
  };

  std::vector<ClosureObject> objects;
  std::unordered_map<std::string, std::size_t> object_by_source;
  objects.push_back(
      ClosureObject{.source = *actor_library, .actor_library = true});
  object_by_source.emplace(actor_library->string(), 0);
  std::map<std::string, ProcessOwnedDependencyIdentity> process_owned;

  for (std::size_t object_index = 0; object_index < objects.size();
       ++object_index) {
    auto needed_entries = print_needed(objects[object_index].source);
    if (!needed_entries) {
      cleanup_failed_stage();
      return failure("cannot inspect actor dependency closure");
    }
    // Local storage remains valid when discovering edges grows objects.
    std::optional<std::vector<fs::path>> directories;
    for (const auto &needed : *needed_entries) {
      const auto resolved =
          resolve_needed(objects[object_index].source, needed, directories);
      const auto private_candidate = resolved.has_value() &&
                                     is_within(package_root, *resolved) &&
                                     !is_intrinsically_process_owned(needed);
      if (private_candidate) {
        const auto key = resolved->string();
        if (!object_by_source.contains(key)) {
          object_by_source.emplace(key, objects.size());
          objects.push_back(ClosureObject{.source = *resolved});
        }
        objects[object_index].edges.push_back(
            {.original_identity = needed, .private_source_key = key});
        continue;
      }

      auto identity_path = loaded_library_path(needed);
      if (!identity_path && resolved) {
        identity_path = resolved;
      }
      ProcessOwnedDependencyIdentity identity;
      if (identity_path) {
        identity.path = identity_path->string();
        if (auto digest = context.impl_->digest(*identity_path)) {
          identity.digest = std::move(*digest);
        }
      }
      if (const auto expected =
              request.expected_process_owned_dependencies.find(needed);
          expected != request.expected_process_owned_dependencies.end() &&
          (identity.digest.empty() ||
           identity.digest != expected->second.digest)) {
        cleanup_failed_stage();
        return failure("process-owned dependency identity changed: " + needed);
      }
      if (resolved && is_within(package_root, *resolved) &&
          identity.digest.empty()) {
        cleanup_failed_stage();
        return failure(
            "bundled process-owned dependency is not already live: " + needed);
      }
      const auto [existing, inserted] = process_owned.emplace(needed, identity);
      if (!inserted && existing->second != identity &&
          !existing->second.digest.empty() && !identity.digest.empty()) {
        cleanup_failed_stage();
        return failure("conflicting process-owned dependency identity: " +
                       needed);
      }
    }
  }

  for (auto &object : objects) {
    auto digest = file_digest(object.source);
    if (!digest) {
      cleanup_failed_stage();
      return failure("cannot fingerprint actor dependency closure");
    }
    object.digest = std::move(*digest);
    const auto relative = object.source.lexically_relative(package_root);
    if (relative.empty() || *relative.begin() == "..") {
      cleanup_failed_stage();
      return failure("actor dependency escapes its package root");
    }
    auto staged_relative = relative;
    if (!object.actor_library) {
      object.original_soname = print_soname(object.source)
                                   .value_or(object.source.filename().string());
      if (object.original_soname.empty()) {
        object.original_soname = object.source.filename().string();
      }
      object.staged_identity = versioned_identity(object.source, object.digest);
      staged_relative.replace_filename(object.staged_identity);
    }
    object.staged = stage_root / staged_relative;
    fs::create_directories(object.staged.parent_path(), error);
    if (error) {
      cleanup_failed_stage();
      return failure("cannot create staged dependency layout");
    }
    fs::copy_file(object.source, object.staged, fs::copy_options::none, error);
    if (error) {
      cleanup_failed_stage();
      return failure("cannot copy actor dependency closure");
    }
  }

  for (auto &object : objects) {
    if (!object.actor_library) {
      const auto set_soname = patchelf(
          {"--set-soname", object.staged_identity, object.staged.string()});
      if (set_soname.exit_code != 0) {
        cleanup_failed_stage();
        return failure("cannot assign content-versioned dependency identity");
      }
    }
    for (const auto &edge : object.edges) {
      const auto dependency = object_by_source.find(edge.private_source_key);
      if (dependency == object_by_source.end()) {
        cleanup_failed_stage();
        return failure("private dependency edge is incomplete");
      }
      const auto &identity = objects[dependency->second].staged_identity;
      const auto replace = patchelf({"--replace-needed", edge.original_identity,
                                     identity, object.staged.string()});
      if (replace.exit_code != 0) {
        cleanup_failed_stage();
        return failure("cannot rewrite private dependency edge");
      }
    }
  }

  std::vector<StagedPrivateDependency> private_dependencies;
  for (const auto &object : objects) {
    if (!object.actor_library) {
      const auto soname = print_soname(object.staged);
      if (!soname || *soname != object.staged_identity) {
        cleanup_failed_stage();
        return failure("staged dependency identity verification failed");
      }
      private_dependencies.push_back(
          {.source_path = object.source,
           .staged_path = object.staged,
           .original_identity = object.original_soname,
           .staged_identity = object.staged_identity,
           .digest = object.digest});
    }
    const auto rewritten_needed = print_needed(object.staged);
    if (!rewritten_needed) {
      cleanup_failed_stage();
      return failure("staged closure verification failed");
    }
    for (const auto &edge : object.edges) {
      const auto dependency = object_by_source.find(edge.private_source_key);
      const auto &identity = objects[dependency->second].staged_identity;
      if (std::ranges::find(*rewritten_needed, identity) ==
              rewritten_needed->end() ||
          std::ranges::find(*rewritten_needed, edge.original_identity) !=
              rewritten_needed->end()) {
        cleanup_failed_stage();
        return failure("rewritten private dependency edge verification failed");
      }
    }
  }

  OBCX_INFO("Staged actor {} generation {} with {} private dependencies",
            request.actor_name, request.generation_id,
            private_dependencies.size());
  auto package = std::make_unique<StagedActorPackage>(
      stage_root, objects.front().staged, std::move(private_dependencies),
      std::move(process_owned));
  return {.package = std::move(package)};
#endif
}

} // namespace obcx::core
