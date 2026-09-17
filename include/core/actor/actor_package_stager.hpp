#ifndef OBCX_INCLUDE_CORE_ACTOR_PACKAGE_STAGER_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_PACKAGE_STAGER_HPP_

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace obcx::core {

struct ProcessOwnedDependencyIdentity {
  std::string path;
  std::string digest;

  auto operator==(const ProcessOwnedDependencyIdentity &) const
      -> bool = default;
};

struct StagedPrivateDependency {
  std::filesystem::path source_path;
  std::filesystem::path staged_path;
  std::string original_identity;
  std::string staged_identity;
  std::string digest;
};

class StagedActorPackage {
public:
  StagedActorPackage(std::filesystem::path root,
                     std::filesystem::path actor_library,
                     std::vector<StagedPrivateDependency> private_dependencies,
                     std::map<std::string, ProcessOwnedDependencyIdentity>
                         process_owned_dependencies);
  ~StagedActorPackage();

  StagedActorPackage(const StagedActorPackage &) = delete;
  auto operator=(const StagedActorPackage &) -> StagedActorPackage & = delete;
  StagedActorPackage(StagedActorPackage &&other) noexcept;
  auto operator=(StagedActorPackage &&other) noexcept -> StagedActorPackage &;

  [[nodiscard]] auto root() const noexcept -> const std::filesystem::path &;
  [[nodiscard]] auto actor_library() const noexcept
      -> const std::filesystem::path &;
  [[nodiscard]] auto private_dependencies() const noexcept
      -> const std::vector<StagedPrivateDependency> &;
  [[nodiscard]] auto process_owned_dependencies() const noexcept
      -> const std::map<std::string, ProcessOwnedDependencyIdentity> &;

private:
  void cleanup() noexcept;

  std::filesystem::path root_;
  std::filesystem::path actor_library_;
  std::vector<StagedPrivateDependency> private_dependencies_;
  std::map<std::string, ProcessOwnedDependencyIdentity>
      process_owned_dependencies_;
};

struct ActorPackageStageRequest {
  std::filesystem::path actor_library;
  std::filesystem::path staging_root;
  std::string actor_name;
  std::uint64_t generation_id = 0;
  std::map<std::string, ProcessOwnedDependencyIdentity>
      expected_process_owned_dependencies;
};

struct ActorPackageStageResult {
  std::unique_ptr<StagedActorPackage> package;
  std::string code;
  std::string message;

  explicit operator bool() const noexcept { return package != nullptr; }
};

class ActorPackageStager {
public:
  [[nodiscard]] auto stage(const ActorPackageStageRequest &request) const
      -> ActorPackageStageResult;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_ACTOR_PACKAGE_STAGER_HPP_
