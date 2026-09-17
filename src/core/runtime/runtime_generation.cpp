#include "core/runtime/runtime_generation.hpp"
#include "core/runtime/process_configuration.hpp"

#include "common/logger.hpp"
#include "core/actor/actor_generation_lifecycle.hpp"
#include "core/actor/actor_manager.hpp"
#include "core/bot/bot_installation_directory.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"
#include "core/command/command_coordinator.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "core/infrastructure/process_staging_uuid.hpp"
#include "core/runtime/orchestrator.hpp"

#include <algorithm>
#include <atomic>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <cctype>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace obcx::core {
namespace {

namespace fs = std::filesystem;

auto failed(std::string code, std::string message)
    -> RuntimeGenerationBuildResult {
  return {.status = RuntimeGenerationBuildStatus::Failed,
          .generation = nullptr,
          .failure = RuntimeGenerationBuildFailure{
              .code = std::move(code), .message = std::move(message)}};
}

auto actor_library_is_path(const std::string &library) -> bool {
  return library.find('/') != std::string::npos ||
         library.find('\\') != std::string::npos || library.ends_with(".so") ||
         library.ends_with(".dylib") || library.ends_with(".bundle");
}

auto resolve_actor_library(const std::string &library,
                           const std::vector<fs::path> &search_directories)
    -> fs::path {
  if (actor_library_is_path(library)) {
    std::error_code error;
    return fs::is_regular_file(library, error) && !error ? fs::path{library}
                                                         : fs::path{};
  }
  const std::array names = {library,
                            "lib" + library + ".so",
                            library + ".so",
                            "lib" + library + ".dylib",
                            library + ".dylib",
                            "lib" + library + ".bundle",
                            library + ".bundle"};
  for (const auto &directory : search_directories) {
    for (const auto &name : names) {
      std::error_code error;
      const auto candidate = directory / name;
      if (fs::is_regular_file(candidate, error) && !error) {
        return candidate;
      }
    }
  }
  return {};
}

auto ordered_actors(std::vector<common::ActorConfig> actors)
    -> std::optional<std::vector<common::ActorConfig>> {
  std::vector<common::ActorConfig> ordered;
  ordered.reserve(actors.size());
  std::unordered_set<std::string> names;
  while (ordered.size() < actors.size()) {
    const auto before = ordered.size();
    for (const auto &actor : actors) {
      if (names.contains(actor.name) ||
          !std::ranges::all_of(actor.required,
                               [&names](const auto &dependency) {
                                 return names.contains(dependency);
                               })) {
        continue;
      }
      ordered.push_back(actor);
      names.insert(actor.name);
    }
    if (ordered.size() == before) {
      return std::nullopt;
    }
  }
  return ordered;
}

auto generation_staging_root(const fs::path &requested,
                             const std::uint64_t generation_id) -> fs::path {
  static std::atomic_uint64_t sequence = 0;
  const auto root = requested.empty()
                        ? fs::temp_directory_path() / "obcx-runtime-generations"
                        : requested;
  return root /
         ("generation-" + std::to_string(generation_id) + "-" +
          std::string{detail::process_staging_uuid()} + "-" +
          std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
}

auto merge_process_dependency(
    std::map<std::string, ProcessOwnedDependencyIdentity> &target,
    const std::string &name, const ProcessOwnedDependencyIdentity &identity)
    -> bool {
  const auto [existing, inserted] = target.emplace(name, identity);
  if (inserted || existing->second == identity) {
    return true;
  }
  if (existing->second.digest.empty()) {
    existing->second = identity;
    return true;
  }
  return identity.digest.empty();
}

auto validate_actor_configuration(const common::RuntimeConfigSnapshot &snapshot,
                                  const BotPlatformCatalog &catalog,
                                  const std::string &actor,
                                  const ActorInputContract &contract)
    -> std::optional<std::string> {
  const auto section = snapshot.get_actor_section(actor);
  std::unordered_map<std::string, std::int64_t> values;
  for (const auto &constraint : contract.integer_configuration) {
    auto value = constraint.default_value;
    if (section) {
      const auto configured = section->at_path(constraint.key);
      if (configured) {
        const auto integer = configured.value<std::int64_t>();
        if (!integer) {
          return actor + "." + constraint.key + " must be an integer";
        }
        value = *integer;
      }
    }
    if (constraint.minimum && value < *constraint.minimum) {
      return actor + "." + constraint.key + " is below its minimum";
    }
    if (constraint.maximum && value > *constraint.maximum) {
      return actor + "." + constraint.key + " exceeds its maximum";
    }
    values.emplace(constraint.key, value);
  }
  for (const auto &key : contract.required_string_configuration) {
    if (!section) {
      return actor + "." + key + " must be a non-empty string";
    }
    const auto configured = section->at_path(key);
    const auto value = configured.value<std::string>();
    if (!value || value->empty()) {
      return actor + "." + key + " must be a non-empty string";
    }
  }

  const auto check_surfaces =
      [&](const auto &constraint) -> std::optional<std::string> {
    for (const auto &surface : constraint.expected_types) {
      if (!catalog.supports(bot::SurfaceId{surface})) {
        return actor + "." + constraint.key +
               " requires an unregistered exact surface";
      }
    }
    return std::nullopt;
  };
  for (const auto &constraint : contract.bot_installation_configuration) {
    if (const auto error = check_surfaces(constraint)) {
      return error;
    }
  }
  for (const auto &collection :
       contract.bot_installation_collection_configuration) {
    for (const auto &constraint : collection.installation_fields) {
      if (const auto error = check_surfaces(constraint)) {
        return error;
      }
    }
  }
  const auto bots = snapshot.get_bot_configs();
  const auto validate_installation =
      [&](const std::string &path, const std::string &installation,
          const std::vector<std::string> &expected_types)
      -> std::optional<std::string> {
    if (installation.empty()) {
      return path + " must name an enabled configured bot";
    }
    const auto bot = std::ranges::find(
        bots, installation, &common::BotInstallationMetadata::installation_id);
    if (bot == bots.end() || !bot->enabled) {
      return path + " must name an enabled configured bot";
    }
    const auto matches_expected = std::ranges::any_of(
        expected_types, [&bot](const std::string_view expected) {
          return bot->surface.value() == expected;
        });
    if (!matches_expected) {
      std::string expected;
      for (const auto &type : expected_types) {
        expected += expected.empty() ? type : " or " + type;
      }
      return path + " requires bot type " + expected;
    }
    return std::nullopt;
  };
  const auto configured = [&](const std::string &key) {
    return section && static_cast<bool>(section->at_path(key));
  };

  std::unordered_map<std::string, bool> scalar_alternative_selected;
  std::unordered_map<std::string, bool> collection_alternative_selected;
  std::unordered_map<std::string, std::string> scalar_alternative_key;
  std::unordered_map<std::string, std::string> collection_alternative_key;
  for (const auto &constraint : contract.bot_installation_configuration) {
    if (!constraint.alternative_group.empty()) {
      scalar_alternative_key.try_emplace(constraint.alternative_group,
                                         constraint.key);
      if (configured(constraint.key)) {
        scalar_alternative_selected[constraint.alternative_group] = true;
      }
    }
  }
  for (const auto &constraint :
       contract.bot_installation_collection_configuration) {
    if (!constraint.alternative_group.empty()) {
      collection_alternative_key.emplace(constraint.alternative_group,
                                         constraint.key);
      if (configured(constraint.key)) {
        collection_alternative_selected[constraint.alternative_group] = true;
      }
    }
  }
  std::unordered_set<std::string> alternative_groups;
  for (const auto &[group, selected] : scalar_alternative_selected) {
    (void)selected;
    alternative_groups.insert(group);
  }
  for (const auto &constraint : contract.bot_installation_configuration) {
    if (!constraint.alternative_group.empty()) {
      alternative_groups.insert(constraint.alternative_group);
    }
  }
  for (const auto &constraint :
       contract.bot_installation_collection_configuration) {
    if (!constraint.alternative_group.empty()) {
      alternative_groups.insert(constraint.alternative_group);
    }
  }
  for (const auto &group : alternative_groups) {
    const auto scalar = scalar_alternative_selected[group];
    const auto collection = collection_alternative_selected[group];
    if (!scalar && !collection) {
      return actor + "." + scalar_alternative_key[group] + " or " + actor +
             "." + collection_alternative_key[group] +
             " must provide one form for " + group;
    }
    if (scalar && collection) {
      return actor + " configuration must provide exactly one form for " +
             group;
    }
  }

  for (const auto &constraint : contract.bot_installation_configuration) {
    if (!constraint.alternative_group.empty() &&
        !scalar_alternative_selected[constraint.alternative_group]) {
      continue;
    }
    if (!section) {
      return actor + "." + constraint.key +
             " must name an enabled configured bot";
    }
    const auto node = section->at_path(constraint.key);
    const auto installation = node.value<std::string>();
    if (!installation) {
      return actor + "." + constraint.key +
             " must name an enabled configured bot";
    }
    if (auto failure =
            validate_installation(actor + "." + constraint.key, *installation,
                                  constraint.expected_types)) {
      return failure;
    }
  }

  for (const auto &constraint :
       contract.bot_installation_collection_configuration) {
    if (!constraint.alternative_group.empty() &&
        !collection_alternative_selected[constraint.alternative_group]) {
      continue;
    }
    if (!section) {
      return actor + "." + constraint.key + " must be a non-empty array";
    }
    const auto node = section->at_path(constraint.key);
    const auto *items = node.as_array();
    if (items == nullptr || items->size() < constraint.minimum_items) {
      return actor + "." + constraint.key + " must contain at least " +
             std::to_string(constraint.minimum_items) + " item(s)";
    }
    std::unordered_set<std::string> identities;
    std::unordered_map<std::string, std::unordered_set<std::string>>
        unique_field_values;
    std::size_t index = 0;
    for (const auto &item : *items) {
      const auto *table = item.as_table();
      const auto item_path =
          actor + "." + constraint.key + "[" + std::to_string(index) + "]";
      if (table == nullptr) {
        return item_path + " must be a table";
      }
      const auto identity =
          (*table)[constraint.identity_key].value<std::string>();
      if (!identity || identity->empty()) {
        return item_path + "." + constraint.identity_key +
               " must be a non-empty string";
      }
      if (!identities.insert(*identity).second) {
        return actor + "." + constraint.key + " contains duplicate " +
               constraint.identity_key;
      }
      for (const auto &field : constraint.installation_fields) {
        const auto installation = (*table)[field.key].value<std::string>();
        const auto path = item_path + "." + field.key;
        if (!installation) {
          return path + " must name an enabled configured bot";
        }
        if (auto failure = validate_installation(path, *installation,
                                                 field.expected_types)) {
          return failure;
        }
        if (std::ranges::find(constraint.unique_fields, field.key) !=
                constraint.unique_fields.end() &&
            !unique_field_values[field.key].insert(*installation).second) {
          return actor + "." + constraint.key + " contains duplicate " +
                 field.key;
        }
      }
      ++index;
    }
  }

  for (const auto &reference :
       contract.collection_identity_reference_configuration) {
    if (!section) {
      continue;
    }
    const auto target_node = section->at_path(reference.target_collection);
    const auto *target_items = target_node.as_array();
    if (target_items == nullptr) {
      // The collection belongs to an unselected alternative form.
      continue;
    }
    std::unordered_set<std::string> target_identities;
    for (const auto &item : *target_items) {
      const auto *table = item.as_table();
      if (table == nullptr) {
        continue;
      }
      if (const auto identity =
              (*table)[reference.target_identity].value<std::string>()) {
        target_identities.insert(*identity);
      }
    }
    const auto validate_reference =
        [&](const toml::table &source,
            const std::string &path) -> std::optional<std::string> {
      const auto value = source[reference.source_key].value<std::string>();
      if (!value || value->empty()) {
        if (reference.optional && !(reference.required_when_target_multiple &&
                                    target_identities.size() > 1)) {
          return std::nullopt;
        }
        return path + "." + reference.source_key +
               " must reference a configured " + reference.target_collection;
      }
      if (!target_identities.contains(*value)) {
        return path + "." + reference.source_key + " references an unknown " +
               reference.target_collection;
      }
      return std::nullopt;
    };

    if (reference.root_section.empty()) {
      if (auto failure = validate_reference(*section, actor)) {
        return failure;
      }
      continue;
    }
    const auto root = snapshot.get_section(reference.root_section);
    if (!root) {
      continue;
    }
    for (const auto &collection : reference.source_collections) {
      const auto *items = (*root)[collection].as_array();
      if (items == nullptr) {
        continue;
      }
      std::size_t index = 0;
      for (const auto &item : *items) {
        const auto *table = item.as_table();
        const auto path = reference.root_section + "." + collection + "[" +
                          std::to_string(index) + "]";
        if (table == nullptr) {
          return path + " must be a table";
        }
        if (auto failure = validate_reference(*table, path)) {
          return failure;
        }
        ++index;
      }
    }
  }

  for (const auto &relation : contract.less_equal_configuration) {
    if (values.at(relation.lesser) > values.at(relation.greater)) {
      return actor + "." + relation.lesser + " must not exceed " + actor + "." +
             relation.greater;
    }
  }
  return std::nullopt;
}

} // namespace

class RuntimeGeneration::StagingDirectoryOwner {
public:
  explicit StagingDirectoryOwner(fs::path root) : root_(std::move(root)) {}
  ~StagingDirectoryOwner() {
    std::error_code error;
    fs::remove_all(root_, error);
    if (error) {
      OBCX_WARN("Failed to remove generation staging directory {}: {}",
                root_.string(), error.message());
    }
  }

