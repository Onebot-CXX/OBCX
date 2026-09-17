#include "core/actor/actor_manager.hpp"

#include "common/logger.hpp"
#include "core/actor/actor_commands.hpp"
#include "core/command/command_matcher.hpp"

#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <filesystem>
#include <limits>
#include <utility>

namespace obcx::core {

namespace {

auto same_actor_path(const std::string &left, const std::string &right)
    -> bool {
  std::error_code error;
  if (std::filesystem::equivalent(left, right, error) && !error) {
    return true;
  }
  error.clear();
  const auto canonical_left = std::filesystem::weakly_canonical(left, error);
  if (error) {
    return false;
  }
  error.clear();
  const auto canonical_right = std::filesystem::weakly_canonical(right, error);
  return !error && canonical_left == canonical_right;
}

auto is_canonical_actor_input(const std::string &name) -> bool {
  if (name.empty()) {
    return false;
  }
  size_t start = 0;
  while (start < name.size()) {
    const auto separator = name.find("::", start);
    const auto end = separator == std::string::npos ? name.size() : separator;
    if (end == start) {
      return false;
    }
    const auto first = static_cast<unsigned char>(name[start]);
    if (!(std::isalpha(first) || name[start] == '_')) {
      return false;
    }
    for (size_t index = start + 1; index < end; ++index) {
      const auto character = static_cast<unsigned char>(name[index]);
      if (!(std::isalnum(character) || name[index] == '_')) {
        return false;
      }
    }
    if (separator == std::string::npos) {
      return true;
    }
    start = separator + 2;
  }
  return false;
}

auto is_canonical_command_name(const std::string &name) -> bool {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  return std::ranges::all_of(name, [](const unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
  });
}

auto contract_integer(const common::json &value)
    -> std::optional<std::int64_t> {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
  }
  if (!value.is_number_integer()) {
    return std::nullopt;
  }
  return value.get<std::int64_t>();
}

auto parse_bot_installation_types(const common::json &value, std::string &error)
    -> std::optional<std::vector<std::string>> {
  std::vector<std::string> expected_types;
  if (value.is_string()) {
    expected_types.push_back(value.get<std::string>());
  } else if (value.is_array() && !value.empty()) {
    for (const auto &entry : value) {
      if (!entry.is_string()) {
        error = "actor bot installation types must be strings";
        return std::nullopt;
      }
      expected_types.push_back(entry.get<std::string>());
    }
  } else {
    error = "actor bot installation constraints require a type or non-empty "
            "type array";
    return std::nullopt;
  }
  if (std::ranges::any_of(expected_types, [](const auto &type) {
        return !bot::detail::valid_bot_id(type);
      })) {
    error =
        "actor bot installation constraints require valid exact surface IDs";
    return std::nullopt;
  }
  std::ranges::sort(expected_types);
  if (std::ranges::adjacent_find(expected_types) != expected_types.end()) {
    error = "actor bot installation constraints contain duplicate types";
    return std::nullopt;
  }
  return expected_types;
}

auto parse_actor_contract(const char *document,
                          const std::string &exported_name, std::string &error)
    -> std::optional<ActorInputContract> {
  if (document == nullptr) {
    error = "obcx_get_actor_contract returned nullptr";
    return std::nullopt;
  }

  common::json parsed;
  try {
    parsed = common::json::parse(document);
  } catch (...) {
    error = "actor input contract is not valid JSON";
    return std::nullopt;
  }
  if (!parsed.is_object()) {
    error = "actor input contract must be a JSON object";
    return std::nullopt;
  }
  if (parsed.contains("outputs")) {
    error = "actor input contract must not declare outputs";
    return std::nullopt;
  }
  if (!parsed.contains("schema_version") ||
      (!parsed["schema_version"].is_number_unsigned() &&
       !parsed["schema_version"].is_number_integer())) {
    error = "actor input contract schema_version must be a positive integer";
    return std::nullopt;
  }
  const auto signed_schema_version = contract_integer(parsed["schema_version"]);
  if (!signed_schema_version || *signed_schema_version <= 0) {
    error = "actor input contract schema_version must be a positive integer";
    return std::nullopt;
  }
  if (*signed_schema_version != 2) {
    error = "unsupported actor input contract schema_version " +
            std::to_string(*signed_schema_version) +
            "; rebuild the actor with the matching schema-2 OBCX SDK";
    return std::nullopt;
  }
  const auto schema_version =
      static_cast<std::uint32_t>(*signed_schema_version);
  if (!parsed.contains("actor") || !parsed["actor"].is_string()) {
    error = "actor input contract actor must be a string";
    return std::nullopt;
  }
  const auto actor = parsed["actor"].get<std::string>();
  if (actor.empty() || actor != exported_name) {
    error = "actor input contract identity '" + actor +
            "' does not match exported actor name '" + exported_name + "'";
    return std::nullopt;
  }
  if (!parsed.contains("accepted_inputs") ||
      !parsed["accepted_inputs"].is_array()) {
    error = "actor input contract accepted_inputs must be an array";
    return std::nullopt;
  }

  ActorInputContract contract;
  contract.schema_version = schema_version;
  contract.actor = actor;
  for (const auto &input : parsed["accepted_inputs"]) {
    if (!input.is_string()) {
      error = "actor input contract accepted_inputs must contain strings";
      return std::nullopt;
    }
    auto canonical = input.get<std::string>();
    if (!is_canonical_actor_input(canonical)) {
      error = "actor input contract contains a malformed canonical input '" +
              canonical + "'";
      return std::nullopt;
    }
    if (!contract.accepted_input_set.insert(canonical).second) {
      error =
          "actor input contract contains duplicate input '" + canonical + "'";
      return std::nullopt;
    }
    contract.accepted_inputs.push_back(std::move(canonical));
  }
  if (contract.accepted_inputs.empty()) {
    error = "actor input contract must contain at least one accepted input";
    return std::nullopt;
  }
  if (!std::ranges::is_sorted(contract.accepted_inputs)) {
    error = "actor input contract accepted_inputs must be deterministic and "
            "sorted";
    return std::nullopt;
  }

  if (parsed.contains("commands")) {
    const auto &commands = parsed["commands"];
    if (!commands.is_array()) {
      error = "actor input contract commands must be an array";
      return std::nullopt;
    }
    std::string previous_name;
    for (const auto &entry : commands) {
      if (!entry.is_object()) {
        error = "actor input contract commands must contain objects";
        return std::nullopt;
      }
      for (const auto &[key, value] : entry.items()) {
        (void)value;
        if (key != "name" && key != "description" && key != "request_type" &&
            key != "matcher") {
          error =
              "actor command registration contains an unsupported member '" +
              key + "'";
          return std::nullopt;
        }
      }
      if (!entry.contains("name") || !entry["name"].is_string() ||
          !entry.contains("description") || !entry["description"].is_string() ||
          !entry.contains("request_type") ||
          !entry["request_type"].is_string()) {
        error = "actor command registration requires string name, "
                "description, and request_type";
        return std::nullopt;
      }
      ActorCommandRegistration registration{
          .name = entry["name"].get<std::string>(),
          .description = entry["description"].get<std::string>(),
          .request_type = entry["request_type"].get<std::string>(),
      };
      if (entry.contains("matcher")) {
        const auto &matcher = entry["matcher"];
        if (!matcher.is_object()) {
          error = "actor command matcher must be an object";
          return std::nullopt;
        }
        for (const auto &[key, value] : matcher.items()) {
          (void)value;
          if (key != "kind" && key != "pattern" && key != "mode") {
            error = "actor command matcher contains an unsupported member '" +
                    key + "'";
            return std::nullopt;
          }
        }
        if (!matcher.contains("kind") || !matcher["kind"].is_string() ||
            !matcher.contains("pattern") || !matcher["pattern"].is_string() ||
            !matcher.contains("mode") || !matcher["mode"].is_string()) {
          error = "actor command matcher requires string kind, pattern, and "
                  "mode";
          return std::nullopt;
        }
        ActorCommandMatcher parsed_matcher{
            .kind = matcher["kind"].get<std::string>(),
            .pattern = matcher["pattern"].get<std::string>(),
            .mode = matcher["mode"].get<std::string>(),
        };
        if (parsed_matcher.kind != "re2") {
          error = "actor command matcher kind is unsupported";
          return std::nullopt;
        }
        if (parsed_matcher.mode != "full") {
          error = "actor command matcher mode is unsupported";
          return std::nullopt;
        }
        const auto compiled = compile_command_re2(parsed_matcher.pattern);
        if (!compiled) {
          error = compiled.message;
          return std::nullopt;
        }
        registration.matcher.emplace(std::move(parsed_matcher));
      }
      if (!is_canonical_command_name(registration.name)) {
        error = "actor command registration contains invalid command name '" +
                registration.name + "'";
        return std::nullopt;
      }
      if (registration.name == command::help_name) {
        error = "actor command registration uses reserved command name 'help'";
        return std::nullopt;
      }
      if (registration.description.empty()) {
        error = "actor command registration description must not be empty";
        return std::nullopt;
      }
      if (!is_canonical_actor_input(registration.request_type)) {
        error = "actor command registration contains malformed request type '" +
                registration.request_type + "'";
        return std::nullopt;
      }
      if (!contract.accepted_input_set.contains(registration.request_type)) {
        error = "actor command registration request type is not an accepted "
                "input: " +
                registration.request_type;
        return std::nullopt;
      }
      if (!previous_name.empty() && previous_name >= registration.name) {
        error = previous_name == registration.name
                    ? "actor input contract contains duplicate command '" +
                          registration.name + "'"
                    : "actor input contract commands must be deterministic and "
                      "sorted";
        return std::nullopt;
      }
      previous_name = registration.name;
      contract.commands.push_back(std::move(registration));
    }
  }

  if (!parsed.contains("configuration")) {
    return contract;
  }
  const auto &configuration = parsed["configuration"];
  if (!configuration.is_object()) {
    error = "actor configuration contract must be a JSON object";
    return std::nullopt;
  }
  for (const auto &[key, value] : configuration.items()) {
    (void)value;
    if (key != "integers" && key != "required_strings" &&
        key != "bot_installations" && key != "bot_installation_collections" &&
        key != "collection_identity_references" && key != "less_equal") {
      error = "actor configuration contract contains an unsupported member '" +
              key + "'";
      return std::nullopt;
    }
  }

  std::unordered_set<std::string> configuration_keys;
  if (configuration.contains("integers")) {
    const auto &integers = configuration["integers"];
    if (!integers.is_object()) {
      error = "actor configuration contract integers must be an object";
      return std::nullopt;
    }
    for (const auto &[key, constraint] : integers.items()) {
      if (key.empty() || !constraint.is_object() ||
          !constraint.contains("default")) {
        error = "actor integer configuration constraints require a key and "
                "default";
        return std::nullopt;
      }
      for (const auto &[member, value] : constraint.items()) {
        (void)value;
        if (member != "default" && member != "minimum" && member != "maximum") {
          error = "actor integer configuration constraint contains an "
                  "unsupported member '" +
                  member + "'";
          return std::nullopt;
        }
      }
      const auto default_value = contract_integer(constraint["default"]);
      const auto minimum = constraint.contains("minimum")
                               ? contract_integer(constraint["minimum"])
                               : std::optional<std::int64_t>{};
      const auto maximum = constraint.contains("maximum")
                               ? contract_integer(constraint["maximum"])
                               : std::optional<std::int64_t>{};
      if (!default_value ||
          (constraint.contains("minimum") && !minimum.has_value()) ||
          (constraint.contains("maximum") && !maximum.has_value()) ||
          (minimum && maximum && *minimum > *maximum) ||
          (minimum && *default_value < *minimum) ||
          (maximum && *default_value > *maximum)) {
        error = "actor integer configuration constraint is invalid for '" +
                key + "'";
        return std::nullopt;
      }
      configuration_keys.insert(key);
      contract.integer_configuration.push_back({.key = key,
                                                .default_value = *default_value,
                                                .minimum = minimum,
                                                .maximum = maximum});
    }
  }

  auto all_configuration_keys = configuration_keys;
  if (configuration.contains("required_strings")) {
    const auto &required_strings = configuration["required_strings"];
    if (!required_strings.is_array()) {
      error = "actor configuration contract required_strings must be an array";
      return std::nullopt;
    }
    for (const auto &entry : required_strings) {
      if (!entry.is_string()) {
        error = "actor configuration contract required_strings entries must "
                "be strings";
        return std::nullopt;
      }
      auto key = entry.get<std::string>();
      if (key.empty() || !all_configuration_keys.insert(key).second) {
        error = "actor configuration contract contains an invalid or duplicate "
                "required string key";
        return std::nullopt;
      }
      contract.required_string_configuration.push_back(std::move(key));
    }
  }

  if (configuration.contains("bot_installations")) {
    const auto &installations = configuration["bot_installations"];
    if (!installations.is_object()) {
      error =
          "actor configuration contract bot_installations must be an object";
      return std::nullopt;
    }
    for (const auto &[key, declaration] : installations.items()) {
      if (key.empty() || !all_configuration_keys.insert(key).second) {
        error = "actor configuration contract contains an invalid or duplicate "
                "bot installation key";
        return std::nullopt;
      }

      const common::json *types = &declaration;
      std::string alternative_group;
      if (declaration.is_object()) {
        for (const auto &[member, value] : declaration.items()) {
          (void)value;
          if (member != "types" && member != "alternative_group") {
            error = "actor bot installation constraint contains an "
                    "unsupported member '" +
                    member + "'";
            return std::nullopt;
          }
        }
        if (!declaration.contains("types")) {
          error = "actor bot installation constraint requires types";
          return std::nullopt;
        }
        types = &declaration["types"];
        if (declaration.contains("alternative_group")) {
          if (!declaration["alternative_group"].is_string()) {
            error = "actor bot installation alternative_group must be a "
                    "string";
            return std::nullopt;
          }
          alternative_group =
              declaration["alternative_group"].get<std::string>();
          if (alternative_group.empty()) {
            error = "actor bot installation alternative_group must not be "
                    "empty";
            return std::nullopt;
          }
        }
      }

      auto expected_types = parse_bot_installation_types(*types, error);
      if (!expected_types) {
        return std::nullopt;
      }
      contract.bot_installation_configuration.push_back(
          {.key = key,
           .expected_types = std::move(*expected_types),
           .alternative_group = std::move(alternative_group)});
    }
  }

  if (configuration.contains("bot_installation_collections")) {
    const auto &collections = configuration["bot_installation_collections"];
    if (!collections.is_object()) {
      error = "actor configuration contract bot_installation_collections must "
              "be an object";
      return std::nullopt;
    }
    for (const auto &[key, declaration] : collections.items()) {
      if (key.empty() || !all_configuration_keys.insert(key).second ||
          !declaration.is_object()) {
        error = "actor configuration contract contains an invalid or duplicate "
                "bot installation collection key";
        return std::nullopt;
      }
      for (const auto &[member, value] : declaration.items()) {
        (void)value;
        if (member != "minimum_items" && member != "identity" &&
            member != "bot_installations" && member != "unique_fields" &&
            member != "alternative_group") {
          error = "actor bot installation collection contains an unsupported "
                  "member '" +
                  member + "'";
          return std::nullopt;
        }
      }
      if (!declaration.contains("minimum_items") ||
          !declaration.contains("identity") ||
          !declaration.contains("bot_installations")) {
        error = "actor bot installation collection requires minimum_items, "
                "identity, and bot_installations";
        return std::nullopt;
      }
      const auto minimum = contract_integer(declaration["minimum_items"]);
      if (!minimum || *minimum <= 0 || *minimum > 1024) {
        error = "actor bot installation collection minimum_items is invalid";
        return std::nullopt;
      }
      if (!declaration["identity"].is_string()) {
        error = "actor bot installation collection identity must be a string";
        return std::nullopt;
      }
      auto identity = declaration["identity"].get<std::string>();
      if (identity.empty()) {
        error = "actor bot installation collection identity must not be empty";
        return std::nullopt;
      }
      const auto &fields = declaration["bot_installations"];
      if (!fields.is_object() || fields.empty()) {
        error = "actor bot installation collection bot_installations must be "
                "a non-empty object";
        return std::nullopt;
      }

      ActorInputContract::BotInstallationCollectionConstraint collection{
          .key = key,
          .minimum_items = static_cast<std::size_t>(*minimum),
          .identity_key = std::move(identity),
      };
      for (const auto &[field, types_value] : fields.items()) {
        if (field.empty() || field == collection.identity_key) {
          error = "actor bot installation collection contains an invalid or "
                  "duplicate item field";
          return std::nullopt;
        }
        auto expected_types = parse_bot_installation_types(types_value, error);
        if (!expected_types) {
          return std::nullopt;
        }
        collection.installation_fields.push_back(
            {.key = field, .expected_types = std::move(*expected_types)});
      }
      if (declaration.contains("unique_fields")) {
        const auto &unique_fields = declaration["unique_fields"];
        if (!unique_fields.is_array() || unique_fields.empty()) {
          error = "actor bot installation collection unique_fields must be a "
                  "non-empty array";
          return std::nullopt;
        }
        for (const auto &entry : unique_fields) {
          if (!entry.is_string() || entry.get<std::string>().empty()) {
            error = "actor bot installation collection unique_fields must "
                    "contain non-empty strings";
            return std::nullopt;
          }
          auto field = entry.get<std::string>();
          const auto declared = std::ranges::find(
              collection.installation_fields, field,
              &ActorInputContract::BotInstallationConfigurationConstraint::key);
          if (declared == collection.installation_fields.end()) {
            error = "actor bot installation collection unique_fields "
                    "references an unknown item field";
            return std::nullopt;
          }
          collection.unique_fields.push_back(std::move(field));
        }
        if (!std::ranges::is_sorted(collection.unique_fields) ||
            std::ranges::adjacent_find(collection.unique_fields) !=
                collection.unique_fields.end()) {
          error = "actor bot installation collection unique_fields must be "
                  "sorted and unique";
          return std::nullopt;
        }
      }
      if (declaration.contains("alternative_group")) {
        if (!declaration["alternative_group"].is_string()) {
          error = "actor bot installation collection alternative_group must "
                  "be a string";
          return std::nullopt;
        }
        collection.alternative_group =
            declaration["alternative_group"].get<std::string>();
        if (collection.alternative_group.empty()) {
          error = "actor bot installation collection alternative_group must "
                  "not be empty";
          return std::nullopt;
        }
      }
      contract.bot_installation_collection_configuration.push_back(
          std::move(collection));
    }
  }

  std::unordered_map<std::string, std::size_t> scalar_alternatives;
  std::unordered_map<std::string, std::size_t> collection_alternatives;
  for (const auto &constraint : contract.bot_installation_configuration) {
    if (!constraint.alternative_group.empty()) {
      ++scalar_alternatives[constraint.alternative_group];
    }
  }
  for (const auto &constraint :
       contract.bot_installation_collection_configuration) {
    if (!constraint.alternative_group.empty()) {
      ++collection_alternatives[constraint.alternative_group];
    }
  }
  for (const auto &[group, count] : scalar_alternatives) {
    (void)count;
    if (collection_alternatives[group] != 1) {
      error = "actor bot installation alternative group '" + group +
              "' must contain one collection form";
      return std::nullopt;
    }
  }
  for (const auto &[group, count] : collection_alternatives) {
    if (count != 1 || !scalar_alternatives.contains(group)) {
      error = "actor bot installation collection alternative group '" + group +
              "' must contain one scalar form";
      return std::nullopt;
    }
  }

  if (configuration.contains("collection_identity_references")) {
    const auto &references = configuration["collection_identity_references"];
    if (!references.is_array()) {
      error = "actor configuration contract collection_identity_references "
              "must be an array";
      return std::nullopt;
    }
    std::unordered_set<std::string> reference_identities;
    for (const auto &declaration : references) {
      if (!declaration.is_object()) {
        error = "actor collection identity references must contain objects";
        return std::nullopt;
      }
      for (const auto &[member, value] : declaration.items()) {
        (void)value;
        if (member != "source_key" && member != "root_section" &&
            member != "source_collections" && member != "target_collection" &&
            member != "target_identity" && member != "optional" &&
            member != "required_when_target_multiple") {
          error = "actor collection identity reference contains an "
                  "unsupported member '" +
                  member + "'";
          return std::nullopt;
        }
      }
      for (const auto required :
           {"source_key", "target_collection", "target_identity"}) {
        if (!declaration.contains(required) ||
            !declaration[required].is_string() ||
            declaration[required].get<std::string>().empty()) {
          error = "actor collection identity reference requires non-empty "
                  "source_key, target_collection, and target_identity";
          return std::nullopt;
        }
      }
      ActorInputContract::CollectionIdentityReferenceConstraint reference{
          .source_key = declaration["source_key"].get<std::string>(),
          .target_collection =
              declaration["target_collection"].get<std::string>(),
          .target_identity = declaration["target_identity"].get<std::string>(),
      };
      if (declaration.contains("root_section")) {
        if (!declaration["root_section"].is_string() ||
            declaration["root_section"].get<std::string>().empty()) {
          error = "actor collection identity reference root_section must be a "
                  "non-empty string";
          return std::nullopt;
        }
        reference.root_section = declaration["root_section"].get<std::string>();
      }
      if (declaration.contains("source_collections")) {
        const auto &collections_value = declaration["source_collections"];
        if (!collections_value.is_array() || collections_value.empty()) {
          error = "actor collection identity reference source_collections "
                  "must be a non-empty array";
          return std::nullopt;
        }
        for (const auto &entry : collections_value) {
          if (!entry.is_string() || entry.get<std::string>().empty()) {
            error = "actor collection identity reference source_collections "
                    "must contain non-empty strings";
            return std::nullopt;
          }
          reference.source_collections.push_back(entry.get<std::string>());
        }
        if (!std::ranges::is_sorted(reference.source_collections) ||
            std::ranges::adjacent_find(reference.source_collections) !=
                reference.source_collections.end()) {
          error = "actor collection identity reference source_collections "
                  "must be sorted and unique";
          return std::nullopt;
        }
      }
      if (reference.root_section.empty() !=
          reference.source_collections.empty()) {
        error = "actor collection identity reference root source requires "
                "both root_section and source_collections";
        return std::nullopt;
      }
      for (const auto boolean : {"optional", "required_when_target_multiple"}) {
        if (declaration.contains(boolean) &&
            !declaration[boolean].is_boolean()) {
          error = "actor collection identity reference boolean members must "
                  "be booleans";
          return std::nullopt;
        }
      }
      reference.optional = declaration.value("optional", false);
      reference.required_when_target_multiple =
          declaration.value("required_when_target_multiple", false);

      const auto target = std::ranges::find(
          contract.bot_installation_collection_configuration,
          reference.target_collection,
          &ActorInputContract::BotInstallationCollectionConstraint::key);
      if (target == contract.bot_installation_collection_configuration.end() ||
          target->identity_key != reference.target_identity) {
        error = "actor collection identity reference targets an unknown "
                "collection identity";
        return std::nullopt;
      }
      const auto identity = reference.root_section + "\x1f" +
                            reference.source_key + "\x1f" +
                            reference.target_collection;
      if (!reference_identities.insert(identity).second) {
        error = "actor configuration contract contains a duplicate collection "
                "identity reference";
        return std::nullopt;
      }
      contract.collection_identity_reference_configuration.push_back(
          std::move(reference));
    }
  }

  if (configuration.contains("less_equal")) {
    const auto &relations = configuration["less_equal"];
    if (!relations.is_array()) {
      error = "actor configuration contract less_equal must be an array";
      return std::nullopt;
    }
    for (const auto &relation : relations) {
      if (!relation.is_array() || relation.size() != 2 ||
          !relation[0].is_string() || !relation[1].is_string()) {
        error = "actor configuration contract less_equal entries must be "
                "two-element string arrays";
        return std::nullopt;
      }
      auto lesser = relation[0].get<std::string>();
      auto greater = relation[1].get<std::string>();
      if (!configuration_keys.contains(lesser) ||
          !configuration_keys.contains(greater)) {
        error = "actor configuration contract less_equal references an "
                "unknown integer constraint";
        return std::nullopt;
      }
      contract.less_equal_configuration.push_back(
          {.lesser = std::move(lesser), .greater = std::move(greater)});
    }
  }
  return contract;
}

} // namespace

