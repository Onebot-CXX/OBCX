#ifndef OBCX_INCLUDE_COMMON_CONFIG_SNAPSHOT_HPP_
#define OBCX_INCLUDE_COMMON_CONFIG_SNAPSHOT_HPP_

#include "common/bot_installation_metadata.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace obcx::core {
struct ProcessConfiguration;
class ProcessConfigAccess;
} // namespace obcx::core

namespace obcx::common {

struct ActorConfig {
  std::string name;
  std::string library;
  bool enabled{};
  std::vector<std::string> required; // enabled actors that must activate first
  std::string partition = "global";
  std::string db;
  std::string db_namespace;
};

struct DbInstanceConfig {
  std::string name;
  std::string type;
  std::string path;
  std::string dsn;
  toml::table config;
};

struct PipelineStageConfig {
  std::string name;
  std::string actor;
  std::string input;
  std::vector<std::string> outputs;
  std::vector<std::string> after;
  std::string mode;
};

struct PipelineConfig {
  std::string name;
  std::string source;
  std::vector<PipelineStageConfig> stages;
};

enum class CommandFallback {
  Continue,
  Consume,
};

struct CommandRouteConfig {
  std::string actor;
  std::vector<std::string> commands;
  std::vector<std::string> platforms;
  std::vector<std::string> bots;
  CommandFallback fallback = CommandFallback::Continue;
  size_t timeout_ms = 0; // 0 = inherit command_runtime.timeout_ms
};

struct CommandMessageObserverConfig {
  std::string actor;
  std::vector<std::string> platforms;
  std::vector<std::string> bots;
  size_t timeout_ms = 0;
};

enum class CommandAccessMode {
  Unrestricted,
  Allowlist,
  Denylist,
};

struct CommandGroupIdentityConfig {
  std::string platform;
  std::string bot;
  std::string native_group_id;

  auto operator==(const CommandGroupIdentityConfig &) const -> bool = default;
};

struct CommandUserIdentityConfig {
  std::string platform;
  std::string bot;
  std::string native_user_id;

  auto operator==(const CommandUserIdentityConfig &) const -> bool = default;
};

struct CommandGroupPolicyConfig {
  std::optional<CommandAccessMode> mode;
  std::vector<CommandGroupIdentityConfig> entries;
  bool entries_present = false;
};

struct CommandUserPolicyConfig {
  std::optional<CommandAccessMode> mode;
  std::vector<CommandUserIdentityConfig> entries;
  bool entries_present = false;
};

struct CommandAccessOverrideConfig {
  std::string command;
  CommandGroupPolicyConfig groups;
  CommandUserPolicyConfig users;
};

struct CommandHelpConfig {
  std::optional<size_t> page_bytes;
  std::optional<size_t> maximum_pages;
};

struct CommandAccessConfig {
  CommandGroupPolicyConfig groups;
  CommandUserPolicyConfig users;
  std::vector<CommandAccessOverrideConfig> overrides;
  bool overrides_present = false;
};

struct CommandRuntimeConfig {
  static constexpr size_t default_timeout_ms = 5'000;
  static constexpr size_t min_timeout_ms = 100;
  static constexpr size_t max_timeout_ms = 300'000;
  static constexpr size_t max_help_page_bytes = 65'536;
  static constexpr size_t max_help_pages = 100;
  static constexpr size_t max_access_entries = 10'000;
  static constexpr size_t max_access_overrides = 1'024;
  static constexpr size_t max_message_observers = 1'024;

  size_t timeout_ms = default_timeout_ms;
  CommandHelpConfig help;
  CommandAccessConfig access;
  std::vector<CommandMessageObserverConfig> message_observers;
  std::vector<CommandRouteConfig> routes;
};

enum class ActorSchedulerPolicy {
  Stealing,
  Sharing,
};

struct ActorRuntimeConfig {
  static constexpr size_t default_reload_drain_timeout_ms = 5'000;
  static constexpr size_t min_reload_drain_timeout_ms = 100;
  static constexpr size_t max_reload_drain_timeout_ms = 300'000;

