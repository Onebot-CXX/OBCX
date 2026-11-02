#include "onebot11/bot/standard_common.hpp"

namespace obcx::onebot11::bot::standard {
namespace {
void validate_byte_limit(std::size_t value) {
  if (value == 0 || value > obcx::bot::detail::domain_maximum_bytes)
    throw std::invalid_argument("standard byte limit is outside hard bounds");
}
} // namespace

void CallLimits::validate() const {
  validate_byte_limit(maximum_parameter_bytes);
  validate_byte_limit(maximum_response_bytes);
  if (timeout.count() <= 0)
    throw std::invalid_argument("standard timeout must be positive");
}

void ValueLimits::validate() const {
  validate_byte_limit(maximum_bytes);
  if (maximum_nodes == 0 ||
      maximum_nodes > obcx::bot::detail::domain_maximum_items ||
      maximum_depth > 32 ||
      maximum_text_bytes > obcx::bot::detail::domain_maximum_bytes)
    throw std::invalid_argument("standard value limits exceed hard bounds");
}

void CallControl::validate() const {
  switch (execution) {
  case ExecutionMode::Direct:
  case ExecutionMode::Async:
  case ExecutionMode::RateLimited:
    break;
  default:
    throw std::invalid_argument("invalid standard execution mode");
  }
  limits.validate();
  if (correlation)
    correlation->validate();
}

} // namespace obcx::onebot11::bot::standard