struct ActorManager::DiscoveredActorLibrary {
  using CreateActor = void *(*)();
  using DestroyActor = void (*)(void *);
  using PrepareActor = ActorPreparationExportResult (*)(void *, ActorContext *);

  void *handle = nullptr;
  CreateActor create_actor = nullptr;
  DestroyActor destroy_actor = nullptr;
  PrepareActor prepare_actor = nullptr;
  std::string name;
  std::string version;
  std::string path;
  ActorInputContract contract;

  ~DiscoveredActorLibrary() {
    if (handle != nullptr) {
      dlclose(handle);
    }
  }
};

SafeActorWrapper::SafeActorWrapper(void *actor_ptr,
                                   std::shared_ptr<void> library_lifetime,
                                   DestroyFunc destroy_func,
                                   PrepareFunc prepare_func,
                                   std::string exported_name,
                                   std::string exported_version,
                                   ActorInputContract contract)
    : actor_ptr_(actor_ptr), library_lifetime_(std::move(library_lifetime)),
      destroy_func_(destroy_func), prepare_func_(prepare_func),
      exported_name_(std::move(exported_name)),
      exported_version_(std::move(exported_version)),
      contract_(std::move(contract)) {}

SafeActorWrapper::~SafeActorWrapper() { reset(); }

SafeActorWrapper::SafeActorWrapper(SafeActorWrapper &&other) noexcept
    : actor_ptr_(other.actor_ptr_),
      library_lifetime_(std::move(other.library_lifetime_)),
      destroy_func_(other.destroy_func_), prepare_func_(other.prepare_func_),
      exported_name_(std::move(other.exported_name_)),
      exported_version_(std::move(other.exported_version_)),
      contract_(std::move(other.contract_)) {
  other.actor_ptr_ = nullptr;
  other.destroy_func_ = nullptr;
  other.prepare_func_ = nullptr;
}

