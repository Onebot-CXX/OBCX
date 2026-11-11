#include "core/runtime/process_configuration.hpp"
#include "common/bot_metadata_document.hpp"
#include "common/logger.hpp"
#include "core/bot/configuration_fingerprint.hpp"
#include "core/bot/configuration_validation.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>

namespace obcx::core {

auto ProcessConfigAccess::plans(const common::RuntimeConfigSnapshot &snapshot)
    -> const std::vector<std::shared_ptr<const BotInstallationPlan>> & {
  if (!snapshot.process_configuration_) {
    throw std::invalid_argument(
        "actor-only snapshot is not validated process configuration");
  }
  return snapshot.process_configuration_->plans;
}

auto ProcessConfigAccess::catalog(const common::RuntimeConfigSnapshot &snapshot)
    -> const std::shared_ptr<const BotPlatformCatalog> & {
  (void)plans(snapshot);
  return snapshot.process_configuration_->catalog;
}

} // namespace obcx::core

namespace obcx::common {
namespace {

constexpr std::array<std::string_view, 2> kBotSecretFields = {"access_token",
                                                              "proxy_password"};

auto load_secrets(const toml::table &document, const std::string &config_path)
    -> std::shared_ptr<const SecretStore> {
  using core::configuration::bot_configuration_error;
  const auto *global = document.get_as<toml::table>("global");
  const auto *file =
      global == nullptr ? nullptr : global->get_as<std::string>("secrets_file");
  if (file == nullptr || file->get().empty()) {
    bot_configuration_error(
        "missing_secrets_file", "global.secrets_file",
        "global.secrets_file must name the secrets file explicitly");
  }
  std::filesystem::path path{file->get()};
  if (path.is_relative()) {
    path = std::filesystem::absolute(config_path).parent_path() / path;
  }
  return std::make_shared<const SecretStore>(SecretStore::load(path));
}

auto resolve_secret(const toml::node *node, const SecretStore &secrets,
                    const std::string &field) -> std::string {
  using core::configuration::bot_configuration_error;
  const auto name = secret_reference_name(node);
  if (!name) {
    bot_configuration_error("inline_secret_rejected", field,
                            field +
                                " must be a { secret = \"<name>\" } reference");
  }
  const auto *value = secrets.find(*name);
  if (value == nullptr) {
    bot_configuration_error("unknown_secret", field,
                            field + " references secret " + *name +
                                ", which global.secrets_file does not define");
  }
  return *value;
}

auto resolve_connection_secrets(toml::table connection,
                                const SecretStore &secrets,
                                const std::string &path) -> toml::table {
  for (const auto field : kBotSecretFields) {
    if (const auto *node = connection.get(field)) {
      connection.insert_or_assign(
          field, resolve_secret(node, secrets,
                                path + ".connection." + std::string{field}));
    }
  }
  return connection;
}

void validate_reference_grants(const toml::node &node,
                               const std::vector<std::string> &grants,
                               const std::string &path) {
  using core::configuration::bot_configuration_error;
  if (const auto name = secret_reference_name(&node)) {
    if (std::ranges::find(grants, *name) == grants.end()) {
      bot_configuration_error("ungranted_secret", path,
                              path + " references secret " + *name +
                                  ", which this actor's secrets list omits");
    }
    return;
  }
  if (const auto *table = node.as_table()) {
    for (const auto &[key, child] : *table) {
      validate_reference_grants(child, grants,
                                path + "." + std::string{key.str()});
    }
  } else if (const auto *array = node.as_array()) {
    for (std::size_t index = 0; index < array->size(); ++index) {
      validate_reference_grants(*array->get(index), grants,
                                path + "[" + std::to_string(index) + "]");
    }
  }
}

void validate_actor_secrets(const toml::table &document,
                            const SecretStore &secrets) {
  using core::configuration::bot_configuration_error;
  const auto *actors = document.get_as<toml::table>("actors");
  if (actors == nullptr) {
    return;
  }
  for (const auto &[key, node] : *actors) {
    const auto *actor = node.as_table();
    if (actor == nullptr) {
      continue;
    }
    const auto path = "actors." + std::string{key.str()};
    std::vector<std::string> grants;
    if (const auto *list = actor->get("secrets")) {
      const auto *array = list->as_array();
      if (array == nullptr) {
        bot_configuration_error("invalid_secret_grant", path + ".secrets",
                                path + ".secrets must be an array of names");
      }
      for (const auto &entry : *array) {
        const auto name = entry.value<std::string>();
        if (!name || secrets.find(*name) == nullptr) {
          bot_configuration_error(
              "unknown_secret", path + ".secrets",
              path + ".secrets entries must name secrets defined in "
                     "global.secrets_file");
        }
        grants.push_back(*name);
      }
    }
    if (const auto *config = actor->get("config")) {
      validate_reference_grants(*config, grants, path + ".config");
    }
  }
}

auto parse_bot_plans(const toml::table &document,
                     const core::BotPlatformCatalog &catalog,
                     const SecretStore &secrets)
    -> std::vector<std::shared_ptr<const core::BotInstallationPlan>> {
  using core::configuration::bot_configuration_error;
  using core::configuration::required_bool;
  using core::configuration::required_string;
  using core::configuration::validate_keys;
  std::vector<std::shared_ptr<const core::BotInstallationPlan>> plans;
  if (!document.contains("bots")) {
    return plans;
  }
  const auto *bots = document.get_as<toml::table>("bots");
  if (!bots) {
    bot_configuration_error("invalid_bot_configuration", "bots",
                            "bots must be a table");
  }
  for (const auto &[key, node] : *bots) {
    const std::string id{key.str()};
    const auto path = "bots." + id;
    const auto *table = node.as_table();
    if (!table || id.empty()) {
      bot_configuration_error("invalid_bot_configuration", path,
                              path + " must be an installation table");
    }
    validate_keys(*table, {"enabled", "surface", "transport", "connection"},
                  path);
    const auto enabled = required_bool(*table, "enabled", path);
    const auto surface = required_string(*table, "surface", path);
    if (!bot::detail::valid_bot_id(surface)) {
      bot_configuration_error(
          "unsupported_bot_surface", path + ".surface",
          path + ".surface must name an exact registered surface");
    }
    const auto transport = required_string(*table, "transport", path);
    const auto *connection = table->get_as<toml::table>("connection");
    if (!connection) {
      bot_configuration_error("invalid_bot_configuration", path + ".connection",
                              path + ".connection must be a table");
    }
    plans.push_back(catalog.parse(
        {id, enabled, bot::SurfaceId{surface}, transport},
        resolve_connection_secrets(*connection, secrets, path), path));
  }
  return plans;
}

} // namespace

ConfigLoader::ConfigLoader(
    std::shared_ptr<const core::BotPlatformCatalog> catalog)
    : catalog_(std::move(catalog)) {
  if (!catalog_ || !catalog_->sealed()) {
    throw std::invalid_argument(
        "configuration loader requires an explicit sealed platform catalog");
  }
}

auto ConfigLoader::build_snapshot(
    const std::string &config_path,
    std::shared_ptr<const core::BotPlatformCatalog> catalog)
    -> RuntimeConfigBuildResult {
  if (!catalog || !catalog->sealed()) {
    return {.diagnostic = ConfigLoadDiagnostic{
                .code = "bot_platform_catalog_unavailable",
                .path = "bots",
                .message = "configuration loading requires an explicit sealed "
                           "platform catalog"}};
  }
  try {
    auto document = toml::parse_file(config_path);
    auto secrets = load_secrets(document, config_path);
    validate_actor_secrets(document, *secrets);
    auto plans = parse_bot_plans(document, *catalog, *secrets);
    std::vector<BotInstallationMetadata> metadata;
    nlohmann::json fingerprints = nlohmann::json::array();
    for (const auto &plan : plans) {
      metadata.push_back(plan->metadata());
      fingerprints.push_back(plan->fingerprint());
    }
    // The raw Bot table is never retained in the public snapshot.
    document.insert_or_assign("bots", detail::bot_metadata_document(metadata));
    auto process = std::make_shared<const core::ProcessConfiguration>(
        core::ProcessConfiguration{
            std::move(catalog), std::move(plans),
            core::configuration_digest(fingerprints.dump())});
    return {.snapshot = std::shared_ptr<const RuntimeConfigSnapshot>(
                new RuntimeConfigSnapshot(
                    config_path, std::move(document), std::move(metadata),
                    std::move(process), std::move(secrets)))};
  } catch (const core::BotConfigurationError &error) {
    return {.diagnostic = ConfigLoadDiagnostic{.code = error.code(),
                                               .path = error.path(),
                                               .message = error.what()}};
  } catch (const toml::parse_error &error) {
    return {.diagnostic =
                ConfigLoadDiagnostic{.code = "config_parse_failed",
                                     .path = config_path,
                                     .line = error.source().begin.line,
                                     .column = error.source().begin.column}};
  } catch (const std::exception &) {
    return {.diagnostic = ConfigLoadDiagnostic{.code = "config_load_failed",
                                               .path = config_path}};
  }
}

void ConfigLoader::publish_snapshot(
    std::shared_ptr<const RuntimeConfigSnapshot> snapshot) {
  if (!snapshot || core::ProcessConfigAccess::catalog(*snapshot) != catalog_) {
    throw std::invalid_argument(
        "cannot publish a snapshot from a different configuration catalog");
  }
  active_snapshot_.store(std::move(snapshot), std::memory_order_release);
}

auto ConfigLoader::current_snapshot() const noexcept
    -> std::shared_ptr<const RuntimeConfigSnapshot> {
  return active_snapshot_.load(std::memory_order_acquire);
}

auto ConfigLoader::load_config(const std::string &config_path) -> bool {
  auto result = build_snapshot(config_path, catalog_);
  if (!result) {
    const auto &diagnostic = *result.diagnostic;
    OBCX_ERROR("Config load failed [{}] path={} line={} column={}",
               diagnostic.code, diagnostic.path, diagnostic.line,
               diagnostic.column);
    return false;
  }
  publish_snapshot(std::move(result.snapshot));
  return true;
}

auto RuntimeConfigSnapshot::process_owned_fingerprint(
    RuntimeThreadFingerprintInput thread_budget) const
    -> ProcessOwnedConfigFingerprint {
  (void)core::ProcessConfigAccess::plans(*this);
  const auto *db = config_data_.get_as<toml::table>("db");
  const auto *instances = db ? db->get_as<toml::table>("instances") : nullptr;
  return {.bots = process_configuration_->bots_fingerprint,
          .database_instances = instances
                                    ? core::configuration_digest(*instances)
                                    : core::configuration_digest("null"),
          .thread_budget = thread_budget};
}

auto ConfigLoader::get_bot_configs() const
    -> std::vector<BotInstallationMetadata> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<BotInstallationMetadata>{}
                             : snapshot->get_bot_configs();
}

