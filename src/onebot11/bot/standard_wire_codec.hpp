#pragma once

#include <string_view>

#include "core/bot/validation.hpp"
#include "onebot11/bot/standard_common.hpp"

namespace obcx::onebot11::bot::standard::detail {

// Synchronous immutable borrow; the returned tree owns all data. The caller
// must establish transport completeness/association separately and may inspect
// native headers only after this succeeds. No checkpoint is published here.
// One parser-local logical ledger charges input + retained DOM + duplicate-key
// copies, not lexer scratch or physical memory. Other codec ledgers are
// separate.
[[nodiscard]] auto parse_standard_reply(std::string_view wire,
                                        const CallLimits &calls,
                                        const ValueLimits &values)
    -> obcx::bot::Json;

} // namespace obcx::onebot11::bot::standard::detail
