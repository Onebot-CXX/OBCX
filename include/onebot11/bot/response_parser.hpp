#ifndef OBCX_INCLUDE_ONEBOT11_BOT_RESPONSE_PARSER_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_RESPONSE_PARSER_HPP_

// Process-only provider parser; not part of the installed Actor SDK.
#include "core/bot/operation_result.hpp"

namespace obcx::onebot11::bot {
[[nodiscard]] auto parse_onebot11_operation_response(std::string_view response,
                                                     bool side_effecting)
    -> obcx::bot::BotOperationResult<obcx::bot::Json>;
} // namespace obcx::onebot11::bot

#endif