auto ConfigLoader::get_actor_configs() const -> std::vector<ActorConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<ActorConfig>{}
                             : snapshot->get_actor_configs();
}

auto ConfigLoader::get_db_instance_configs() const
    -> std::vector<DbInstanceConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<DbInstanceConfig>{}
                             : snapshot->get_db_instance_configs();
}

auto ConfigLoader::get_pipeline_configs() const -> std::vector<PipelineConfig> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<PipelineConfig>{}
                             : snapshot->get_pipeline_configs();
}

auto ConfigLoader::get_actor_runtime_config() const -> ActorRuntimeConfig {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? ActorRuntimeConfig{}
                             : snapshot->get_actor_runtime_config();
}

auto ConfigLoader::get_command_runtime_config() const -> CommandRuntimeConfig {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? CommandRuntimeConfig{}
                             : snapshot->get_command_runtime_config();
}

auto ConfigLoader::validate_actor_runtime_config() const
    -> std::vector<ConfigValidationError> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<ConfigValidationError>{}
                             : snapshot->validate_actor_runtime_config();
}

auto ConfigLoader::validate_actor_pipeline_configs() const
    -> std::vector<ConfigValidationError> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::vector<ConfigValidationError>{}
                             : snapshot->validate_actor_pipeline_configs();
}

auto ConfigLoader::validate_actor_pipeline_contracts(
    const std::unordered_map<std::string, std::unordered_set<std::string>>
        &actor_inputs) const -> std::vector<ConfigValidationError> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr
             ? std::vector<ConfigValidationError>{}
             : snapshot->validate_actor_pipeline_contracts(actor_inputs);
}

auto ConfigLoader::get_section(const std::string &section_name) const
    -> std::optional<toml::table> {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::nullopt
                             : snapshot->get_section(section_name);
}

auto ConfigLoader::reload_config() -> bool {
  const auto snapshot = current_snapshot();
  return snapshot != nullptr && load_config(snapshot->config_path());
}

auto ConfigLoader::get_config_path() const -> std::string {
  const auto snapshot = current_snapshot();
  return snapshot == nullptr ? std::string{} : snapshot->config_path();
}

} // namespace obcx::common
