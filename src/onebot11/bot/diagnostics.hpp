#ifndef OBCX_SRC_ONEBOT11_BOT_DIAGNOSTICS_HPP_
#define OBCX_SRC_ONEBOT11_BOT_DIAGNOSTICS_HPP_

#include "core/bot/operation_error.hpp"

namespace obcx::onebot11::bot {

// OneBot access tokens have no fixed token syntax. Its credential-bearing
// diagnostic keys and authorization/URL forms use the generic safety policy.
inline auto redact_diagnostic(const std::string_view value) -> std::string {
  return obcx::bot::redact_bot_diagnostic(value);
}

} // namespace obcx::onebot11::bot

#endif // OBCX_SRC_ONEBOT11_BOT_DIAGNOSTICS_HPP_
