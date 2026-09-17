#include "common/config_snapshot.hpp"
#include "bot_metadata_document.hpp"

#include <algorithm>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_set>

namespace obcx::common {

namespace {

auto get_string_value(const toml::table &table, std::string_view key,
                      const std::string &default_value = "") -> std::string {
  if (const auto node = table.get(key)) {
    if (auto value = node->value<std::string>()) {
      return *value;
    }
  }

  return default_value;
}

auto get_bool_value(const toml::table &table, std::string_view key,
                    const bool default_value = false) -> bool {
  if (const auto node = table.get(key)) {
    if (auto value = node->value<bool>()) {
      return *value;
    }
  }

  return default_value;
}

auto get_non_negative_size(const toml::table &table, std::string_view key,
                           const size_t default_value) -> size_t {
  if (const auto node = table.get(key)) {
    if (const auto value = node->value<int64_t>(); value && *value >= 0) {
      return static_cast<size_t>(*value);
    }
  }

  return default_value;
}

auto get_string_array(const toml::node *node) -> std::vector<std::string> {
  std::vector<std::string> values;

  if (node == nullptr) {
    return values;
  }

  if (const auto array = node->as_array()) {
    for (const auto &item : *array) {
      if (auto item_str = item.value<std::string>()) {
        values.push_back(*item_str);
      }
    }
  }

  return values;
}

auto get_string_or_array(const toml::node *node) -> std::vector<std::string> {
  if (node == nullptr) {
    return {};
  }

  if (auto value = node->value<std::string>()) {
    return {*value};
  }

  return get_string_array(node);
}

auto valid_command_name(const std::string &name) -> bool {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  return std::ranges::all_of(name, [](const unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

auto command_access_mode(const toml::table *policy)
    -> std::optional<CommandAccessMode> {
  if (policy == nullptr) {
    return std::nullopt;
  }
  const auto value = (*policy)["mode"].value<std::string>();
  if (value == "unrestricted") {
    return CommandAccessMode::Unrestricted;
  }
  if (value == "allowlist") {
    return CommandAccessMode::Allowlist;
  }
  if (value == "denylist") {
    return CommandAccessMode::Denylist;
  }
  return std::nullopt;
}

auto parse_group_policy(const toml::table *policy) -> CommandGroupPolicyConfig {
  CommandGroupPolicyConfig result;
  result.mode = command_access_mode(policy);
  if (policy == nullptr) {
    return result;
  }
  const auto *entries_node = policy->get("entries");
  result.entries_present = entries_node != nullptr;
  const auto *entries =
      entries_node == nullptr ? nullptr : entries_node->as_array();
  if (entries == nullptr) {
    return result;
  }
  for (const auto &node : *entries) {
    const auto *entry = node.as_table();
    if (entry == nullptr) {
      continue;
    }
    result.entries.push_back(CommandGroupIdentityConfig{
        .platform = get_string_value(*entry, "platform"),
        .bot = get_string_value(*entry, "bot"),
        .native_group_id = get_string_value(*entry, "native_group_id"),
    });
  }
  return result;
}

auto parse_user_policy(const toml::table *policy) -> CommandUserPolicyConfig {
  CommandUserPolicyConfig result;
  result.mode = command_access_mode(policy);
  if (policy == nullptr) {
    return result;
  }
  const auto *entries_node = policy->get("entries");
  result.entries_present = entries_node != nullptr;
  const auto *entries =
      entries_node == nullptr ? nullptr : entries_node->as_array();
  if (entries == nullptr) {
    return result;
  }
  for (const auto &node : *entries) {
    const auto *entry = node.as_table();
    if (entry == nullptr) {
      continue;
    }
    result.entries.push_back(CommandUserIdentityConfig{
        .platform = get_string_value(*entry, "platform"),
        .bot = get_string_value(*entry, "bot"),
        .native_user_id = get_string_value(*entry, "native_user_id"),
    });
  }
  return result;
}

void append_unknown_key_errors(
    const toml::table &table,
    const std::initializer_list<std::string_view> allowed,
    const std::string &path, std::vector<ConfigValidationError> &errors) {
  for (const auto &[key, value] : table) {
    (void)value;
    const auto known = std::ranges::find(allowed, key.str()) != allowed.end();
    if (!known) {
      errors.push_back(ConfigValidationError{
          .code = "unknown_command_runtime_field",
          .message = "command runtime table contains an unknown field",
          .dependency = path + "." + std::string{key.str()},
      });
    }
  }
}

auto valid_policy_id(const std::string_view value,
                     const std::size_t maximum = 1024U) -> bool {
  if (value.empty() || value.size() > maximum) {
    return false;
  }
  return std::ranges::all_of(value, [](const unsigned char byte) {
    return byte >= 0x20U && byte != 0x7FU;
  });
}

auto platform_matches_installation(
    const std::vector<BotInstallationMetadata> &bots,
    const std::string_view platform, const std::string_view installation)
    -> bool {
  const auto bot = std::ranges::find(bots, installation,
                                     &BotInstallationMetadata::installation_id);
  return bot != bots.end() && bot->enabled && bot->ingress_platform == platform;
}

void validate_command_policy(const toml::table *policy, const bool group,
                             const std::string &path,
                             const std::vector<BotInstallationMetadata> &bots,
                             std::vector<ConfigValidationError> &errors) {
  if (policy == nullptr) {
    errors.push_back(ConfigValidationError{
        .code = "missing_command_access_policy",
        .message = "command access requires explicit group and user policies",
        .dependency = path,
    });
    return;
  }
  append_unknown_key_errors(*policy, {"mode", "entries"}, path, errors);
  const auto mode = (*policy)["mode"].value<std::string>();
  if (!mode || (*mode != "unrestricted" && *mode != "allowlist" &&
                *mode != "denylist")) {
    errors.push_back(ConfigValidationError{
        .code = "invalid_command_access_mode",
        .message = "command access mode must be unrestricted, allowlist, or "
                   "denylist",
        .dependency = path + ".mode",
    });
  }
  const auto *entries_node = policy->get("entries");
  const auto *entries =
      entries_node == nullptr ? nullptr : entries_node->as_array();
  if (entries == nullptr) {
    errors.push_back(ConfigValidationError{
        .code = "invalid_command_access_entries",
        .message = "command access entries must be an explicit array",
        .dependency = path + ".entries",
    });
    return;
  }
  if (entries->size() > CommandRuntimeConfig::max_access_entries) {
    errors.push_back(ConfigValidationError{
        .code = "command_access_entries_exceed_limit",
        .message = "command access entries exceed the supported bound",
        .dependency = path + ".entries",
    });
  }
  if (mode == "unrestricted" && !entries->empty()) {
    errors.push_back(ConfigValidationError{
        .code = "unrestricted_command_access_has_entries",
        .message = "unrestricted command access requires an empty array",
        .dependency = path + ".entries",
    });
  }

  std::set<std::tuple<std::string, std::string, std::string>> identities;
  std::size_t index = 0;
  for (const auto &node : *entries) {
    const auto entry_path = path + ".entries[" + std::to_string(index++) + "]";
    const auto *entry = node.as_table();
    if (entry == nullptr) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_access_entry",
          .message = "command access entry must be a table",
          .dependency = entry_path,
      });
      continue;
    }
    const auto native_key = group ? "native_group_id" : "native_user_id";
    append_unknown_key_errors(*entry, {"platform", "bot", native_key},
                              entry_path, errors);
    const auto platform = (*entry)["platform"].value<std::string>();
    const auto bot = (*entry)["bot"].value<std::string>();
    const auto native = (*entry)[native_key].value<std::string>();
    if (!platform || (*platform != "telegram" && *platform != "qq")) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_access_platform",
          .message = "command access platform must be normalized and active",
          .dependency = entry_path + ".platform",
      });
    }
    if (!bot || !valid_policy_id(*bot, 256U)) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_access_bot",
          .message = "command access bot must be an exact installation ID",
          .dependency = entry_path + ".bot",
      });
    }
    if (!native || !valid_policy_id(*native)) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_access_native_id",
          .message = "command access native ID is invalid",
          .dependency = entry_path + "." + native_key,
      });
    }
    if (platform && bot &&
        !platform_matches_installation(bots, *platform, *bot)) {
      errors.push_back(ConfigValidationError{
          .code = "command_access_installation_mismatch",
          .message = "command access identity does not match an enabled bot",
          .dependency = entry_path,
      });
    }
    if (platform && bot && native &&
        !identities.emplace(*platform, *bot, *native).second) {
      errors.push_back(ConfigValidationError{
          .code = "duplicate_command_access_identity",
          .message = "command access policy contains a duplicate identity",
          .dependency = entry_path,
      });
    }
  }
}

