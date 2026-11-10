#pragma once

#include "core/bot/bot_ids.hpp"
#include "core/bot/gateway_codec.hpp"
#include <chrono>
#include <concepts>
#include <limits>
#include <optional>
#include <tuple>
#include <variant>

namespace obcx::bot {
// Wire schema 1 is the bot DTO schema, not actor input-contract schema 3.
// Binary fields require CBOR (or the in-process Json binary value), not dump().
template <class T> struct BotFields;
namespace detail {
template <class T>
concept BotRecord = requires { BotFields<T>::fields(); };
template <class T> struct IsOptional : std::false_type {};
template <class T> struct IsOptional<std::optional<T>> : std::true_type {
  using value_type = T;
};
template <class T> struct IsVector : std::false_type {};
template <class T, class A>
struct IsVector<std::vector<T, A>> : std::true_type {
  using value_type = T;
};
template <class T> struct IsVariant : std::false_type {};
template <class... T> struct IsVariant<std::variant<T...>> : std::true_type {};
template <class T> struct MemberType;
template <class C, class T> struct MemberType<T C::*> {
  using type = T;
};
inline void validate_binary_budget(const Json &d, std::size_t item_limit,
                                   std::size_t total_limit) {
  validate_bot_wire(d);
  std::size_t total = 0;
  auto walk = [&](auto &&self, const Json &v) -> void {
    if (v.is_binary()) {
      const auto size = v.get_binary().size();
      if (size > item_limit || size > total_limit - total)
        throw std::invalid_argument("binary media exceeds request budget");
      total += size;
    } else if (v.is_structured())
      for (const auto &item : v)
        self(self, item);
  };
  walk(walk, d);
}

template <class T> struct BotDecodePreflight {
  static void validate(const Json &d) { validate_bot_wire(d); }
};

// Owning records specialize only tighter field checks; nested decoding calls
// this hook without rescanning the entire wire tree at each record.
template <class T> struct BotRecordPreflight;

// Optional borrowed encode-side accounting, before constructing the wire tree.
// Specializations preserve the existing wire limits, not allocator/RSS limits.
template <class T> struct BotEncodePreflight;

template <class T> auto bot_encode(const T &value) -> Json;
template <class T> auto bot_decode(const Json &d) -> T;
template <class T, std::size_t I = 0>
auto decode_variant(const Json &d, std::size_t index) -> T {
  if constexpr (I == std::variant_size_v<T>)
    throw std::invalid_argument("unknown bot alternative");
  else {
    if (index == I)
      return T{std::in_place_index<I>,
               bot_decode<std::variant_alternative_t<I, T>>(d)};
    return decode_variant<T, I + 1>(d, index);
  }
}
inline auto bounded_unsigned(const Json &d, std::uint64_t maximum)
    -> std::uint64_t {
  if (!d.is_number_unsigned() &&
      !(d.is_number_integer() && d.get<std::int64_t>() >= 0))
    throw std::invalid_argument("expected nonnegative integer");
  const auto n = d.get<std::uint64_t>();
  if (n > maximum)
    throw std::invalid_argument("bot integer exceeds limit");
  return n;
}
template <class T> auto bot_encode(const T &value) -> Json {
  if constexpr (BotRecord<T>) {
    if constexpr (requires { BotEncodePreflight<T>::validate(value); })
      BotEncodePreflight<T>::validate(value);
    if constexpr (requires { value.validate(); })
      value.validate();
    Json d = {{"schema", 1}};
    std::apply(
        [&](auto... field) {
          ((d[field.first] = bot_encode(value.*field.second)), ...);
        },
        BotFields<T>::fields());
    return d;
  } else if constexpr (IsOptional<T>::value) {
    return value ? bot_encode(*value) : Json(nullptr);
  } else if constexpr (std::same_as<T, std::vector<std::uint8_t>>) {
    if (value.size() > bot_maximum_bytes)
      throw std::invalid_argument("bot binary exceeds hard bound");
    return Json::binary(value);
  } else if constexpr (IsVector<T>::value) {
    if (value.size() > bot_maximum_items)
      throw std::invalid_argument("bot array exceeds hard bound");
    Json d = Json::array();
    for (const auto &item : value)
      d.push_back(bot_encode(item));
    return d;
  } else if constexpr (IsVariant<T>::value) {
    return {
        {"alternative", value.index()},
        {"value",
         std::visit([](const auto &item) { return bot_encode(item); }, value)}};
  } else if constexpr (std::is_enum_v<T>) {
    return static_cast<unsigned>(value);
  } else if constexpr (std::same_as<T, std::string>) {
    if (value.size() > bot_maximum_bytes)
      throw std::invalid_argument("bot string exceeds hard bound");
    validate_utf8(value);
    return value;
  } else
    return Json(value);
}
template <class T> auto bot_decode(const Json &d) -> T {
  if constexpr (BotRecord<T>) {
    require_object(d, "bot record");
    if (bounded_unsigned(d.at("schema"), 1) != 1)
      throw std::invalid_argument("unsupported bot schema");
    const auto fields = BotFields<T>::fields();
    if (d.size() != std::tuple_size_v<decltype(fields)> + 1)
      throw std::invalid_argument("bot record field mismatch");
    if constexpr (requires { BotRecordPreflight<T>::validate(d); })
      BotRecordPreflight<T>::validate(d);
    auto result = std::apply(
        [&](auto... field) {
          return T{
              bot_decode<typename MemberType<decltype(field.second)>::type>(
                  d.at(field.first))...};
        },
        fields);
    if constexpr (requires { result.validate(); })
      result.validate();
    return result;
  } else if constexpr (IsOptional<T>::value) {
    if (d.is_null())
      return std::nullopt;
    return bot_decode<typename IsOptional<T>::value_type>(d);
  } else if constexpr (std::same_as<T, std::vector<std::uint8_t>>) {
    if (!d.is_binary() || d.get_binary().size() > bot_maximum_bytes)
      throw std::invalid_argument("expected bounded binary media");
    return {d.get_binary().begin(), d.get_binary().end()};
  } else if constexpr (IsVector<T>::value) {
    if (!d.is_array() || d.size() > bot_maximum_items)
      throw std::invalid_argument("expected bounded bot array");
    T result;
    result.reserve(d.size());
    for (const auto &item : d)
      result.push_back(bot_decode<typename IsVector<T>::value_type>(item));
    return result;
  } else if constexpr (IsVariant<T>::value) {
    require_only_keys(d, "bot alternative", {"alternative", "value"});
    return decode_variant<T>(
        d.at("value"),
        bounded_unsigned(d.at("alternative"), std::variant_size_v<T> - 1));
  } else if constexpr (std::is_enum_v<T>) {
    return static_cast<T>(bounded_unsigned(d, 255));
  } else if constexpr (std::is_unsigned_v<T> && !std::same_as<T, bool>) {
    return static_cast<T>(bounded_unsigned(d, std::numeric_limits<T>::max()));
  } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
    if (!d.is_number_integer() ||
        (d.is_number_unsigned() &&
         d.get<std::uint64_t>() >
             static_cast<std::uint64_t>(std::numeric_limits<T>::max())))
      throw std::invalid_argument("expected bounded signed integer");
    const auto n = d.get<std::int64_t>();
    if (n < std::numeric_limits<T>::min() || n > std::numeric_limits<T>::max())
      throw std::invalid_argument("signed integer exceeds range");
    return static_cast<T>(n);
  } else if constexpr (std::same_as<T, std::string>) {
    if (!d.is_string() ||
        d.get_ref<const std::string &>().size() > bot_maximum_bytes)
      throw std::invalid_argument("expected bounded bot string");
    validate_utf8(d.get_ref<const std::string &>());
    return d.get<std::string>();
  } else
    return d.get<T>();
}
} // namespace detail

