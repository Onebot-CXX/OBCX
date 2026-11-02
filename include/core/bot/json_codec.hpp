#pragma once

#include "core/bot/validation.hpp"

#include <type_traits>
#include <utility>

// SDK values with required identity fields cannot be default constructed.
// Their factory builds those fields explicitly; normal ADL still owns encoding.
NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename T>
struct adl_serializer<
    T, std::void_t<typename T::obcx_bot_json_factory,
                   decltype(T::from_json(std::declval<const json &>()))>> {
  static auto from_json(const json &document) -> T {
    obcx::bot::detail::validate_domain_wire(document);
    return T::from_json(document);
  }

  static void from_json(const json &document, T &value) {
    value = from_json(document);
  }

  static void to_json(json &document, const T &value) {
    json encoded;
    ::nlohmann::to_json(encoded, value);
    obcx::bot::detail::validate_domain_wire(encoded);
    document = std::move(encoded);
  }
};
NLOHMANN_JSON_NAMESPACE_END
