#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/bot/operation_gateway.hpp"

namespace obcx::onebot11::bot::standard {

enum class ExecutionMode : std::uint8_t { Direct, Async, RateLimited };

enum class Sex : std::uint8_t { Male, Female, Unknown };
enum class GroupRole : std::uint8_t { Owner, Administrator, Member };
enum class MessageKind : std::uint8_t { Private, Group };

// Explicit per-call limits; these are not configuration defaults. The shared
// connection ingress bound and caller response/result bounds remain distinct.
// These are protocol-size limits, not preparation-memory or residency caps.
struct CallLimits {
  std::size_t maximum_parameter_bytes;
  std::size_t maximum_response_bytes;
  std::chrono::milliseconds timeout;

  void validate() const;
};

struct ValueLimits {
  std::size_t maximum_bytes;
  std::size_t maximum_nodes;
  std::size_t maximum_depth;
  std::size_t maximum_text_bytes;

  void validate() const;
};

struct CallControl {
  ExecutionMode execution;
  CallLimits limits;
  std::optional<obcx::bot::OperationCorrelation> correlation;

  void validate() const;
};

} // namespace obcx::onebot11::bot::standard