template <class T>
requires detail::BotRecord<T>
struct GatewayCodec<T> {
  static auto encode(T &value) -> Json {
    auto d = detail::bot_encode(value);
    detail::validate_bot_wire(d);
    return d;
  }
  static auto decode(const Json &payload) -> T {
    detail::BotDecodePreflight<T>::validate(payload);
    return detail::bot_decode<T>(payload);
  }
};
} // namespace obcx::bot

NLOHMANN_JSON_NAMESPACE_BEGIN
template <class T>
requires obcx::bot::detail::BotRecord<T>
struct adl_serializer<T> {
  static void to_json(json &d, const T &value) {
    auto encoded = obcx::bot::detail::bot_encode(value);
    obcx::bot::detail::validate_bot_wire(encoded);
    d = std::move(encoded);
  }
  static auto from_json(const json &d) -> T {
    obcx::bot::detail::BotDecodePreflight<T>::validate(d);
    return obcx::bot::detail::bot_decode<T>(d);
  }
  static void from_json(const json &d, T &value) { value = from_json(d); }
};
NLOHMANN_JSON_NAMESPACE_END

// Used only by the bot declarations; field order is frozen by schema 1.
#define OBCX_BOT_FIELDS(Type, ...)                                             \
  template <> struct BotFields<Type> {                                         \
    static auto fields() {                                                     \
      using V [[maybe_unused]] = Type;                                         \
      return std::make_tuple(__VA_ARGS__);                                     \
    }                                                                          \
  }
#define OBCX_BOT_FIELD(name)                                                   \
  std::pair { #name, &V::name }
