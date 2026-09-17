#ifndef OBCX_SRC_TELEGRAM_BOT_DIAGNOSTICS_HPP_
#define OBCX_SRC_TELEGRAM_BOT_DIAGNOSTICS_HPP_

#include "core/bot/operation_error.hpp"

namespace obcx::telegram::bot {

// Provider-specific credential recognition stays outside the common SDK.
inline auto redact_diagnostic(const std::string_view value) -> std::string {
  for (std::size_t colon = 0; colon < value.size(); ++colon) {
    if (value[colon] != ':') {
      continue;
    }
    auto begin = colon;
    while (begin > 0 && value[begin - 1] >= '0' && value[begin - 1] <= '9') {
      --begin;
    }
    auto end = colon + 1;
    while (end < value.size()) {
      const auto byte = value[end];
      if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')) {
        break;
      }
      ++end;
    }
    if (colon - begin >= 3U && end - colon - 1U >= 6U) {
      return "[redacted provider diagnostic]";
    }
  }
  return obcx::bot::redact_bot_diagnostic(value);
}

} // namespace obcx::telegram::bot

#endif // OBCX_SRC_TELEGRAM_BOT_DIAGNOSTICS_HPP_
