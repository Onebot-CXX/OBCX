#ifndef OBCX_INCLUDE_CORE_PROCESS_CONFIGURATION_HPP_
#define OBCX_INCLUDE_CORE_PROCESS_CONFIGURATION_HPP_

#include "common/config_snapshot.hpp"
#include "core/bot/platform_catalog.hpp"
#include <atomic>

namespace obcx::core {

struct ProcessConfiguration {
  const std::shared_ptr<const BotPlatformCatalog> catalog;
  const std::vector<std::shared_ptr<const BotInstallationPlan>> plans;
  const std::string bots_fingerprint;
};

class ProcessConfigAccess {
public:
  [[nodiscard]] static auto plans(const common::RuntimeConfigSnapshot &snapshot)
      -> const std::vector<std::shared_ptr<const BotInstallationPlan>> &;
  [[nodiscard]] static auto catalog(
      const common::RuntimeConfigSnapshot &snapshot)
      -> const std::shared_ptr<const BotPlatformCatalog> &;
};

} // namespace obcx::core

namespace obcx::common {

class ConfigLoader {
public:
  explicit ConfigLoader(
      std::shared_ptr<const core::BotPlatformCatalog> catalog);
  ~ConfigLoader() = default;

  ConfigLoader(const ConfigLoader &) = delete;
  auto operator=(const ConfigLoader &) -> ConfigLoader & = delete;
  ConfigLoader(ConfigLoader &&) = delete;
  auto operator=(ConfigLoader &&) -> ConfigLoader & = delete;

  [[nodiscard]] static auto build_snapshot(
      const std::string &config_path,
      std::shared_ptr<const core::BotPlatformCatalog> catalog)
      -> RuntimeConfigBuildResult;
  void publish_snapshot(std::shared_ptr<const RuntimeConfigSnapshot> snapshot);
  [[nodiscard]] auto current_snapshot() const noexcept
      -> std::shared_ptr<const RuntimeConfigSnapshot>;

  auto load_config(const std::string &config_path) -> bool;

  auto get_bot_configs() const -> std::vector<BotInstallationMetadata>;

  auto get_actor_configs() const -> std::vector<ActorConfig>;

  auto get_db_instance_configs() const -> std::vector<DbInstanceConfig>;

  auto get_pipeline_configs() const -> std::vector<PipelineConfig>;

  auto get_command_runtime_config() const -> CommandRuntimeConfig;

  auto get_actor_runtime_config() const -> ActorRuntimeConfig;

  auto validate_actor_runtime_config() const
      -> std::vector<ConfigValidationError>;

  auto validate_actor_pipeline_configs() const
      -> std::vector<ConfigValidationError>;

  auto validate_actor_pipeline_contracts(
      const std::unordered_map<std::string, std::unordered_set<std::string>>
          &actor_inputs) const -> std::vector<ConfigValidationError>;

  template <typename T>
  auto get_value(const std::string &key) const -> std::optional<T> {
    const auto snapshot = current_snapshot();
    return snapshot == nullptr ? std::nullopt
                               : snapshot->template get_value<T>(key);
  }

  auto get_section(const std::string &section_name) const
      -> std::optional<toml::table>;

  auto reload_config() -> bool;

  [[nodiscard]] auto is_loaded() const noexcept -> bool {
    return current_snapshot() != nullptr;
  }

  [[nodiscard]] auto get_config_path() const -> std::string;

private:
  const std::shared_ptr<const core::BotPlatformCatalog> catalog_;
  std::atomic<std::shared_ptr<const RuntimeConfigSnapshot>> active_snapshot_;
};

} // namespace obcx::common

#endif