auto SafeActorWrapper::operator=(SafeActorWrapper &&other) noexcept
    -> SafeActorWrapper & {
  if (this != &other) {
    reset();
    actor_ptr_ = other.actor_ptr_;
    library_lifetime_ = std::move(other.library_lifetime_);
    destroy_func_ = other.destroy_func_;
    prepare_func_ = other.prepare_func_;
    exported_name_ = std::move(other.exported_name_);
    exported_version_ = std::move(other.exported_version_);
    contract_ = std::move(other.contract_);
    other.actor_ptr_ = nullptr;
    other.destroy_func_ = nullptr;
    other.prepare_func_ = nullptr;
  }
  return *this;
}

auto SafeActorWrapper::get() const -> IActorV2 * {
  return static_cast<IActorV2 *>(actor_ptr_);
}

auto SafeActorWrapper::operator->() const -> IActorV2 * { return get(); }

auto SafeActorWrapper::operator*() const -> IActorV2 & { return *get(); }

SafeActorWrapper::operator bool() const { return actor_ptr_ != nullptr; }

auto SafeActorWrapper::exported_name() const -> const std::string & {
  return exported_name_;
}

auto SafeActorWrapper::exported_version() const -> const std::string & {
  return exported_version_;
}

auto SafeActorWrapper::input_contract() const -> const ActorInputContract & {
  return contract_;
}