  [[nodiscard]] auto root() const noexcept -> const fs::path & { return root_; }

private:
  fs::path root_;
};

class RuntimeGeneration::RouteState {
public:
  mutable std::mutex mutex;
  std::size_t in_flight = 0;
  bool accepting = true;
  std::vector<std::weak_ptr<boost::asio::steady_timer>> drain_waiters;
};

class RuntimeGeneration::RouteLease {
public:
  explicit RouteLease(std::shared_ptr<RuntimeGeneration> generation)
      : generation_(std::move(generation)) {}

  ~RouteLease() {
    if (generation_) {
      generation_->release_route();
    }
  }

private:
  std::shared_ptr<RuntimeGeneration> generation_;
};

RuntimeGeneration::RuntimeGeneration(
    const std::uint64_t id, RuntimeThreadBudget thread_budget,
    NativeActorSchedulerOptions scheduler_options,
    std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot,
    common::ProcessOwnedConfigFingerprint process_owned_fingerprint,
    std::shared_ptr<DbManager> db_manager,
    std::shared_ptr<BotInstallationDirectory> bot_installation_directory,
    std::shared_ptr<bot::BotOperationGateway> bot_operation_client,
    std::shared_ptr<BlockingExecutor> blocking_executor, fs::path staging_root)
    : staging_owner_(
          std::make_unique<StagingDirectoryOwner>(std::move(staging_root))),
      actor_manager_(std::make_unique<ActorManager>()),
      route_state_(std::make_shared<RouteState>()), id_(id),
      thread_budget_(thread_budget), config_snapshot_(std::move(snapshot)),
      process_owned_fingerprint_(std::move(process_owned_fingerprint)),
      actor_io_pool_(std::make_shared<boost::asio::thread_pool>(
          thread_budget_.io_workers)),
      services_(std::make_shared<ActorServices>()),
      scheduler_(std::make_shared<NativeActorScheduler>(
          std::move(scheduler_options), services_)),
      db_manager_(std::move(db_manager)),
      bot_installation_directory_(std::move(bot_installation_directory)),
      bot_operation_client_(std::move(bot_operation_client)),
      blocking_executor_(std::move(blocking_executor)),
      orchestrator_(std::make_shared<Orchestrator>(scheduler_, services_)),
      staging_root_(staging_owner_->root()) {}

RuntimeGeneration::~RuntimeGeneration() { shutdown(); }

auto RuntimeGeneration::id() const noexcept -> std::uint64_t { return id_; }

auto RuntimeGeneration::thread_budget() const noexcept
    -> const RuntimeThreadBudget & {
  return thread_budget_;
}

auto RuntimeGeneration::config_snapshot() const noexcept
    -> const std::shared_ptr<const common::RuntimeConfigSnapshot> & {
  return config_snapshot_;
}

auto RuntimeGeneration::process_owned_fingerprint() const noexcept
    -> const common::ProcessOwnedConfigFingerprint & {
  return process_owned_fingerprint_;
}

auto RuntimeGeneration::process_owned_dependencies() const noexcept
    -> const std::map<std::string, ProcessOwnedDependencyIdentity> & {
  return process_owned_dependencies_;
}

auto RuntimeGeneration::actor_manager() const noexcept -> ActorManager * {
  return actor_manager_.get();
}

auto RuntimeGeneration::services() const noexcept
    -> const std::shared_ptr<ActorServices> & {
  return services_;
}

auto RuntimeGeneration::scheduler() const noexcept
    -> const std::shared_ptr<NativeActorScheduler> & {
  return scheduler_;
}

auto RuntimeGeneration::orchestrator() const noexcept
    -> const std::shared_ptr<Orchestrator> & {
  return orchestrator_;
}

auto RuntimeGeneration::db_manager() const noexcept
    -> const std::shared_ptr<DbManager> & {
  return db_manager_;
}

auto RuntimeGeneration::bot_installation_directory() const noexcept
    -> const std::shared_ptr<BotInstallationDirectory> & {
  return bot_installation_directory_;
}

auto RuntimeGeneration::bot_operation_client() const noexcept
    -> const std::shared_ptr<bot::BotOperationGateway> & {
  return bot_operation_client_;
}

auto RuntimeGeneration::blocking_executor() const noexcept
    -> const std::shared_ptr<BlockingExecutor> & {
  return blocking_executor_;
}

auto RuntimeGeneration::command_routing_table() const noexcept
    -> const std::shared_ptr<const CommandRoutingTable> & {
  return command_routing_table_;
}

void RuntimeGeneration::activate_actor_background_work() {
  if (shutdown_.load(std::memory_order_acquire)) {
    return;
  }
  const auto info = services_->get_service<ActorGenerationInfo>();
  if (!info || info->purpose == ActorGenerationPurpose::ValidationOnly) {
    return;
  }
  if (auto lifecycle = services_->get_service<ActorGenerationLifecycle>()) {
    lifecycle->activate();
  }
}

void RuntimeGeneration::invalidate_actor_background_work() noexcept {
  if (auto lifecycle = services_->get_service<ActorGenerationLifecycle>()) {
    lifecycle->invalidate();
  }
}

void RuntimeGeneration::activate_command_catalogs() {
  bool expected = false;
  if (!command_catalog_active_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel) ||
      shutdown_.load(std::memory_order_acquire) || !command_routing_table_) {
    return;
  }

