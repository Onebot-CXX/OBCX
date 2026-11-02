#pragma once

#include "core/actor/actor_commands.hpp"
#include "core/bot/domain_events.hpp"
#include "core/bot/operation_result.hpp"

namespace obcx::command {

// Reads only the existing typed source projection. Decoding is not live reply
// admission, archive restoration, or renewed media authority.
[[nodiscard]] auto source_message(const CommandInvocation &invocation)
    -> bot::BotOperationResult<bot::MessageReceived>;

} // namespace obcx::command
