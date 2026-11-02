#pragma once

#include "../../bot/standard_execution_codec.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace obcx::network::detail {
struct StandardWsEchoCandidate {
  bool standard_namespace_seen;
  std::optional<std::uint64_t> echo;
};

// Borrowed association only, never JSON/header validation or authority.
auto locate_standard_ws_echo(std::string_view frame) -> StandardWsEchoCandidate;
auto make_standard_ws_envelope(
    obcx::onebot11::bot::standard::detail::NativeAction action,
    obcx::onebot11::bot::standard::ExecutionMode mode,
    std::string_view parameters, std::uint64_t echo) -> std::string;
} // namespace obcx::network::detail