auto has_stage_dependency_cycle(const PipelineConfig &pipeline) -> bool {
  std::unordered_set<std::string> stage_names;
  std::unordered_map<std::string, std::vector<std::string>> graph;
  std::unordered_map<std::string, int> in_degree;

  for (const auto &stage : pipeline.stages) {
    stage_names.insert(stage.name);
    graph[stage.name] = {};
    in_degree[stage.name] = 0;
  }

  for (const auto &stage : pipeline.stages) {
    for (const auto &dependency : stage.after) {
      if (!stage_names.contains(dependency)) {
        continue;
      }
      graph[dependency].push_back(stage.name);
      in_degree[stage.name]++;
    }
  }

  std::queue<std::string> ready;
  for (const auto &[stage_name, degree] : in_degree) {
    if (degree == 0) {
      ready.push(stage_name);
    }
  }

  size_t visited = 0;
  while (!ready.empty()) {
    const auto current = ready.front();
    ready.pop();
    visited++;

    for (const auto &next : graph[current]) {
      in_degree[next]--;
      if (in_degree[next] == 0) {
        ready.push(next);
      }
    }
  }

  return visited != stage_names.size();
}

auto has_actor_dependency_cycle(const std::vector<ActorConfig> &actors)
    -> bool {
  std::unordered_set<std::string> enabled;
  std::unordered_map<std::string, size_t> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> dependents;
  for (const auto &actor : actors) {
    if (actor.enabled) {
      enabled.insert(actor.name);
      in_degree[actor.name] = 0;
    }
  }
  for (const auto &actor : actors) {
    if (!actor.enabled) {
      continue;
    }
    for (const auto &dependency : actor.required) {
      if (enabled.contains(dependency)) {
        dependents[dependency].push_back(actor.name);
        ++in_degree[actor.name];
      }
    }
  }

  std::queue<std::string> ready;
  for (const auto &[actor, degree] : in_degree) {
    if (degree == 0) {
      ready.push(actor);
    }
  }
  size_t visited = 0;
  while (!ready.empty()) {
    auto actor = std::move(ready.front());
    ready.pop();
    ++visited;
    for (const auto &dependent : dependents[actor]) {
      if (--in_degree[dependent] == 0) {
        ready.push(dependent);
      }
    }
  }
  return visited != enabled.size();
}

} // namespace