  ActorSchedulerPolicy policy = ActorSchedulerPolicy::Stealing;
  size_t workers = 0;          // 0 = selected from the runtime thread budget
  size_t blocking_workers = 0; // 0 = selected from the runtime thread budget
  size_t slow_resume_warning_ms = 10;
  size_t routing_hop_limit = 32;
  size_t reload_drain_timeout_ms = default_reload_drain_timeout_ms;
};

struct ConfigValidationError {
  std::string code;
  std::string message;
  std::string pipeline;
  std::string stage;
  std::string actor;
  std::string dependency;
  std::string input;
};

struct RuntimeThreadFingerprintInput {
  size_t actor_workers = 0;
  size_t io_workers = 0;
  size_t blocking_workers = 0;

  auto operator==(const RuntimeThreadFingerprintInput &) const
      -> bool = default;
};

struct ProcessOwnedConfigFingerprint {
  std::string bots;
  std::string database_instances;
  RuntimeThreadFingerprintInput thread_budget;

  auto operator==(const ProcessOwnedConfigFingerprint &) const
      -> bool = default;
};

struct ConfigLoadDiagnostic {
  std::string code;
  std::string path;
  std::string message;
  size_t line = 0;
  size_t column = 0;
};

class RuntimeConfigSnapshot {
public:
  ~RuntimeConfigSnapshot() = default;
  RuntimeConfigSnapshot(const RuntimeConfigSnapshot &) = delete;
  auto operator=(const RuntimeConfigSnapshot &)
      -> RuntimeConfigSnapshot & = delete;
  RuntimeConfigSnapshot(RuntimeConfigSnapshot &&) = delete;
  auto operator=(RuntimeConfigSnapshot &&) -> RuntimeConfigSnapshot & = delete;

  [[nodiscard]] auto get_bot_configs() const
      -> std::vector<BotInstallationMetadata>;
  [[nodiscard]] auto get_actor_configs() const -> std::vector<ActorConfig>;
  [[nodiscard]] auto get_db_instance_configs() const
      -> std::vector<DbInstanceConfig>;
  [[nodiscard]] auto get_pipeline_configs() const
      -> std::vector<PipelineConfig>;
  [[nodiscard]] auto get_command_runtime_config() const -> CommandRuntimeConfig;
  [[nodiscard]] auto get_actor_runtime_config() const -> ActorRuntimeConfig;
  [[nodiscard]] auto validate_actor_runtime_config() const
      -> std::vector<ConfigValidationError>;
  [[nodiscard]] auto validate_actor_pipeline_configs() const
      -> std::vector<ConfigValidationError>;
  [[nodiscard]] auto validate_actor_pipeline_contracts(
      const std::unordered_map<std::string, std::unordered_set<std::string>>
          &actor_inputs) const -> std::vector<ConfigValidationError>;

  template <typename T>
  [[nodiscard]] auto get_value(std::string_view key) const -> std::optional<T> {
    const auto node = config_data_.at_path(key);
    if (!node) {
      return std::nullopt;
    }
    return node.template value<T>();
  }

  template <typename T>
  [[nodiscard]] auto get_actor_value(std::string_view actor,
                                     std::string_view key) const
      -> std::optional<T> {
    const auto *actors = config_data_.get_as<toml::table>("actors");
    const auto *actor_table =
        actors == nullptr ? nullptr : actors->get_as<toml::table>(actor);
    const auto *actor_config = actor_table == nullptr
                                   ? nullptr
                                   : actor_table->get_as<toml::table>("config");
    if (actor_config == nullptr) {
      return std::nullopt;
    }
    const auto node = actor_config->at_path(key);
    if (!node) {
      return std::nullopt;
    }
    return node.template value<T>();
  }

