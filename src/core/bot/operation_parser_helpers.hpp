#ifndef OBCX_SRC_CORE_BOT_OPERATION_PARSER_HELPERS_HPP_
#define OBCX_SRC_CORE_BOT_OPERATION_PARSER_HELPERS_HPP_
#include "core/bot/operation_result.hpp"

namespace obcx::core::parser_detail {
using bot::BotOperationError;
using bot::BotOperationErrorCode;
using bot::BotOperationResult;
using bot::Json;
using bot::SubmissionSafety;

inline auto scalar_string(const Json &value) -> std::optional<std::string> {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  return std::nullopt;
}

template <typename Redactor>
inline auto safe_message(const Json &document, const std::string_view primary,
                         const std::string_view secondary,
                         const std::string_view fallback, Redactor redact)
    -> std::string {
  for (const auto key : {primary, secondary}) {
    const auto field = std::string{key};
    if (document.contains(field) && document.at(field).is_string()) {
      const auto value = document.at(field).get<std::string>();
      if (!value.empty()) {
        return redact(value);
      }
    }
  }
  return std::string{fallback};
}

inline auto malformed(const std::string_view provider,
                      const bool side_effecting) -> BotOperationResult<Json> {
  return BotOperationResult<Json>::failure(
      {.code = BotOperationErrorCode::MalformedResponse,
       .message = std::string{provider} +
                  " returned an invalid or incomplete response",
       .retryable = !side_effecting,
       .submission_safety = side_effecting
                                ? SubmissionSafety::PossiblySubmitted
                                : SubmissionSafety::DefinitelyNotSubmitted});
}

} // namespace obcx::core::parser_detail
#endif