auto SafeActorWrapper::prepare_generation(ActorContext &context) const
    -> ActorPreparationResult {
  if (prepare_func_ == nullptr) {
    return ActorPreparationResult::ready();
  }
  ActorPreparationExportResult exported;
  try {
    exported = prepare_func_(actor_ptr_, &context);
  } catch (const std::exception &error) {
    return ActorPreparationResult::failed(error.what());
  } catch (...) {
    return ActorPreparationResult::failed(
        "actor generation preparation crossed the ABI with an unknown "
        "exception");
  }

  const auto message = exported.message == nullptr
                           ? std::string{}
                           : std::string{exported.message};
  switch (static_cast<ActorPreparationStatus>(exported.status)) {
  case ActorPreparationStatus::Ready:
    return ActorPreparationResult::ready();
  case ActorPreparationStatus::Failed:
    return ActorPreparationResult::failed(
        message.empty() ? "actor preparation failed" : message);
  case ActorPreparationStatus::RestartRequired:
    return ActorPreparationResult::restart_required(
        message.empty() ? "actor preparation requires process restart"
                        : message);
  }
  return ActorPreparationResult::failed(
      "actor generation preparation returned an invalid ABI status");
}

auto SafeActorWrapper::library_lifetime() const -> std::shared_ptr<void> {
  return library_lifetime_;
}