auto ActorConfigSnapshotBuilder::build(
    toml::table actor_document, std::vector<BotInstallationMetadata> bots,
    std::string config_path) -> RuntimeConfigBuildResult {
  if (actor_document.contains("bots")) {
    return {.diagnostic = ConfigLoadDiagnostic{
                .code = "actor_config_contains_bot_configuration",
                .path = "bots",
                .message = "actor-only construction requires explicit Bot "
                           "metadata, not a bots table"}};
  }
  try {
    auto metadata = detail::bot_metadata_document(bots);
    actor_document.insert("bots", std::move(metadata));
    return {.snapshot = std::shared_ptr<const RuntimeConfigSnapshot>(
                new RuntimeConfigSnapshot(std::move(config_path),
                                          std::move(actor_document),
                                          std::move(bots), nullptr))};
  } catch (const std::exception &) {
    return {.diagnostic = ConfigLoadDiagnostic{
                .code = "invalid_actor_bot_metadata",
                .path = "bots",
                .message = "invalid or duplicate installation metadata"}};
  }
}

auto RuntimeConfigSnapshot::get_bot_configs() const
    -> std::vector<BotInstallationMetadata> {
  return bots_;
}

auto RuntimeConfigSnapshot::get_actor_configs() const
    -> std::vector<ActorConfig> {
  std::vector<ActorConfig> actor_configs;

  if (auto actors_section = config_data_.get("actors")) {
    if (auto actors_table = actors_section->as_table()) {
      for (const auto &[actor_name, actor_config] : *actors_table) {
        if (auto actor_table = actor_config.as_table()) {
          ActorConfig config;
          config.name = std::string{actor_name};
          config.library = get_string_value(*actor_table, "library");
          config.enabled = get_bool_value(*actor_table, "enabled");
          config.required = get_string_array(actor_table->get("requires"));
          config.partition =
              get_string_value(*actor_table, "partition", "global");
          config.db = get_string_value(*actor_table, "db");
          config.db_namespace =
              get_string_value(*actor_table, "db_namespace", config.name);

          actor_configs.push_back(std::move(config));
        }
      }
    }
  }

  return actor_configs;
}

auto RuntimeConfigSnapshot::get_db_instance_configs() const
    -> std::vector<DbInstanceConfig> {
  std::vector<DbInstanceConfig> db_configs;

  if (auto db_section = config_data_.get("db")) {
    if (auto db_table = db_section->as_table()) {
      if (auto instances_section = db_table->get("instances")) {
        if (auto instances_table = instances_section->as_table()) {
          for (const auto &[instance_name, instance_config] :
               *instances_table) {
            if (auto instance_table = instance_config.as_table()) {
              DbInstanceConfig config;
              config.name = std::string{instance_name};
              config.type = get_string_value(*instance_table, "type");
              config.path = get_string_value(*instance_table, "path");
              config.dsn = get_string_value(*instance_table, "dsn");
              config.config = *instance_table;

              db_configs.push_back(std::move(config));
            }
          }
        }
      }
    }
  }

  return db_configs;
}

auto RuntimeConfigSnapshot::get_pipeline_configs() const
    -> std::vector<PipelineConfig> {
  std::vector<PipelineConfig> pipeline_configs;

  if (auto pipelines_section = config_data_.get("pipelines")) {
    if (auto pipelines_table = pipelines_section->as_table()) {
      for (const auto &[pipeline_name, pipeline_config] : *pipelines_table) {
        if (auto pipeline_table = pipeline_config.as_table()) {
          PipelineConfig config;
          config.name = std::string{pipeline_name};
          config.source = get_string_value(*pipeline_table, "source");

          if (auto stages_section = pipeline_table->get("stages")) {
            if (auto stages_array = stages_section->as_array()) {
              for (const auto &stage_config : *stages_array) {
                if (auto stage_table = stage_config.as_table()) {
                  PipelineStageConfig stage;
                  stage.name = get_string_value(*stage_table, "name");
                  stage.actor = get_string_value(*stage_table, "actor");
                  stage.input = get_string_value(*stage_table, "input");
                  stage.outputs =
                      get_string_or_array(stage_table->get("output"));
                  stage.after = get_string_array(stage_table->get("after"));
                  stage.mode = get_string_value(*stage_table, "mode");

                  config.stages.push_back(std::move(stage));
                }
              }
            }
          }

          pipeline_configs.push_back(std::move(config));
        }
      }
    }
  }

  return pipeline_configs;
}