  std::vector<CommandBotKey> publish;
  {
    std::scoped_lock lock(command_catalog_mutex_);
    command_catalog_status_.clear();
    for (const auto &[key, bot] : command_routing_table_->bots()) {
      auto &status = command_catalog_status_[key];
      status.key = key;
      status.desired_generation = id_;
      status.publication_supported =
          bot.adapter->supports_catalog_publication();
      status.desired = bot.catalog;
      if (status.publication_supported) {
        publish.push_back(key);
      } else {
        status.last_success_generation = id_;
        status.observed = bot.catalog;
      }
    }
  }

  for (auto &key : publish) {
    boost::asio::co_spawn(actor_io_pool_->get_executor(),
                          reconcile_command_catalog(std::move(key)),
                          boost::asio::detached);
  }
}

auto RuntimeGeneration::command_catalog_status() const
    -> std::vector<CommandCatalogStatus> {
  std::scoped_lock lock(command_catalog_mutex_);
  std::vector<CommandCatalogStatus> result;
  result.reserve(command_catalog_status_.size());
  for (const auto &[key, status] : command_catalog_status_) {
    (void)key;
    result.push_back(status);
  }
  return result;
}

auto RuntimeGeneration::reconcile_command_catalog(CommandBotKey key)
    -> boost::asio::awaitable<void> {
  constexpr std::size_t max_attempts = 3;
  const auto bot_binding = command_routing_table_->find_bot(key);
  if (bot_binding == nullptr || !bot_binding->adapter) {
    co_return;
  }

  for (std::size_t attempt = 1; attempt <= max_attempts; ++attempt) {
    if (!command_catalog_active_.load(std::memory_order_acquire) ||
        shutdown_.load(std::memory_order_acquire)) {
      co_return;
    }

    CommandCatalogPublishResult published{
        .supported = true,
        .succeeded = false,
        .code = "command_catalog_bot_unavailable",
        .message = "configured live bot is unavailable",
    };
    std::shared_ptr<CommandCatalogPublisher> catalog;
    if (bot_installation_directory_ != nullptr) {
      catalog = bot_installation_directory_->command_catalog_publisher(
          bot_binding->installation);
    }
    published = co_await bot_binding->adapter->publish_catalog(
        catalog.get(), bot_binding->catalog);

    {
      std::scoped_lock lock(command_catalog_mutex_);
      auto status = command_catalog_status_.find(key);
      if (status == command_catalog_status_.end()) {
        co_return;
      }
      status->second.last_attempted_generation = id_;
      status->second.attempts = attempt;
      status->second.retries = attempt - 1;
      status->second.failure_code =
          published.succeeded ? std::string{} : published.code;
      if (published.succeeded) {
        status->second.last_success_generation = id_;
        status->second.observed = bot_binding->catalog;
      }
    }
    if (published.succeeded) {
      co_return;
    }
    if (attempt == max_attempts ||
        !command_catalog_active_.load(std::memory_order_acquire)) {
      co_return;
    }

    boost::asio::steady_timer retry_timer{
        co_await boost::asio::this_coro::executor,
        std::chrono::milliseconds{50U << (attempt - 1U)}};
    boost::system::error_code error;
    co_await retry_timer.async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, error));
  }
}

