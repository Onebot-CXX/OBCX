#ifndef OBCX_INCLUDE_CORE_BOT_IDS_HPP_
#define OBCX_INCLUDE_CORE_BOT_IDS_HPP_

#include <nlohmann/json.hpp>

#include <compare>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace obcx::bot {
namespace detail {

// Syntax is independent of the process's registered platforms and operations.
constexpr auto valid_bot_id(const std::string_view value) noexcept -> bool {
  if (value.empty() || value.size() > 128U) {
    return false;
  }
  for (const auto byte : value) {
    if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
          byte == '.' || byte == '_' || byte == '-')) {
      return false;
    }
  }
  return true;
}

struct SurfaceIdTag {};
struct ActionIdTag {};

template <typename Tag> class StableBotId {
public:
  explicit StableBotId(const std::string_view value) : value_(checked(value)) {}

  [[nodiscard]] auto value() const noexcept -> const std::string & {
    return value_;
  }

  void validate() const { (void)checked(value_); }

  auto operator<=>(const StableBotId &) const = default;

private:
  static auto checked(const std::string_view value) -> std::string_view {
    if (!valid_bot_id(value)) {
      // Do not echo arbitrary input, which could contain credentials/controls.
      throw std::invalid_argument(
          "bot ID must contain 1..128 lowercase ASCII letters, digits, '.', "
          "'_' or '-'");
    }
    return value;
  }

  std::string value_;
};

} // namespace detail

using SurfaceId = detail::StableBotId<detail::SurfaceIdTag>;
using ActionId = detail::StableBotId<detail::ActionIdTag>;

} // namespace obcx::bot

// Returning from_json supports json::get<T>() without a default identity.
NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename Tag>
struct adl_serializer<obcx::bot::detail::StableBotId<Tag>> {
  using Id = obcx::bot::detail::StableBotId<Tag>;

  static void to_json(json &document, const Id &id) {
    id.validate();
    document = id.value();
  }

  static auto from_json(const json &document) -> Id {
    if (!document.is_string()) {
      throw std::invalid_argument("bot ID must be a string");
    }
    return Id{document.template get_ref<const std::string &>()};
  }

  static void from_json(const json &document, Id &id) {
    id = from_json(document);
  }
};
NLOHMANN_JSON_NAMESPACE_END

namespace std {
template <typename Tag> struct hash<obcx::bot::detail::StableBotId<Tag>> {
  auto operator()(const obcx::bot::detail::StableBotId<Tag> &id) const noexcept
      -> std::size_t {
    return std::hash<std::string_view>{}(id.value());
  }
};
} // namespace std

#endif // OBCX_INCLUDE_CORE_BOT_IDS_HPP_
