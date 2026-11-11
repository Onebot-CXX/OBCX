#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <unordered_map>

namespace obcx::common {

// Values come only from the secrets file named by global.secrets_file. The
// configuration document holds references, never values.
class SecretStore {
public:
  // Rejects files that are not regular or are readable by group or others.
  [[nodiscard]] static auto load(const std::filesystem::path &path)
      -> SecretStore;

  [[nodiscard]] auto find(std::string_view name) const -> const std::string *;

private:
  std::unordered_map<std::string, std::string> values_;
};

// A secret reference is exactly `{ secret = "<name>" }`.
[[nodiscard]] inline auto secret_reference_name(const toml::node *node)
    -> std::optional<std::string> {
  const auto *table = node == nullptr ? nullptr : node->as_table();
  if (table == nullptr || table->size() != 1) {
    return std::nullopt;
  }
  const auto name = table->get_as<std::string>("secret");
  if (name == nullptr || name->get().empty()) {
    return std::nullopt;
  }
  return name->get();
}

} // namespace obcx::common
