#ifndef OBCX_INCLUDE_CORE_BOT_JSON_CODEC_HPP_
#define OBCX_INCLUDE_CORE_BOT_JSON_CODEC_HPP_

#include <nlohmann/json.hpp>

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
    return T::from_json(document);
  }

  static void from_json(const json &document, T &value) {
    value = T::from_json(document);
  }

  static void to_json(json &document, const T &value) {
    ::nlohmann::to_json(document, value);
  }
};
NLOHMANN_JSON_NAMESPACE_END

#endif // OBCX_INCLUDE_CORE_BOT_JSON_CODEC_HPP_