auto RuntimeConfigSnapshot::get_actor_runtime_config() const
    -> ActorRuntimeConfig {
  ActorRuntimeConfig config;

  const auto *runtime = config_data_.get_as<toml::table>("actor_runtime");
  if (runtime == nullptr) {
    return config;
  }

  if (const auto *scheduler = runtime->get_as<toml::table>("scheduler")) {
    const auto policy = get_string_value(*scheduler, "policy", "stealing");
    if (policy == "sharing") {
      config.policy = ActorSchedulerPolicy::Sharing;
    }

    config.workers = get_non_negative_size(*scheduler, "workers", 0);
    config.blocking_workers =
        get_non_negative_size(*scheduler, "blocking_workers", 0);
    config.slow_resume_warning_ms =
        get_non_negative_size(*scheduler, "slow_resume_warning_ms", 10);
  }
  if (const auto *routing = runtime->get_as<toml::table>("routing")) {
    config.routing_hop_limit = get_non_negative_size(*routing, "hop_limit", 32);
  }
  if (const auto *reload = runtime->get_as<toml::table>("reload")) {
    config.reload_drain_timeout_ms = get_non_negative_size(
        *reload, "drain_timeout_ms",
        ActorRuntimeConfig::default_reload_drain_timeout_ms);
  }

  return config;
}

auto RuntimeConfigSnapshot::get_command_runtime_config() const
    -> CommandRuntimeConfig {
  CommandRuntimeConfig config;
  const auto *runtime = config_data_.get_as<toml::table>("command_runtime");
  if (runtime == nullptr) {
    return config;
  }

  config.timeout_ms = get_non_negative_size(
      *runtime, "timeout_ms", CommandRuntimeConfig::default_timeout_ms);
  if (const auto *help = runtime->get_as<toml::table>("help")) {
    if (const auto value = (*help)["page_bytes"].value<int64_t>();
        value && *value >= 0) {
      config.help.page_bytes = static_cast<size_t>(*value);
    }
    if (const auto value = (*help)["maximum_pages"].value<int64_t>();
        value && *value >= 0) {
      config.help.maximum_pages = static_cast<size_t>(*value);
    }
  }
  if (const auto *access = runtime->get_as<toml::table>("access")) {
    config.access.groups =
        parse_group_policy(access->get_as<toml::table>("groups"));
    config.access.users =
        parse_user_policy(access->get_as<toml::table>("users"));
    const auto *overrides_node = access->get("overrides");
    config.access.overrides_present = overrides_node != nullptr;
    if (const auto *overrides = access->get_as<toml::table>("overrides")) {
      for (const auto &[command, node] : *overrides) {
        const auto *override_table = node.as_table();
        if (override_table == nullptr) {
          continue;
        }
        config.access.overrides.push_back(CommandAccessOverrideConfig{
            .command = std::string{command.str()},
            .groups = parse_group_policy(
                override_table->get_as<toml::table>("groups")),
            .users =
                parse_user_policy(override_table->get_as<toml::table>("users")),
        });
      }
    }
  }
  if (const auto *observers =
          runtime->get_as<toml::array>("message_observers")) {
    for (const auto &observer_node : *observers) {
      const auto *observer = observer_node.as_table();
      if (observer == nullptr) {
        continue;
      }
      config.message_observers.push_back(CommandMessageObserverConfig{
          .actor = get_string_value(*observer, "actor"),
          .platforms = get_string_array(observer->get("platforms")),
          .bots = get_string_array(observer->get("bots")),
          .timeout_ms = get_non_negative_size(*observer, "timeout_ms", 0),
      });
    }
  }
  if (const auto *routes = runtime->get_as<toml::array>("routes")) {
    for (const auto &route_node : *routes) {
      const auto *route = route_node.as_table();
      if (route == nullptr) {
        continue;
      }
      CommandRouteConfig parsed;
      parsed.actor = get_string_value(*route, "actor");
      parsed.commands = get_string_array(route->get("commands"));
      parsed.platforms = get_string_array(route->get("platforms"));
      parsed.bots = get_string_array(route->get("bots"));
      parsed.fallback =
          get_string_value(*route, "fallback", "continue") == "consume"
              ? CommandFallback::Consume
              : CommandFallback::Continue;
      parsed.timeout_ms = get_non_negative_size(*route, "timeout_ms", 0);
      config.routes.push_back(std::move(parsed));
    }
  }
  return config;
}