void SafeActorWrapper::reset() {
  if (actor_ptr_ && destroy_func_) {
    destroy_func_(actor_ptr_);
  }
  actor_ptr_ = nullptr;
  destroy_func_ = nullptr;
  prepare_func_ = nullptr;
  library_lifetime_.reset();
}

ActorManager::ActorManager() = default;

ActorManager::~ActorManager() { unload_all_actors(); }

void ActorManager::add_actor_directory(const std::string &directory) {
  if (std::filesystem::exists(directory) &&
      std::filesystem::is_directory(directory)) {
    actor_directories_.push_back(directory);
    OBCX_INFO("Added actor directory: {}", directory);
  } else {
    OBCX_WARN("Actor directory does not exist: {}", directory);
  }
}

auto ActorManager::load_actor(const std::string &actor_name) -> bool {
  if (is_actor_loaded(actor_name)) {
    OBCX_WARN("Actor {} is already loaded", actor_name);
    return true;
  }

  if (!discover_actor(actor_name)) {
    return false;
  }
  return activate_actor(actor_name);
}

auto ActorManager::load_actor_from_path(const std::string &actor_path) -> bool {
  for (const auto &[name, loaded] : loaded_actors_) {
    (void)name;
    if (same_actor_path(loaded.path, actor_path)) {
      return true;
    }
  }
  if (!discover_actor_from_path(actor_path)) {
    return false;
  }
  for (const auto &[name, library] : discovered_actors_) {
    if (library && same_actor_path(library->path, actor_path)) {
      return activate_actor(name);
    }
  }
  last_error_ = "discovered actor path is unavailable for activation";
  return false;
}

