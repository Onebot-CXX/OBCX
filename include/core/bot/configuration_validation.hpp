#ifndef OBCX_INCLUDE_CORE_BOT_CONFIGURATION_VALIDATION_HPP_
#define OBCX_INCLUDE_CORE_BOT_CONFIGURATION_VALIDATION_HPP_

#include "core/bot/configuration_error.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <unordered_set>

namespace obcx::core::configuration {

[[noreturn]] inline void bot_configuration_error(std::string code,
                                                 std::string path,
                                                 const std::string &message) {
  throw BotConfigurationError(std::move(code), std::move(path), message);
}

inline void validate_keys(
    const toml::table &table,
    const std::unordered_set<std::string_view> &allowed,
    const std::string_view path,
    const std::unordered_set<std::string_view> &legacy_keys) {
  for (const auto &[key, value] : table) {
    (void)value;
    const auto key_view = key.str();
    if (allowed.contains(key_view)) {
      continue;
    }
    const auto field = std::string{path} + "." + std::string{key_view};
    const auto legacy = legacy_keys.contains(key_view);
    bot_configuration_error(
        legacy ? "legacy_bot_configuration_key"
               : "unknown_bot_configuration_key",
        field,
        legacy ? field + " is a legacy key; use exact surface/transport and "
                         "explicit *_ms/use_tls fields"
               : field + " is not supported");
  }
}

inline auto required_string(const toml::table &table,
                            const std::string_view key,
                            const std::string_view path) -> std::string {
  const auto *node = table.get(key);
  if (node == nullptr) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::string>();
  if (!value || value->empty()) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be a non-empty string");
  }
  return *value;
}

inline auto required_string_value(const toml::table &table,
                                  const std::string_view key,
                                  const std::string_view path) -> std::string {
  const auto *node = table.get(key);
  if (node == nullptr) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::string>();
  if (!value) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be a string");
  }
  return *value;
}

inline auto required_bool(const toml::table &table, const std::string_view key,
                          const std::string_view path) -> bool {
  const auto *node = table.get(key);
  const auto value =
      node == nullptr ? std::optional<bool>{} : node->value<bool>();
  if (!value) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be a boolean");
  }
  return *value;
}

inline auto required_port(const toml::table &table, const std::string_view path)
    -> std::uint16_t {
  const auto *node = table.get("port");
  if (node == nullptr) {
    const auto field = std::string{path} + ".port";
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::int64_t>();
  if (!value || *value <= 0 || *value > 65'535) {
    const auto field = std::string{path} + ".port";
    bot_configuration_error("invalid_bot_configuration_value", field,
                            field + " must be an integer from 1 to 65535");
  }
  return static_cast<std::uint16_t>(*value);
}

inline auto required_duration(const toml::table &table,
                              const std::string_view key,
                              const std::string_view path)
    -> std::chrono::milliseconds {
  const auto *node = table.get(key);
  if (node == nullptr) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error("missing_bot_configuration_value", field,
                            field + " must be specified explicitly");
  }
  const auto value = node->value<std::int64_t>();
  constexpr std::int64_t maximum_duration_ms = 300'000;
  if (!value || *value <= 0 || *value > maximum_duration_ms) {
    const auto field = std::string{path} + "." + std::string{key};
    bot_configuration_error(
        "invalid_bot_configuration_value", field,
        field + " must be a positive millisecond value no greater than 300000");
  }
  return std::chrono::milliseconds{*value};
}

} // namespace obcx::core::configuration

#endif