auto RuntimeConfigSnapshot::validate_actor_runtime_config() const
    -> std::vector<ConfigValidationError> {
  std::vector<ConfigValidationError> errors;

  const auto *runtime = config_data_.get_as<toml::table>("actor_runtime");
  if (runtime != nullptr) {
    if (const auto *scheduler = runtime->get_as<toml::table>("scheduler")) {
      if (const auto *policy_node = scheduler->get("policy")) {
        const auto policy = policy_node->value<std::string>();
        if (!policy || (*policy != "stealing" && *policy != "sharing")) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_actor_scheduler_policy",
              .message = "actor_runtime.scheduler.policy must be 'stealing' or "
                         "'sharing'",
              .dependency = policy.value_or("<non-string>"),
          });
        }
      }

      for (const auto key :
           {std::string_view{"workers"}, std::string_view{"blocking_workers"},
            std::string_view{"slow_resume_warning_ms"}}) {
        if (const auto *node = scheduler->get(key)) {
          const auto value = node->value<int64_t>();
          if (!value || *value < 0) {
            errors.push_back(ConfigValidationError{
                .code = key == "workers" ? "invalid_actor_worker_count"
                        : key == "blocking_workers"
                            ? "invalid_blocking_worker_count"
                            : "invalid_slow_resume_warning",
                .message = "actor runtime numeric scheduler options must be "
                           "non-negative integers",
                .dependency = std::string{key},
            });
          }
        }
      }
    }
    if (const auto *routing = runtime->get_as<toml::table>("routing")) {
      if (const auto *node = routing->get("hop_limit")) {
        const auto value = node->value<int64_t>();
        if (!value || *value <= 0) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_routing_hop_limit",
              .message = "actor_runtime.routing.hop_limit must be a positive "
                         "integer",
              .dependency = "hop_limit",
          });
        }
      }
    }
    if (const auto *reload = runtime->get_as<toml::table>("reload")) {
      if (const auto *node = reload->get("drain_timeout_ms")) {
        const auto value = node->value<int64_t>();
        if (!value ||
            *value < static_cast<int64_t>(
                         ActorRuntimeConfig::min_reload_drain_timeout_ms) ||
            *value > static_cast<int64_t>(
                         ActorRuntimeConfig::max_reload_drain_timeout_ms)) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_reload_drain_timeout",
              .message =
                  "actor_runtime.reload.drain_timeout_ms must be between " +
                  std::to_string(
                      ActorRuntimeConfig::min_reload_drain_timeout_ms) +
                  " and " +
                  std::to_string(
                      ActorRuntimeConfig::max_reload_drain_timeout_ms),
              .dependency = "drain_timeout_ms",
          });
        }
      }
    }
  }

  const auto *command_runtime =
      config_data_.get_as<toml::table>("command_runtime");
  if (command_runtime != nullptr) {
    append_unknown_key_errors(
        *command_runtime,
        {"timeout_ms", "help", "access", "message_observers", "routes"},
        "command_runtime", errors);
    const auto *routes = command_runtime->get_as<toml::array>("routes");
    const auto routes_active = routes != nullptr && !routes->empty();

    const auto *help = command_runtime->get_as<toml::table>("help");
    if (routes_active && help == nullptr) {
      errors.push_back(ConfigValidationError{
          .code = "missing_command_help_configuration",
          .message = "active command routes require explicit help bounds",
          .dependency = "command_runtime.help",
      });
    }
    if (help != nullptr) {
      append_unknown_key_errors(*help, {"page_bytes", "maximum_pages"},
                                "command_runtime.help", errors);
      const auto validate_help_bound = [&](const std::string_view key,
                                           const std::size_t maximum) {
        const auto value = (*help)[key].value<int64_t>();
        if (!value || *value <= 0 ||
            static_cast<std::uint64_t>(*value) > maximum) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_help_bound",
              .message = "command help bounds must be finite positive "
                         "integers within supported limits",
              .dependency = "command_runtime.help." + std::string{key},
          });
        }
      };
      validate_help_bound("page_bytes",
                          CommandRuntimeConfig::max_help_page_bytes);
      validate_help_bound("maximum_pages",
                          CommandRuntimeConfig::max_help_pages);
    }

    const auto *access = command_runtime->get_as<toml::table>("access");
    if (routes_active && access == nullptr) {
      errors.push_back(ConfigValidationError{
          .code = "missing_command_access_configuration",
          .message = "active command routes require explicit access policy",
          .dependency = "command_runtime.access",
      });
    }
    if (access != nullptr) {
      append_unknown_key_errors(*access, {"groups", "users", "overrides"},
                                "command_runtime.access", errors);
      validate_command_policy(access->get_as<toml::table>("groups"), true,
                              "command_runtime.access.groups", bots_, errors);
      validate_command_policy(access->get_as<toml::table>("users"), false,
                              "command_runtime.access.users", bots_, errors);
      const auto *overrides_node = access->get("overrides");
      const auto *overrides =
          overrides_node == nullptr ? nullptr : overrides_node->as_table();
      if (overrides_node != nullptr && overrides == nullptr) {
        errors.push_back(ConfigValidationError{
            .code = "invalid_command_access_overrides",
            .message = "command access overrides must be keyed tables; use "
                       "[command_runtime.access.overrides.<command>.groups] "
                       "and .users instead of an array with command fields",
            .dependency = "command_runtime.access.overrides",
        });
      }
      if (overrides != nullptr) {
        if (overrides->size() > CommandRuntimeConfig::max_access_overrides) {
          errors.push_back(ConfigValidationError{
              .code = "command_access_overrides_exceed_limit",
              .message = "command access overrides exceed the supported bound",
              .dependency = "command_runtime.access.overrides",
          });
        }
        for (const auto &[key, node] : *overrides) {
          const auto command = std::string{key.str()};
          const auto path = "command_runtime.access.overrides." + command;
          if (!valid_command_name(command)) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_access_override_name",
                .message =
                    "command access override key must be a canonical name",
                .dependency = path,
            });
          }
          const auto *override_table = node.as_table();
          if (override_table == nullptr) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_access_override",
                .message = "command access override must be a table",
                .dependency = path,
            });
            continue;
          }
          append_unknown_key_errors(*override_table, {"groups", "users"}, path,
                                    errors);
          validate_command_policy(override_table->get_as<toml::table>("groups"),
                                  true, path + ".groups", bots_, errors);
          validate_command_policy(override_table->get_as<toml::table>("users"),
                                  false, path + ".users", bots_, errors);
        }
      }
    }

    if (const auto *timeout = command_runtime->get("timeout_ms")) {
      const auto value = timeout->value<int64_t>();
      if (!value ||
          *value < static_cast<int64_t>(CommandRuntimeConfig::min_timeout_ms) ||
          *value > static_cast<int64_t>(CommandRuntimeConfig::max_timeout_ms)) {
        errors.push_back(ConfigValidationError{
            .code = "invalid_command_timeout",
            .message = "command_runtime.timeout_ms must be between " +
                       std::to_string(CommandRuntimeConfig::min_timeout_ms) +
                       " and " +
                       std::to_string(CommandRuntimeConfig::max_timeout_ms),
            .dependency = "command_runtime.timeout_ms",
        });
      }
    }
    const auto *observers_node = command_runtime->get("message_observers");
    if (observers_node != nullptr && observers_node->as_array() == nullptr) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_message_observers",
          .message =
              "command_runtime.message_observers must be an array of tables",
          .dependency = "command_runtime.message_observers",
      });
    }
    if (const auto *observers =
            command_runtime->get_as<toml::array>("message_observers")) {
      if (observers->size() > CommandRuntimeConfig::max_message_observers) {
        errors.push_back(ConfigValidationError{
            .code = "command_message_observers_exceed_limit",
            .message = "command message observers exceed the supported bound",
            .dependency = "command_runtime.message_observers",
        });
      }
      size_t observer_index = 0;
      for (const auto &observer_node : *observers) {
        const auto *observer = observer_node.as_table();
        const auto observer_path = "command_runtime.message_observers[" +
                                   std::to_string(observer_index++) + "]";
        if (observer == nullptr) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_message_observer",
              .message = "command message observer must be a table",
              .dependency = observer_path,
          });
          continue;
        }
        append_unknown_key_errors(*observer,
                                  {"actor", "platforms", "bots", "timeout_ms"},
                                  observer_path, errors);
        const auto actor = observer->get("actor");
        if (actor == nullptr || !actor->is_string() ||
            actor->value_or<std::string>("").empty()) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_message_observer_actor",
              .message =
                  "command message observer actor must be a non-empty string",
              .dependency = observer_path + ".actor",
          });
        }
        for (const auto key :
             {std::string_view{"platforms"}, std::string_view{"bots"}}) {
          const auto *node = observer->get(key);
          const auto *array = node == nullptr ? nullptr : node->as_array();
          if (array == nullptr || array->empty() ||
              !std::ranges::all_of(*array, [](const auto &item) {
                if (!item.is_string()) {
                  return false;
                }
                const auto value = item.template value<std::string>();
                return value && !value->empty();
              })) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_message_observer_scope",
                .message = "command message observer platforms and bots must "
                           "be non-empty string arrays",
                .dependency = observer_path + "." + std::string{key},
            });
          }
        }
        const auto *timeout = observer->get("timeout_ms");
        const auto timeout_value = timeout == nullptr
                                       ? std::optional<int64_t>{}
                                       : timeout->value<int64_t>();
        if (!timeout_value ||
            *timeout_value <
                static_cast<int64_t>(CommandRuntimeConfig::min_timeout_ms) ||
            *timeout_value >
                static_cast<int64_t>(CommandRuntimeConfig::max_timeout_ms)) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_message_observer_timeout",
              .message = "command message observer timeout_ms must be "
                         "specified within the supported range",
              .dependency = observer_path + ".timeout_ms",
          });
        }
      }
    }

    const auto *routes_node = command_runtime->get("routes");
    if (routes_node != nullptr && routes_node->as_array() == nullptr) {
      errors.push_back(ConfigValidationError{
          .code = "invalid_command_routes",
          .message = "command_runtime.routes must be an array of tables",
          .dependency = "command_runtime.routes",
      });
    }
    if (const auto *routes = command_runtime->get_as<toml::array>("routes")) {
      size_t route_index = 0;
      for (const auto &route_node : *routes) {
        const auto *route = route_node.as_table();
        const auto route_path =
            "command_runtime.routes[" + std::to_string(route_index++) + "]";
        if (route == nullptr) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_route",
              .message = "command route must be a table",
              .dependency = route_path,
          });
          continue;
        }
        append_unknown_key_errors(*route,
                                  {"actor", "commands", "platforms", "bots",
                                   "fallback", "timeout_ms"},
                                  route_path, errors);
        const auto actor = route->get("actor");
        if (actor == nullptr || !actor->is_string() ||
            actor->value_or<std::string>("").empty()) {
          errors.push_back(ConfigValidationError{
              .code = "invalid_command_actor",
              .message = "command route actor must be a non-empty string",
              .dependency = route_path + ".actor",
          });
        }
        for (const auto key :
             {std::string_view{"commands"}, std::string_view{"platforms"},
              std::string_view{"bots"}}) {
          const auto *node = route->get(key);
          const auto *array = node == nullptr ? nullptr : node->as_array();
          if (array == nullptr || array->empty() ||
              !std::ranges::all_of(*array, [](const auto &item) {
                if (!item.is_string()) {
                  return false;
                }
                const auto value = item.template value<std::string>();
                return value && !value->empty();
              })) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_scope",
                .message = "command route commands, platforms, and bots must "
                           "be non-empty string arrays",
                .dependency = route_path + "." + std::string{key},
            });
          }
        }
        if (const auto *commands = route->get_as<toml::array>("commands")) {
          for (const auto &command : *commands) {
            const auto name = command.value<std::string>();
            if (name && !valid_command_name(*name)) {
              errors.push_back(ConfigValidationError{
                  .code = "invalid_command_name",
                  .message = "command route contains an invalid command name",
                  .dependency = *name,
              });
            }
          }
        }
        if (const auto *fallback = route->get("fallback")) {
          const auto value = fallback->value<std::string>();
          if (!value || (*value != "continue" && *value != "consume")) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_fallback",
                .message =
                    "command route fallback must be 'continue' or 'consume'",
                .dependency = route_path + ".fallback",
            });
          }
        }
        if (const auto *timeout = route->get("timeout_ms")) {
          const auto value = timeout->value<int64_t>();
          if (!value ||
              *value <
                  static_cast<int64_t>(CommandRuntimeConfig::min_timeout_ms) ||
              *value >
                  static_cast<int64_t>(CommandRuntimeConfig::max_timeout_ms)) {
            errors.push_back(ConfigValidationError{
                .code = "invalid_command_timeout",
                .message = "command route timeout_ms is outside the supported "
                           "range",
                .dependency = route_path + ".timeout_ms",
            });
          }
        }
      }
    }
  }

  return errors;
}