auto RuntimeGeneration::staging_root() const noexcept -> const fs::path & {
  return staging_root_;
}

auto RuntimeGeneration::admit_route() -> RouteAdmission {
  auto self = shared_from_this();
  {
    std::scoped_lock lock(route_state_->mutex);
    if (!route_state_->accepting || shutdown_.load(std::memory_order_acquire)) {
      return {};
    }
    ++route_state_->in_flight;
  }

  try {
    return std::make_shared<RouteLease>(std::move(self));
  } catch (...) {
    release_route();
    throw;
  }
}

auto RuntimeGeneration::process(MessageEnvelope message,
                                RouteAdmission admission)
    -> boost::asio::awaitable<OrchestratorResult> {
  if (!admission) {
    OrchestratorResult result;
    result.failures.push_back(OrchestratorFailure{
        .failure = ActorFailure{.code = "reload_route_not_admitted",
                                .message = "actor route was not admitted",
                                .retryable = true}});
    co_return result;
  }
  if (command_coordinator_) {
    co_return co_await command_coordinator_->process(std::move(message),
                                                     std::move(admission));
  }
  co_return co_await orchestrator_->process(std::move(message),
                                            std::move(admission));
}

auto RuntimeGeneration::async_wait_for_drain(
    const std::chrono::steady_clock::time_point deadline)
    -> boost::asio::awaitable<bool> {
  auto executor = co_await boost::asio::this_coro::executor;

  for (;;) {
    auto timer = std::make_shared<boost::asio::steady_timer>(executor);
    timer->expires_at(deadline);
    {
      std::scoped_lock lock(route_state_->mutex);
      if (route_state_->in_flight == 0) {
        co_return true;
      }
      std::erase_if(route_state_->drain_waiters,
                    [](const auto &waiter) { return waiter.expired(); });
      route_state_->drain_waiters.push_back(timer);
    }

    boost::system::error_code error;
    co_await timer->async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, error));

    {
      std::scoped_lock lock(route_state_->mutex);
      if (route_state_->in_flight == 0) {
        co_return true;
      }
    }
    if (!error || std::chrono::steady_clock::now() >= deadline) {
      co_return false;
    }
  }
}

