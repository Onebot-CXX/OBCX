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
#include <link.h>
#include <openssl/evp.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>

#ifndef OBCX_PATCHELF_EXECUTABLE
#define OBCX_PATCHELF_EXECUTABLE "patchelf"
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
  while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
  }
  const auto exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return {.exit_code = exit_code, .output = std::move(output)};
}

auto patchelf(std::initializer_list<std::string> arguments) -> CommandResult {
  std::vector<std::string> command{OBCX_PATCHELF_EXECUTABLE};
  command.insert(command.end(), arguments.begin(), arguments.end());
  return run_command(command);
}

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

auto resolve_needed(const fs::path &consumer, std::string_view needed)
    -> std::optional<fs::path> {
  if (needed.empty()) {
    return std::nullopt;
  }
  const fs::path needed_path{needed};
  if (needed_path.is_absolute()) {
    return canonical_existing(needed_path);
  }

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
  for (const auto &directory : directories) {
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
    for (const auto &needed : *needed_entries) {
      const auto resolved =
          resolve_needed(objects[object_index].source, needed);
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
        if (auto digest = file_digest(*identity_path)) {
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
}

} // namespace obcx::core