auto ActorManager::discover_actor(const std::string &actor_name) -> bool {
  if (is_actor_loaded(actor_name) || discovered_actors_.contains(actor_name)) {
    return true;
  }
  const auto actor_path = find_actor_file(actor_name);
  if (actor_path.empty()) {
    last_error_ = "actor not found in actor directories";
    OBCX_ERROR("Actor {} not found in actor directories", actor_name);
    return false;
  }
  return discover_actor_from_path(actor_path);
}

auto ActorManager::discover_actor_from_path(const std::string &actor_path)
    -> bool {
  last_error_.clear();
  auto library = discover_actor_library(actor_path);
  if (!library) {
    return false;
  }
  const auto actor_name = library->name;
  if (actor_name.empty()) {
    last_error_ = "actor has an empty name";
    OBCX_ERROR("Actor loaded from {} has an empty name", actor_path);
    return false;
  }
  if (is_actor_loaded(actor_name) || discovered_actors_.contains(actor_name)) {
    OBCX_WARN("Actor {} is already loaded or discovered", actor_name);
    return true;
  }

  discovered_actors_[actor_name] = std::move(library);
  OBCX_INFO("Actor {} contract discovered successfully from {}", actor_name,
            actor_path);
  return true;
}

auto ActorManager::activate_actor(const std::string &actor_name) -> bool {
  const std::string stable_actor_name = actor_name;
  if (is_actor_loaded(stable_actor_name)) {
    return true;
  }
  const auto discovered = discovered_actors_.find(stable_actor_name);
  if (discovered == discovered_actors_.end() || !discovered->second) {
    last_error_ = "actor library has not been discovered";
    return false;
  }
  auto &library = *discovered->second;
  void *actor_ptr = nullptr;
  try {
    actor_ptr = library.create_actor();
  } catch (...) {
    last_error_ = "exception during actor construction";
    return false;
  }
  if (actor_ptr == nullptr) {
    last_error_ = "obcx_create_actor_v2 returned nullptr";
    return false;
  }

  auto library_lifetime =
      std::shared_ptr<void>{library.handle, [](void *handle) {
                              if (handle != nullptr) {
                                dlclose(handle);
                              }
                            }};
  library.handle = nullptr;
  std::shared_ptr<SafeActorWrapper> wrapper;
  try {
    wrapper = std::make_shared<SafeActorWrapper>(
        actor_ptr, library_lifetime, library.destroy_actor,
        library.prepare_actor, library.name, library.version, library.contract);
  } catch (...) {
    library.destroy_actor(actor_ptr);
    last_error_ = "failed to allocate actor lifetime wrapper";
    return false;
  }

  LoadedActor loaded_actor;
  loaded_actor.wrapper = std::move(wrapper);
  loaded_actor.path = library.path;
  loaded_actors_[stable_actor_name] = std::move(loaded_actor);
  discovered_actors_.erase(discovered);
  OBCX_INFO("Actor {} constructed and activated successfully",
            stable_actor_name);
  return true;
}