auto RuntimeConfigSnapshot::validate_actor_pipeline_configs() const
    -> std::vector<ConfigValidationError> {
  std::vector<ConfigValidationError> errors;

  const auto actors = get_actor_configs();
  const auto pipelines = get_pipeline_configs();
  const auto db_instances = get_db_instance_configs();

  std::unordered_set<std::string> actor_names;
  for (const auto &actor : actors) {
    if (actor.enabled) {
      actor_names.insert(actor.name);
    }
  }

  for (const auto &actor : actors) {
    if (!actor.enabled) {
      continue;
    }
    for (const auto &dependency : actor.required) {
      if (!actor_names.contains(dependency)) {
        errors.push_back(ConfigValidationError{
            .code = "missing_actor_dependency",
            .message = "Actor requires another actor that is not declared "
                       "and enabled in [actors]",
            .actor = actor.name,
            .dependency = dependency,
        });
      }
    }
  }
  if (has_actor_dependency_cycle(actors)) {
    errors.push_back(ConfigValidationError{
        .code = "actor_dependency_cycle",
        .message = "Enabled actor dependency graph contains a cycle",
    });
  }

  std::unordered_set<std::string> db_instance_names;
  for (const auto &db_instance : db_instances) {
    db_instance_names.insert(db_instance.name);
  }

  std::unordered_set<std::string> routable_sources{
      "obcx::core::events::RawMessageEvent",
      "obcx::core::events::RawNoticeEvent",
      "obcx::core::events::RawHeartbeatEvent",
      "obcx::core::events::BotMessageSentEvent", "ActorFailed"};
  for (const auto &pipeline : pipelines) {
    for (const auto &stage : pipeline.stages) {
      if (!stage.input.empty()) {
        routable_sources.insert(stage.input);
      }
      routable_sources.insert(stage.outputs.begin(), stage.outputs.end());
    }
  }

  for (const auto &actor : actors) {
    if (actor.enabled && !actor.db.empty() &&
        !db_instance_names.contains(actor.db)) {
      errors.push_back(ConfigValidationError{
          .code = "missing_db_instance",
          .message = "Actor references a DB instance that is not declared in "
                     "[db.instances]",
          .actor = actor.name,
          .dependency = actor.db,
      });
    }
  }

  for (const auto &pipeline : pipelines) {
    if (pipeline.source.empty() ||
        !routable_sources.contains(pipeline.source)) {
      errors.push_back(ConfigValidationError{
          .code = pipeline.source.empty() ? "invalid_pipeline_source"
                                          : "unknown_pipeline_source",
          .message = pipeline.source.empty()
                         ? "Pipeline source must not be empty"
                         : "Pipeline source must be a runtime ingress type, "
                           "a configured actor input, or a declared stage "
                           "output",
          .pipeline = pipeline.name,
          .input = pipeline.source,
      });
    }
    std::unordered_set<std::string> stage_names;
    for (const auto &stage : pipeline.stages) {
      if (!stage_names.insert(stage.name).second) {
        errors.push_back(ConfigValidationError{
            .code = "duplicate_stage_name",
            .message = "Pipeline stage names must be unique",
            .pipeline = pipeline.name,
            .stage = stage.name,
        });
      }
    }

    for (const auto &stage : pipeline.stages) {
      if (!stage.mode.empty() && stage.mode != "await" &&
          stage.mode != "async") {
        errors.push_back(ConfigValidationError{
            .code = "invalid_stage_mode",
            .message = "Pipeline stage mode must be 'await' or 'async'",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
            .dependency = stage.mode,
        });
      }
      if (!actor_names.contains(stage.actor)) {
        errors.push_back(ConfigValidationError{
            .code = "missing_actor",
            .message = "Pipeline stage references an actor that is not "
                       "declared and enabled in [actors]",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
        });
      }

      for (const auto &dependency : stage.after) {
        if (!stage_names.contains(dependency)) {
          errors.push_back(ConfigValidationError{
              .code = "missing_stage_dependency",
              .message = "Pipeline stage references an unknown dependency in "
                         "after",
              .pipeline = pipeline.name,
              .stage = stage.name,
              .actor = stage.actor,
              .dependency = dependency,
          });
        }
      }
    }

    if (has_stage_dependency_cycle(pipeline)) {
      errors.push_back(ConfigValidationError{
          .code = "stage_dependency_cycle",
          .message = "Pipeline stage dependency graph contains a cycle",
          .pipeline = pipeline.name,
      });
    }
  }

  return errors;
}

