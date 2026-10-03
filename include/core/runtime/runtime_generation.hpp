#ifndef OBCX_INCLUDE_CORE_RUNTIME_GENERATION_HPP_
#define OBCX_INCLUDE_CORE_RUNTIME_GENERATION_HPP_

#include "common/config_snapshot.hpp"
#include "core/actor/actor_package_stager.hpp"
#include "core/actor/actor_restart_constraints.hpp"
#include "core/actor/blocking_executor.hpp"
#include "core/actor/native_actor_scheduler.hpp"
#include "core/bot/typed_operation.hpp"
#include "core/command/command_coordinator.hpp"
#include "core/runtime/runtime_thread_budget.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace obcx::bot {
class BotOperationGateway;
}

namespace obcx::core {

class ActorManager;
class BotInstallationDirectory;
class BotPlatformCatalog;
class DbManager;
class Orchestrator;
struct OrchestratorResult;

enum class RuntimeGenerationBuildPurpose {
  Startup,
  ValidationOnly,
  ReloadCandidate,
};

struct CommandCatalogStatus {
  CommandBotKey key;
  std::uint64_t desired_generation = 0;
  std::optional<std::uint64_t> last_attempted_generation;
  std::optional<std::uint64_t> last_success_generation;
  std::size_t attempts = 0;
  std::size_t retries = 0;
  bool publication_supported = false;
  std::string failure_code;
  std::vector<CommandCatalogEntry> desired;
  std::vector<CommandCatalogEntry> observed;
};

class RuntimeGeneration
    : public std::enable_shared_from_this<RuntimeGeneration> {
public:
  using RouteAdmission = std::shared_ptr<void>;

  ~RuntimeGeneration();

  RuntimeGeneration(const RuntimeGeneration &) = delete;
  auto operator=(const RuntimeGeneration &) -> RuntimeGeneration & = delete;
  RuntimeGeneration(RuntimeGeneration &&) = delete;
  auto operator=(RuntimeGeneration &&) -> RuntimeGeneration & = delete;

  [[nodiscard]] auto id() const noexcept -> std::uint64_t;
  [[nodiscard]] auto thread_budget() const noexcept
      -> const RuntimeThreadBudget &;
  [[nodiscard]] auto config_snapshot() const noexcept
      -> const std::shared_ptr<const common::RuntimeConfigSnapshot> &;
  [[nodiscard]] auto process_owned_fingerprint() const noexcept
      -> const common::ProcessOwnedConfigFingerprint &;
  [[nodiscard]] auto process_owned_dependencies() const noexcept
      -> const std::map<std::string, ProcessOwnedDependencyIdentity> &;
  [[nodiscard]] auto actor_manager() const noexcept -> ActorManager *;
  [[nodiscard]] auto services() const noexcept
      -> const std::shared_ptr<ActorServices> &;
  [[nodiscard]] auto scheduler() const noexcept
      -> const std::shared_ptr<NativeActorScheduler> &;
  [[nodiscard]] auto orchestrator() const noexcept
      -> const std::shared_ptr<Orchestrator> &;
  [[nodiscard]] auto db_manager() const noexcept
      -> const std::shared_ptr<DbManager> &;
  [[nodiscard]] auto bot_installation_directory() const noexcept
      -> const std::shared_ptr<BotInstallationDirectory> &;
  [[nodiscard]] auto bot_operation_client() const noexcept
      -> const std::shared_ptr<bot::BotOperationGateway> &;
  [[nodiscard]] auto blocking_executor() const noexcept
      -> const std::shared_ptr<BlockingExecutor> &;
  [[nodiscard]] auto command_routing_table() const noexcept
      -> const std::shared_ptr<const CommandRoutingTable> &;
  void activate_command_catalogs();
  void activate_actor_background_work();
  void invalidate_actor_background_work() noexcept;
  [[nodiscard]] auto command_catalog_status() const
      -> std::vector<CommandCatalogStatus>;
  [[nodiscard]] auto staging_root() const noexcept
      -> const std::filesystem::path &;

  [[nodiscard]] auto admit_route() -> RouteAdmission;
  auto process(MessageEnvelope message, RouteAdmission admission)
      -> boost::asio::awaitable<OrchestratorResult>;
  auto async_wait_for_drain(std::chrono::steady_clock::time_point deadline)
      -> boost::asio::awaitable<bool>;
  [[nodiscard]] auto in_flight_routes() const noexcept -> std::size_t;

  void shutdown();

private:
  friend class RuntimeGenerationBuilder;
  class RouteLease;
  class RouteState;
  class StagingDirectoryOwner;

  RuntimeGeneration(
      std::uint64_t id, RuntimeThreadBudget thread_budget,
      NativeActorSchedulerOptions scheduler_options,
      std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot,
      common::ProcessOwnedConfigFingerprint process_owned_fingerprint,
      std::shared_ptr<DbManager> db_manager,
      std::shared_ptr<BotInstallationDirectory> bot_installation_directory,
      std::shared_ptr<bot::BotOperationGateway> bot_operation_client,
      std::shared_ptr<BlockingExecutor> blocking_executor,
      std::filesystem::path staging_root);

  void release_route() noexcept;
  auto reconcile_command_catalog(CommandBotKey key)
      -> boost::asio::awaitable<void>;

  // Declared first so the generation staging root is removed last, after all
  // actor aliases, manager handles, and per-actor stage owners are gone.
  std::unique_ptr<StagingDirectoryOwner> staging_owner_;
  std::vector<std::unique_ptr<StagedActorPackage>> staged_packages_;
  std::unique_ptr<ActorManager> actor_manager_;
  std::shared_ptr<RouteState> route_state_;
  std::uint64_t id_ = 0;
  RuntimeThreadBudget thread_budget_;
  std::shared_ptr<const common::RuntimeConfigSnapshot> config_snapshot_;
  common::ProcessOwnedConfigFingerprint process_owned_fingerprint_;
  std::map<std::string, ProcessOwnedDependencyIdentity>
      process_owned_dependencies_;
  std::shared_ptr<boost::asio::thread_pool> actor_io_pool_;
  std::shared_ptr<ActorServices> services_;
  std::shared_ptr<NativeActorScheduler> scheduler_;
  std::shared_ptr<DbManager> db_manager_;
  std::shared_ptr<BotInstallationDirectory> bot_installation_directory_;
  std::shared_ptr<bot::BotOperationGateway> bot_operation_client_;
  std::shared_ptr<BlockingExecutor> blocking_executor_;
  std::shared_ptr<Orchestrator> orchestrator_;
  std::shared_ptr<const CommandRoutingTable> command_routing_table_;
  std::shared_ptr<CommandCoordinator> command_coordinator_;
  mutable std::mutex command_catalog_mutex_;
  std::map<CommandBotKey, CommandCatalogStatus> command_catalog_status_;
  std::atomic_bool command_catalog_active_ = false;
  std::filesystem::path staging_root_;
  std::atomic_bool shutdown_ = false;
};

struct RuntimeGenerationBuildRequest {
  RuntimeGenerationBuildPurpose purpose =
      RuntimeGenerationBuildPurpose::Startup;
  std::uint64_t generation_id = 0;
  std::string config_path;
  std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot;
  std::vector<std::filesystem::path> actor_search_directories;
  std::filesystem::path staging_root;
  std::size_t configured_io_sources = 1;
  std::shared_ptr<DbManager> db_manager;
  std::shared_ptr<BotInstallationDirectory> bot_installation_directory;
  std::shared_ptr<bot::BotOperationGateway> bot_operation_client;
  std::shared_ptr<BlockingExecutor> blocking_executor;
  bool require_registered_bots = false;
  std::optional<common::ProcessOwnedConfigFingerprint>
      active_process_owned_fingerprint;
  std::map<std::string, ProcessOwnedDependencyIdentity>
      active_process_owned_dependencies;
};

enum class RuntimeGenerationBuildStatus {
  NotConfigured,
  Ready,
  Failed,
};

struct RuntimeGenerationBuildFailure {
  std::string code;
  std::string message;
};

struct RuntimeGenerationBuildResult {
  RuntimeGenerationBuildStatus status = RuntimeGenerationBuildStatus::Failed;
  std::shared_ptr<RuntimeGeneration> generation;
  std::optional<RuntimeGenerationBuildFailure> failure;

  [[nodiscard]] auto ready() const noexcept -> bool {
    return status == RuntimeGenerationBuildStatus::Ready &&
           generation != nullptr;
  }
};

class RuntimeGenerationBuilder {
public:
  explicit RuntimeGenerationBuilder(
      std::shared_ptr<const BotPlatformCatalog> catalog);
  [[nodiscard]] auto parse_config(const std::string &config_path) const
      -> common::RuntimeConfigBuildResult;
  [[nodiscard]] auto build(RuntimeGenerationBuildRequest request) const
      -> RuntimeGenerationBuildResult;

private:
  const std::shared_ptr<const BotPlatformCatalog> catalog_;
  const std::shared_ptr<ActorRestartConstraintRegistry> restart_constraints_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_RUNTIME_GENERATION_HPP_