auto RuntimeGeneration::in_flight_routes() const noexcept -> std::size_t {
  std::scoped_lock lock(route_state_->mutex);
  return route_state_->in_flight;
}

void RuntimeGeneration::release_route() noexcept {
  std::vector<std::shared_ptr<boost::asio::steady_timer>> waiters;
  {
    std::scoped_lock lock(route_state_->mutex);
    if (route_state_->in_flight == 0) {
      return;
    }
    --route_state_->in_flight;
    if (route_state_->in_flight != 0) {
      return;
    }
    for (auto &weak_waiter : route_state_->drain_waiters) {
      if (auto waiter = weak_waiter.lock()) {
        waiters.push_back(std::move(waiter));
      }
    }
    route_state_->drain_waiters.clear();
  }
  for (const auto &waiter : waiters) {
    try {
      boost::asio::post(waiter->get_executor(), [waiter] { waiter->cancel(); });
    } catch (...) {
      // The waiter rechecks route state after every wakeup. An unavailable
      // executor cannot run the waiting coroutine again.
    }
  }
}

void RuntimeGeneration::shutdown() {
  bool expected = false;
  if (!shutdown_.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
    return;
  }
  {
    std::scoped_lock lock(route_state_->mutex);
    route_state_->accepting = false;
  }
  command_catalog_active_.store(false, std::memory_order_release);
  if (auto lifecycle = services_->get_service<ActorGenerationLifecycle>()) {
    lifecycle->retire();
  }
  if (command_coordinator_) {
    command_coordinator_->shutdown();
  }
  if (orchestrator_) {
    orchestrator_->shutdown();
  }
  if (scheduler_) {
    scheduler_->release_actors();
  }
  if (actor_manager_) {
    // Destroy idle actor instances so their destructors can cancel
    // generation-owned timers, but retain every DSO until all callback and
    // blocking completion bridges have retired.
    actor_manager_->begin_actor_retirement();
  }
  if (actor_io_pool_) {
    // Drain actor-destructor callbacks and any completion bridge waiting on
    // already-admitted blocking work before unloading actor code.
    actor_io_pool_->join();
  }
  if (actor_manager_) {
    actor_manager_->finish_actor_retirement();
  }
}