auto RuntimeConfigSnapshot::validate_actor_pipeline_contracts(
    const std::unordered_map<std::string, std::unordered_set<std::string>>
        &actor_inputs) const -> std::vector<ConfigValidationError> {
  std::vector<ConfigValidationError> errors;
  for (const auto &pipeline : get_pipeline_configs()) {
    for (const auto &stage : pipeline.stages) {
      const auto actor = actor_inputs.find(stage.actor);
      if (actor == actor_inputs.end()) {
        errors.push_back(ConfigValidationError{
            .code = "actor_unavailable",
            .message = "Pipeline stage actor was not loaded with a valid "
                       "input contract",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
            .input = stage.input,
        });
        continue;
      }
      if (!actor->second.contains(stage.input)) {
        errors.push_back(ConfigValidationError{
            .code = "unsupported_actor_input",
            .message = "Pipeline stage input is absent from the actor input "
                       "contract",
            .pipeline = pipeline.name,
            .stage = stage.name,
            .actor = stage.actor,
            .input = stage.input,
        });
      }
    }
  }
  return errors;
}

auto RuntimeConfigSnapshot::get_section(
    const std::string_view section_name) const -> std::optional<toml::table> {
  const auto section = config_data_.at_path(section_name);
  if (section) {
    if (const auto *section_table = section.as_table()) {
      return *section_table;
    }
  }

  return std::nullopt;
}

