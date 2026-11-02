#pragma once

#include "common/json_utils.hpp"

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace obcx::bot {

using Json = common::json;

class SendOutcomeEnvelopeBudget;

namespace detail {

inline constexpr std::size_t domain_maximum_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t domain_maximum_items = 65536;

class DomainBudgetExceeded : public std::invalid_argument {
public:
  DomainBudgetExceeded()
      : std::invalid_argument("domain work exceeds hard bound") {}
};
// Shared by factory and record codecs, including bounded evidence recovery.
class DomainWireBudget {
public:
  void nodes(std::size_t count = 1) {
    if (count > domain_maximum_items - nodes_)
      throw DomainBudgetExceeded{};
    nodes_ += count;
  }
  void bytes(std::size_t count) {
    if (count > domain_maximum_bytes - bytes_)
      throw DomainBudgetExceeded{};
    bytes_ += count;
  }
  void inspect(const Json &value, unsigned depth = 0) {
    nodes();
    if (depth > 32)
      throw DomainBudgetExceeded{};
    if (value.is_binary())
      bytes(value.get_binary().size());
    else if (value.is_string())
      bytes(value.get_ref<const std::string &>().size());
    else if (value.is_structured()) {
      for (auto it = value.begin(); it != value.end(); ++it) {
        if (value.is_object())
          bytes(it.key().size());
        inspect(it.value(), depth + 1);
      }
    }
  }

private:
  // Only checked envelope accounting can merge reservations or replace a
  // factory-identified null node. No caller-supplied numeric discounts.
  friend class ::obcx::bot::SendOutcomeEnvelopeBudget;
  std::size_t nodes_{};
  std::size_t bytes_{};
};
// Inspect the already-owned wire tree without copying any payload buffers.
inline void validate_domain_wire(const Json &document) {
  DomainWireBudget{}.inspect(document);
}

inline void require_object(const Json &document, const std::string_view type) {
  if (!document.is_object()) {
    throw std::invalid_argument(std::string{type} + " must be an object");
  }
}

inline void require_only_keys(
    const Json &document, const std::string_view type,
    const std::initializer_list<std::string_view> allowed) {
  require_object(document, type);
  for (auto entry = document.begin(); entry != document.end(); ++entry) {
    bool accepted = false;
    for (const auto key : allowed) {
      if (entry.key() == key) {
        accepted = true;
        break;
      }
    }
    if (!accepted) {
      throw std::invalid_argument(std::string{type} +
                                  " contains an unknown field");
    }
  }
}

inline auto require_string(const Json &document, const std::string_view key,
                           const std::string_view type) -> std::string {
  validate_domain_wire(document);
  const auto field = std::string{key};
  if (!document.contains(field) || !document.at(field).is_string()) {
    throw std::invalid_argument(std::string{type} + " requires string " +
                                field);
  }
  return document.at(field).get<std::string>();
}

inline void validate_identifier(const std::string_view value,
                                const std::string_view field,
                                const std::size_t maximum = 1024) {
  if (value.empty()) {
    throw std::invalid_argument(std::string{field} + " cannot be empty");
  }
  if (value.size() > maximum) {
    throw std::invalid_argument(std::string{field} + " exceeds its limit");
  }
  for (const auto character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20U || byte == 0x7FU) {
      throw std::invalid_argument(std::string{field} +
                                  " contains a control character");
    }
  }
}

// The returned reference is checked before callers construct an owned field.
inline auto require_identifier(const Json &document, std::string_view key,
                               std::string_view type, std::size_t maximum)
    -> const std::string & {
  const auto field = std::string{key};
  if (!document.contains(field) || !document.at(field).is_string())
    throw std::invalid_argument(std::string{type} + " requires string " +
                                field);
  const auto &value = document.at(field).get_ref<const std::string &>();
  validate_identifier(value, key, maximum);
  return value;
}
inline void preflight_optional_identifier(const Json &document,
                                          std::string_view key,
                                          std::string_view type,
                                          std::size_t maximum) {
  if (!document.at(std::string{key}).is_null())
    (void)require_identifier(document, key, type, maximum);
}

} // namespace detail

} // namespace obcx::bot
