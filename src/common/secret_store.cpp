#include "common/secret_store.hpp"
#include "core/bot/configuration_error.hpp"

#include <system_error>

namespace obcx::common {
namespace {

[[noreturn]] void secret_error(std::string code, const std::string &message) {
  throw core::BotConfigurationError(std::move(code), "global.secrets_file",
                                    message);
}

} // namespace

auto SecretStore::load(const std::filesystem::path &path) -> SecretStore {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::is_regular_file(status)) {
    secret_error("secrets_file_unavailable",
                 "global.secrets_file must name an existing regular file");
  }
  constexpr auto shared =
      std::filesystem::perms::group_all | std::filesystem::perms::others_all;
  if ((status.permissions() & shared) != std::filesystem::perms::none) {
    secret_error("secrets_file_permissions",
                 "global.secrets_file must not be accessible by group or "
                 "others (chmod 600)");
  }
  toml::table document;
  try {
    document = toml::parse_file(path.string());
  } catch (const toml::parse_error &) {
    // Parser diagnostics may quote secret values.
    secret_error("secrets_file_parse_failed",
                 "global.secrets_file is not valid TOML");
  }
  SecretStore store;
  for (const auto &[key, node] : document) {
    const std::string name{key.str()};
    const auto *value = node.as_string();
    if (name.empty() || value == nullptr) {
      secret_error("invalid_secret_entry", "global.secrets_file entry " + name +
                                               " must be a top-level string");
    }
    store.values_.emplace(name, value->get());
  }
  return store;
}

auto SecretStore::find(const std::string_view name) const
    -> const std::string * {
  const auto found = values_.find(std::string{name});
  return found == values_.end() ? nullptr : &found->second;
}

} // namespace obcx::common
