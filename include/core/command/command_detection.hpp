#ifndef OBCX_INCLUDE_CORE_COMMAND_DETECTION_HPP_
#define OBCX_INCLUDE_CORE_COMMAND_DETECTION_HPP_
#include "core/actor/actor.hpp"
#include "core/actor/actor_commands.hpp"
#include "core/command/command_matcher.hpp"
#include "core/command/command_platform_adapter.hpp"
#include <algorithm>
#include <cctype>
#include <utility>

namespace obcx::core::command_detail {
inline auto lowercase(std::string value) -> std::string {
  std::ranges::transform(value, value.begin(), [](const unsigned char byte) {
    if (byte >= 'A' && byte <= 'Z') {
      return static_cast<char>(byte - 'A' + 'a');
    }
    return static_cast<char>(byte);
  });
  return value;
}

inline auto trim_arguments(std::string_view value) -> std::string {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  return std::string{value};
}

inline auto raw_text(const MessageEnvelope &event, const std::string_view key)
    -> std::string {
  if (event.raw.is_object() && event.raw.contains(key) &&
      event.raw.at(key).is_string()) {
    return event.raw.at(key).get<std::string>();
  }
  return {};
}

inline auto command_from_token(std::string token,
                               const std::string_view arguments,
                               const std::string_view bot_target,
                               const bool require_canonical_name)
    -> std::optional<DetectedCommand> {
  if (token.empty() || token.front() != '/') {
    return std::nullopt;
  }
  token.erase(token.begin());
  auto target = std::string{};
  if (const auto separator = token.find('@'); separator != std::string::npos) {
    target = lowercase(token.substr(separator + 1));
    token.resize(separator);
  }
  token = lowercase(std::move(token));
  if (token.empty() || token.size() > command_candidate_max_bytes ||
      (require_canonical_name && !command::valid_name(token))) {
    return std::nullopt;
  }
  if (!target.empty()) {
    auto expected = lowercase(std::string{bot_target});
    if (!expected.empty() && expected.front() == '@') {
      expected.erase(expected.begin());
    }
    if (expected.empty() || target != expected) {
      return std::nullopt;
    }
  }
  return DetectedCommand{
      .name = std::move(token),
      .arguments = trim_arguments(arguments),
  };
}

} // namespace obcx::core::command_detail
#endif
