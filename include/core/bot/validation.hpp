#ifndef OBCX_INCLUDE_CORE_BOT_VALIDATION_HPP_
#define OBCX_INCLUDE_CORE_BOT_VALIDATION_HPP_

#include "common/json_utils.hpp"

#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace obcx::bot {

using Json = common::json;

namespace detail {

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

} // namespace detail

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_VALIDATION_HPP_
