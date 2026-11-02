#pragma once

// Process-only raw native extraction. Validate before common JSON conversion,
// which accepts floating-point and oversized unsigned numeric identities.
#include "common/message_type.hpp"
#include "core/bot/validation.hpp"
#include <charconv>
#include <limits>

namespace obcx::adapter::onebot11 {
inline auto numeric_identity(const common::json &document, std::string_view key)
    -> std::optional<std::string> {
  const auto field = document.find(std::string(key));
  if (!document.is_object() || field == document.end())
    return std::nullopt;
  if (field->is_string()) {
    const auto &text = field->get_ref<const std::string &>();
    if (text.empty() || text.size() > 20)
      return std::nullopt;
    std::int64_t value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        std::to_string(value) != text)
      return std::nullopt;
    return text;
  }
  if (!field->is_number_integer() ||
      (field->is_number_unsigned() && field->get<std::uint64_t>() > INT64_MAX))
    return std::nullopt;
  return std::to_string(field->get<std::int64_t>());
}
inline void validate_event_identities(const common::json &document) {
  for (auto key :
       {"self_id", "message_id", "user_id", "group_id", "target_id"}) {
    if (document.contains(key) && !document.at(key).is_null() &&
        !numeric_identity(document, key))
      throw std::invalid_argument(
          "QQ event has a noncanonical numeric identity");
  }
  if (document.contains("sender") && document.at("sender").is_object()) {
    const auto &sender = document.at("sender");
    if (sender.contains("user_id") && !numeric_identity(sender, "user_id"))
      throw std::invalid_argument(
          "QQ sender has a noncanonical numeric identity");
  }
}
} // namespace obcx::adapter::onebot11