auto RuntimeConfigSnapshot::get_actor_section(
    const std::string_view actor, const std::string_view section_name) const
    -> std::optional<toml::table> {
  const auto *actors = config_data_.get_as<toml::table>("actors");
  const auto *actor_table =
      actors == nullptr ? nullptr : actors->get_as<toml::table>(actor);
  const auto *actor_config = actor_table == nullptr
                                 ? nullptr
                                 : actor_table->get_as<toml::table>("config");
  if (actor_config == nullptr) {
    return std::nullopt;
  }
  if (section_name.empty()) {
    return *actor_config;
  }
  const auto section = actor_config->at_path(section_name);
  if (!section) {
    return std::nullopt;
  }
  const auto *section_table = section.as_table();
  return section_table == nullptr ? std::nullopt
                                  : std::optional<toml::table>{*section_table};
}

auto changed_process_owned_domains(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate)
    -> std::vector<std::string> {
  std::vector<std::string> changed;
  if (active.bots != candidate.bots) {
    changed.emplace_back("bots");
  }
  if (active.database_instances != candidate.database_instances) {
    changed.emplace_back("database_instances");
  }
  if (active.thread_budget != candidate.thread_budget) {
    changed.emplace_back("runtime_thread_budget");
  }
  return changed;
}

auto describe_process_owned_changes(
    const ProcessOwnedConfigFingerprint &active,
    const ProcessOwnedConfigFingerprint &candidate) -> std::string {
  const auto changed = changed_process_owned_domains(active, candidate);
  std::string description;
  for (const auto &domain : changed) {
    if (!description.empty()) {
      description += ',';
    }
    description += domain;
  }
  return description;
}

} // namespace obcx::common