auto ActorManager::activate_all_discovered() -> bool {
  const auto names = get_discovered_actor_names();
  return std::ranges::all_of(
      names, [this](const auto &name) { return activate_actor(name); });
}

auto ActorManager::prepare_actor(const std::string &actor_name,
                                 ActorContext &context) const
    -> ActorPreparationResult {
  const auto loaded = loaded_actors_.find(actor_name);
  if (loaded == loaded_actors_.end() || !loaded->second.wrapper) {
    return ActorPreparationResult::failed(
        "actor is unavailable for generation preparation");
  }
  return loaded->second.wrapper->prepare_generation(context);
}

void ActorManager::unload_actor(const std::string &actor_name) {
  const auto removed =
      loaded_actors_.erase(actor_name) + discovered_actors_.erase(actor_name);
  if (removed > 0) {
    OBCX_INFO("Actor {} unloaded", actor_name);
  }
}

void ActorManager::unload_all_actors() {
  begin_actor_retirement();
  finish_actor_retirement();
}

void ActorManager::begin_actor_retirement() {
  retiring_actor_libraries_.reserve(retiring_actor_libraries_.size() +
                                    loaded_actors_.size());
  for (const auto &[name, loaded] : loaded_actors_) {
    (void)name;
    if (loaded.wrapper) {
      retiring_actor_libraries_.push_back(loaded.wrapper->library_lifetime());
    }
  }
  loaded_actors_.clear();
  discovered_actors_.clear();
  OBCX_INFO("All actor instances retired");
}

void ActorManager::finish_actor_retirement() {
  retiring_actor_libraries_.clear();
  OBCX_INFO("All actors unloaded");
}

auto ActorManager::is_actor_loaded(const std::string &actor_name) const
    -> bool {
  return loaded_actors_.contains(actor_name);
}

auto ActorManager::get_actor(const std::string &actor_name) const
    -> IActorV2 * {
  if (const auto it = loaded_actors_.find(actor_name);
      it != loaded_actors_.end() && it->second.wrapper) {
    return it->second.wrapper->get();
  }
  return nullptr;
}

auto ActorManager::get_actor_shared(const std::string &actor_name) const
    -> std::shared_ptr<IActorV2> {
  if (const auto it = loaded_actors_.find(actor_name);
      it != loaded_actors_.end() && it->second.wrapper) {
    auto wrapper = it->second.wrapper;
    auto *actor = wrapper->get();
    return std::shared_ptr<IActorV2>(std::move(wrapper), actor);
  }
  return nullptr;
}

auto ActorManager::get_loaded_actor_names() const -> std::vector<std::string> {
  std::vector<std::string> names;
  names.reserve(loaded_actors_.size());
  for (const auto &[name, actor] : loaded_actors_) {
    (void)actor;
    names.push_back(name);
  }
  return names;
}

auto ActorManager::get_discovered_actor_names() const
    -> std::vector<std::string> {
  std::vector<std::string> names;
  names.reserve(discovered_actors_.size());
  for (const auto &[name, actor] : discovered_actors_) {
    (void)actor;
    names.push_back(name);
  }
  return names;
}