RuntimeGenerationBuilder::RuntimeGenerationBuilder(
    std::shared_ptr<const BotPlatformCatalog> catalog)
    : catalog_(std::move(catalog)),
      restart_constraints_(std::make_shared<ActorRestartConstraintRegistry>()) {
  if (!catalog_ || !catalog_->sealed()) {
    throw std::invalid_argument(
        "generation builder requires an explicit sealed platform catalog");
  }
}

auto RuntimeGenerationBuilder::parse_config(
    const std::string &config_path) const -> common::RuntimeConfigBuildResult {
  return common::ConfigLoader::build_snapshot(config_path, catalog_);
}

auto RuntimeGenerationBuilder::build(RuntimeGenerationBuildRequest request)
    const -> RuntimeGenerationBuildResult {
  if (!request.snapshot) {
    return failed("reload_parse_failed", "configuration snapshot is missing");
  }

  try {
    if (ProcessConfigAccess::catalog(*request.snapshot) != catalog_) {
      return failed("reload_configuration_catalog_mismatch",
                    "snapshot belongs to a different process platform catalog");
    }
  } catch (const std::invalid_argument &) {
    return failed("reload_process_configuration_required",
                  "actor-only context is not validated process configuration");
  }

  auto errors = request.snapshot->validate_actor_runtime_config();
  auto pipeline_errors = request.snapshot->validate_actor_pipeline_configs();
  errors.insert(errors.end(), std::make_move_iterator(pipeline_errors.begin()),
                std::make_move_iterator(pipeline_errors.end()));
  if (!errors.empty()) {
    return failed(errors.front().code.empty() ? "reload_config_invalid"
                                              : errors.front().code,
                  "actor configuration validation failed");
  }

  const DbManager db_validator;
  if (!db_validator
           .validate_configs(request.snapshot->get_db_instance_configs())
           .empty()) {
    return failed("reload_database_invalid",
                  "database configuration validation failed");
  }

  const auto runtime_config = request.snapshot->get_actor_runtime_config();
  const auto thread_budget =
      resolve_runtime_thread_budget(RuntimeThreadBudgetRequest{
          .actor_workers = runtime_config.workers,
          .io_workers = std::max<std::size_t>(1, request.configured_io_sources),
          .blocking_workers = runtime_config.blocking_workers,
      });
  const auto process_fingerprint = request.snapshot->process_owned_fingerprint(
      common::RuntimeThreadFingerprintInput{
          .actor_workers = thread_budget.actor_workers,
          .io_workers = thread_budget.io_workers,
          .blocking_workers = thread_budget.blocking_workers,
      });
  if (request.active_process_owned_fingerprint.has_value() &&
      process_fingerprint != *request.active_process_owned_fingerprint) {
    return failed(
        "reload_restart_required",
        common::describe_process_owned_changes(
            *request.active_process_owned_fingerprint, process_fingerprint));
  }

  auto actor_configs = request.snapshot->get_actor_configs();
  auto pipelines = request.snapshot->get_pipeline_configs();
  const auto command_runtime = request.snapshot->get_command_runtime_config();
  const auto has_command_runtime = !command_runtime.routes.empty() ||
                                   !command_runtime.message_observers.empty();
  std::erase_if(actor_configs,
                [](const auto &actor) { return !actor.enabled; });
  if (actor_configs.empty() && pipelines.empty() && !has_command_runtime) {
    return {.status = RuntimeGenerationBuildStatus::NotConfigured};
  }
  if (actor_configs.empty() || (pipelines.empty() && !has_command_runtime)) {
    return failed(
        "reload_actor_graph_invalid",
        "actor runtime requires enabled actors and either pipelines, command "
        "routes, or command message observers");
  }
  if (!request.db_manager) {
    return failed("reload_process_service_missing",
                  "process-owned runtime services are missing");
  }
  if (!request.bot_operation_client) {
    if (request.purpose == RuntimeGenerationBuildPurpose::ReloadCandidate) {
      return failed(
          "reload_process_service_missing",
          "process operation gateway is missing for reload candidate");
    }
    // Validation/startup without live installations uses an explicitly empty
    // gateway, never a guessed provider or a newly created transport.
    auto empty_gateway = std::make_shared<BotOperationDispatcher>(
        [](const bot::SurfaceId &) { return false; });
    empty_gateway->seal_registrations();
    request.bot_operation_client = std::move(empty_gateway);
  }
  if (request.purpose == RuntimeGenerationBuildPurpose::ReloadCandidate &&
      !request.blocking_executor) {
    return failed("reload_process_service_missing",
                  "process blocking executor is missing for reload candidate");
  }
  if (request.purpose == RuntimeGenerationBuildPurpose::ValidationOnly) {
    request.blocking_executor.reset();
  }

  auto ordered = ordered_actors(std::move(actor_configs));
  if (!ordered) {
    return failed("actor_dependency_cycle", "actor dependency ordering failed");
  }

  NativeActorSchedulerOptions scheduler_options;
  scheduler_options.use_global_sharing =
      runtime_config.policy == common::ActorSchedulerPolicy::Sharing;
  scheduler_options.worker_count = thread_budget.actor_workers;
  scheduler_options.slow_resume_warning_ms =
      runtime_config.slow_resume_warning_ms;

  const auto staging_root =
      generation_staging_root(request.staging_root, request.generation_id);
  std::error_code staging_error;
  const auto staging_created =
      fs::create_directories(staging_root, staging_error);
  if (staging_error || !staging_created) {
    return failed("reload_staging_failed",
                  "generation staging directory cannot be created");
  }

  if (request.purpose == RuntimeGenerationBuildPurpose::Startup &&
      !request.blocking_executor) {
    request.blocking_executor =
        std::make_shared<BlockingExecutor>(thread_budget.blocking_workers);
  }

  auto generation = std::shared_ptr<RuntimeGeneration>(new RuntimeGeneration{
      request.generation_id, thread_budget, scheduler_options, request.snapshot,
      process_fingerprint, std::move(request.db_manager),
      std::move(request.bot_installation_directory),
      std::move(request.bot_operation_client),
      std::move(request.blocking_executor), staging_root});

  ActorPackageStager stager;
  std::unordered_map<std::string, std::unordered_set<std::string>> actor_inputs;
  std::unordered_map<std::string, ActorInputContract> actor_contracts;
  for (const auto &actor : *ordered) {
    const auto library_name =
        actor.library.empty() ? actor.name : actor.library;
    const auto library =
        resolve_actor_library(library_name, request.actor_search_directories);
    if (library.empty()) {
      return failed("reload_actor_unavailable",
                    "configured actor library cannot be resolved: " +
                        actor.name);
    }
    auto staged = stager.stage({.actor_library = library,
                                .staging_root = staging_root,
                                .actor_name = actor.name,
                                .generation_id = request.generation_id,
                                .expected_process_owned_dependencies =
                                    request.active_process_owned_dependencies});
    if (!staged) {
      return failed(staged.code, std::move(staged.message));
    }
    for (const auto &[name, identity] :
         staged.package->process_owned_dependencies()) {
      if (!merge_process_dependency(generation->process_owned_dependencies_,
                                    name, identity)) {
        return failed("reload_dependency_identity_conflict",
                      "process-owned dependency identity conflict: " + name);
      }
    }
    if (!generation->actor_manager_->discover_actor_from_path(
            staged.package->actor_library().string())) {
      return failed("reload_contract_invalid",
                    "actor contract discovery failed for " + actor.name);
    }
    const auto *contract =
        generation->actor_manager_->get_actor_contract(actor.name);
    if (contract == nullptr) {
      return failed("reload_contract_invalid",
                    "actor contract identity does not match " + actor.name);
    }
    if (const auto configuration_error = validate_actor_configuration(
            *request.snapshot, *catalog_, actor.name, *contract)) {
      return failed("reload_actor_config_invalid", *configuration_error);
    }
    actor_inputs.emplace(actor.name, contract->accepted_input_set);
    actor_contracts.emplace(actor.name, *contract);
    generation->staged_packages_.push_back(std::move(staged.package));
  }

  auto command_table =
      build_command_routing_table(*request.snapshot, actor_contracts);
  if (!command_table) {
    return failed(command_table.failure->code,
                  std::move(command_table.failure->message));
  }
  generation->command_routing_table_ = std::move(command_table.table);

  if (!request.snapshot->validate_actor_pipeline_contracts(actor_inputs)
           .empty()) {
    return failed("reload_contract_invalid",
                  "actor pipeline contract validation failed");
  }

  if (request.require_registered_bots) {
    for (const auto &bot : request.snapshot->get_bot_configs()) {
      if (!bot.enabled) {
        continue;
      }
      const auto &surface = bot.surface;
      const auto &identity = bot.installation_id;
      const auto available =
          generation->bot_installation_directory_ != nullptr &&
          static_cast<bool>(generation->bot_installation_directory_->endpoint(
              {.installation_id = identity, .surface = surface}));
      if (!available) {
        return failed("reload_bot_unavailable",
                      "configured live bot is unavailable: " + identity);
      }
    }
  }

  const auto actor_generation_purpose = [&] {
    switch (request.purpose) {
    case RuntimeGenerationBuildPurpose::Startup:
      return ActorGenerationPurpose::Startup;
    case RuntimeGenerationBuildPurpose::ValidationOnly:
      return ActorGenerationPurpose::ValidationOnly;
    case RuntimeGenerationBuildPurpose::ReloadCandidate:
      return ActorGenerationPurpose::ReloadCandidate;
    }
    return ActorGenerationPurpose::Startup;
  }();
  generation->orchestrator_->register_service<ActorGenerationInfo>(
      std::make_shared<ActorGenerationInfo>(ActorGenerationInfo{
          .purpose = actor_generation_purpose,
          .generation_id = request.generation_id,
      }));
  generation->orchestrator_->register_service<ActorGenerationLifecycle>(
      std::make_shared<ActorGenerationLifecycle>(
          [weak = std::weak_ptr<RuntimeGeneration>(
               generation)] -> ActorGenerationLifecycle::WorkLease {
            if (auto active = weak.lock()) {
              return active->admit_route();
            }
            return {};
          }));
  generation->orchestrator_->register_service<ActorRestartConstraintRegistry>(
      restart_constraints_);
  generation->orchestrator_->register_service<DbManager>(
      generation->db_manager_);
  generation->orchestrator_->register_service<bot::BotOperationGateway>(
      generation->bot_operation_client_);
  generation->orchestrator_->register_service<common::ActorConfigService>(
      std::make_shared<common::ActorConfigService>(request.snapshot));
  generation->orchestrator_->register_service<boost::asio::any_io_executor>(
      std::make_shared<boost::asio::any_io_executor>(
          generation->actor_io_pool_->get_executor()));
  if (generation->blocking_executor_) {
    generation->orchestrator_->register_service<BlockingExecutor>(
        generation->blocking_executor_);
  }

  for (const auto &actor : *ordered) {
    if (!generation->actor_manager_->activate_actor(actor.name)) {
      return failed("reload_activation_failed",
                    "actor construction failed for " + actor.name);
    }
    ActorContext preparation_context(actor.name, generation->services_,
                                     actor.db, actor.db_namespace);
    const auto preparation = generation->actor_manager_->prepare_actor(
        actor.name, preparation_context);
    if (!preparation.ok()) {
      const auto code =
          preparation.status == ActorPreparationStatus::RestartRequired
              ? "reload_restart_required"
              : "reload_actor_initialization_failed";
      return failed(
          code, "actor " + actor.name +
                    " generation preparation failed: " + preparation.message);
    }
    auto instance = generation->actor_manager_->get_actor_shared(actor.name);
    if (!instance) {
      return failed("reload_activation_failed",
                    "constructed actor is unavailable: " + actor.name);
    }
    try {
      generation->orchestrator_->register_actor(std::move(instance));
    } catch (...) {
      return failed("reload_activation_failed",
                    "actor scheduler registration failed: " + actor.name);
    }
  }

  generation->orchestrator_->configure_actors(std::move(*ordered));
  generation->orchestrator_->configure_pipelines(std::move(pipelines));
  generation->orchestrator_->set_routing_hop_limit(
      runtime_config.routing_hop_limit);
  generation->command_coordinator_ = std::make_shared<CommandCoordinator>(
      generation->id_, generation->command_routing_table_,
      generation->scheduler_, generation->orchestrator_,
      generation->bot_operation_client_);
  OBCX_INFO("Runtime generation {} ready with {} actors", request.generation_id,
            generation->actor_manager_->get_loaded_actor_names().size());
  return {.status = RuntimeGenerationBuildStatus::Ready,
          .generation = std::move(generation)};
}

} // namespace obcx::core