  [[nodiscard]] auto get_section(std::string_view section_name) const
      -> std::optional<toml::table>;
  [[nodiscard]] auto get_actor_section(std::string_view actor,
                                       std::string_view section_name = {}) const
      -> std::optional<toml::table>;
  [[nodiscard]] auto process_owned_fingerprint(
      RuntimeThreadFingerprintInput thread_budget) const
      -> ProcessOwnedConfigFingerprint;
  [[nodiscard]] auto config_path() const noexcept -> const std::string & {
    return config_path_;
  }

private:
  friend class ConfigLoader;
  friend class ActorConfigSnapshotBuilder;
  friend class core::ProcessConfigAccess;

  RuntimeConfigSnapshot(
      std::string config_path, toml::table config_data,
      std::vector<BotInstallationMetadata> bots,
      std::shared_ptr<const core::ProcessConfiguration> process_configuration)
      : config_path_(std::move(config_path)),
        config_data_(std::move(config_data)), bots_(std::move(bots)),
        process_configuration_(std::move(process_configuration)) {}

  std::string config_path_;
  toml::table config_data_;
  std::vector<BotInstallationMetadata> bots_;
  std::shared_ptr<const core::ProcessConfiguration> process_configuration_;
};

struct RuntimeConfigBuildResult {
  std::shared_ptr<const RuntimeConfigSnapshot> snapshot;
  std::optional<ConfigLoadDiagnostic> diagnostic;

  explicit operator bool() const noexcept { return snapshot != nullptr; }
};

// Explicit data-only SDK entry point, not a process configuration loader.
// The document must not contain bots; supply their non-secret metadata
// separately.
class ActorConfigSnapshotBuilder {
public:
  [[nodiscard]] static auto build(toml::table actor_document,
                                  std::vector<BotInstallationMetadata> bots,
                                  std::string config_path)
      -> RuntimeConfigBuildResult;
};

class ActorConfigView {
public:
  ActorConfigView() = default;
  ActorConfigView(std::shared_ptr<const RuntimeConfigSnapshot> snapshot,
                  std::string actor)
      : snapshot_(std::move(snapshot)), actor_(std::move(actor)) {}

  [[nodiscard]] auto available() const noexcept -> bool {
    return snapshot_ != nullptr;
  }
  [[nodiscard]] auto actor() const noexcept -> const std::string & {
    return actor_;
  }

  template <typename T>
  [[nodiscard]] auto get_value(std::string_view key) const -> std::optional<T> {
    return snapshot_ == nullptr
               ? std::nullopt
               : snapshot_->template get_actor_value<T>(actor_, key);
  }

  [[nodiscard]] auto get_section(std::string_view section_name = {}) const
      -> std::optional<toml::table> {
    return snapshot_ == nullptr
               ? std::nullopt
               : snapshot_->get_actor_section(actor_, section_name);
  }

  [[nodiscard]] auto get_root_section(std::string_view section_name) const
      -> std::optional<toml::table> {
    return snapshot_ == nullptr ? std::nullopt
                                : snapshot_->get_section(section_name);
  }

private:
  std::shared_ptr<const RuntimeConfigSnapshot> snapshot_;
  std::string actor_;
};

class ActorConfigService {
public:
  explicit ActorConfigService(
      std::shared_ptr<const RuntimeConfigSnapshot> snapshot)
      : snapshot_(std::move(snapshot)) {}

  [[nodiscard]] auto for_actor(std::string actor) const -> ActorConfigView {
    return ActorConfigView{snapshot_, std::move(actor)};
  }

private:
  std::shared_ptr<const RuntimeConfigSnapshot> snapshot_;
};

[[nodiscard]] auto changed_process_owned_domains(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate) -> std::vector<std::string>;
[[nodiscard]] auto describe_process_owned_changes(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate) -> std::string;

} // namespace obcx::common

#endif // OBCX_INCLUDE_COMMON_CONFIG_SNAPSHOT_HPP_