auto ActorManager::get_actor_contract(const std::string &actor_name) const
    -> const ActorInputContract * {
  if (const auto loaded = loaded_actors_.find(actor_name);
      loaded != loaded_actors_.end() && loaded->second.wrapper) {
    return &loaded->second.wrapper->input_contract();
  }
  if (const auto discovered = discovered_actors_.find(actor_name);
      discovered != discovered_actors_.end() && discovered->second) {
    return &discovered->second->contract;
  }
  return nullptr;
}

auto ActorManager::last_error() const -> const std::string & {
  return last_error_;
}

auto ActorManager::find_actor_file(const std::string &actor_name) const
    -> std::string {
  const std::vector possible_names = {
      actor_name,
      "lib" + actor_name + ".so",
      actor_name + ".so",
      "lib" + actor_name + ".dylib",
      actor_name + ".dylib",
      "lib" + actor_name + ".bundle",
      actor_name + ".bundle",
  };
  for (const auto &directory : actor_directories_) {
    for (const auto &name : possible_names) {
      const auto full_path = std::filesystem::path(directory) / name;
      if (std::filesystem::exists(full_path)) {
        return full_path.string();
      }
    }
  }
  return {};
}

auto ActorManager::discover_actor_library(const std::string &actor_path)
    -> std::unique_ptr<DiscoveredActorLibrary> {
  void *handle = dlopen(actor_path.c_str(), RTLD_NOW);
  if (!handle) {
    const char *error = dlerror();
    last_error_ = "failed to load actor library: " +
                  std::string{error ? error : "unknown dlopen error"};
    OBCX_ERROR("Failed to load actor library {}: {}", actor_path, last_error_);
    return nullptr;
  }

  using create_actor_t = DiscoveredActorLibrary::CreateActor;
  using destroy_actor_t = DiscoveredActorLibrary::DestroyActor;
  using prepare_actor_t = DiscoveredActorLibrary::PrepareActor;
  using actor_string_t = const char *(*)();
  using actor_abi_t = std::uint32_t (*)();

  auto close_with_error = [&](std::string error) {
    last_error_ = std::move(error);
    OBCX_ERROR("Failed to load actor library {}: {}", actor_path, last_error_);
    dlclose(handle);
  };
  auto required_symbol = [&](const char *symbol) -> void * {
    dlerror();
    void *resolved = dlsym(handle, symbol);
    if (const char *error = dlerror()) {
      close_with_error("failed to load " + std::string{symbol} +
                       " symbol: " + error);
      return nullptr;
    }
    return resolved;
  };
  auto optional_symbol = [&](const char *symbol) -> void * {
    dlerror();
    void *resolved = dlsym(handle, symbol);
    if (dlerror() != nullptr) {
      return nullptr;
    }
    return resolved;
  };

  auto get_actor_abi = reinterpret_cast<actor_abi_t>(
      required_symbol("obcx_get_actor_abi_generation"));
  if (!get_actor_abi) {
    return nullptr;
  }
  std::uint32_t abi_generation = 0;
  try {
    abi_generation = get_actor_abi();
  } catch (...) {
    close_with_error("actor ABI generation query threw an exception");
    return nullptr;
  }
  if (abi_generation != OBCX_ACTOR_ABI_GENERATION_V2) {
    close_with_error("unsupported actor ABI generation " +
                     std::to_string(abi_generation));
    return nullptr;
  }

  auto create_actor =
      reinterpret_cast<create_actor_t>(required_symbol("obcx_create_actor_v2"));
  if (!create_actor) {
    return nullptr;
  }
  auto destroy_actor = reinterpret_cast<destroy_actor_t>(
      required_symbol("obcx_destroy_actor_v2"));
  if (!destroy_actor) {
    return nullptr;
  }
  auto prepare_actor = reinterpret_cast<prepare_actor_t>(
      optional_symbol("obcx_prepare_actor_generation_v2"));
  auto get_actor_name = reinterpret_cast<actor_string_t>(
      required_symbol("obcx_get_actor_name_v2"));
  if (!get_actor_name) {
    return nullptr;
  }
  auto get_actor_version = reinterpret_cast<actor_string_t>(
      required_symbol("obcx_get_actor_version_v2"));
  if (!get_actor_version) {
    return nullptr;
  }
  auto get_actor_contract = reinterpret_cast<actor_string_t>(
      required_symbol("obcx_get_actor_contract"));
  if (!get_actor_contract) {
    return nullptr;
  }

  std::string exported_name;
  std::string exported_version;
  const char *contract_document = nullptr;
  try {
    if (const char *name = get_actor_name()) {
      exported_name = name;
    }
    if (const char *version = get_actor_version()) {
      exported_version = version;
    }
    contract_document = get_actor_contract();
  } catch (...) {
    close_with_error("actor metadata or contract query threw an exception");
    return nullptr;
  }
  if (exported_name.empty() || exported_name == "unknown") {
    close_with_error("actor exports an empty or unknown name");
    return nullptr;
  }
  std::string contract_error;
  auto contract =
      parse_actor_contract(contract_document, exported_name, contract_error);
  if (!contract) {
    close_with_error(std::move(contract_error));
    return nullptr;
  }

  auto library = std::make_unique<DiscoveredActorLibrary>();
  library->handle = handle;
  library->create_actor = create_actor;
  library->destroy_actor = destroy_actor;
  library->prepare_actor = prepare_actor;
  library->name = std::move(exported_name);
  library->version = std::move(exported_version);
  library->path = actor_path;
  library->contract = std::move(*contract);
  return library;
}

} // namespace obcx::core
