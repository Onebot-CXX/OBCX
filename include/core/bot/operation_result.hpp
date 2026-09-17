#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_RESULT_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_RESULT_HPP_

#include "core/bot/operation_error.hpp"

#include <optional>
#include <utility>

namespace obcx::bot {

template <typename T> struct BotOperationResult {
  std::optional<T> value;
  std::optional<BotOperationError> error;

  [[nodiscard]] static auto success(T result) -> BotOperationResult {
    return {.value = std::move(result)};
  }

  [[nodiscard]] static auto failure(
      BotOperationError failure) // NOLINT(performance-unnecessary-value-param)
      -> BotOperationResult {
    failure.validate();
    return {.error = std::move(failure)};
  }

  [[nodiscard]] auto ok() const noexcept -> bool {
    return value.has_value() && !error.has_value();
  }

  void validate() const {
    if (value.has_value() == error.has_value()) {
      throw std::invalid_argument(
          "BotOperationResult requires exactly one value or error");
    }
    if (error.has_value()) {
      error->validate();
    }
  }

  auto operator==(const BotOperationResult &) const -> bool = default;
};

template <typename T>
void to_json(Json &document, const BotOperationResult<T> &result) {
  result.validate();
  if (result.ok()) {
    document = {{"ok", true}, {"value", *result.value}};
  } else {
    document = {{"ok", false}, {"error", *result.error}};
  }
}

template <typename T>
void from_json(const Json &document, BotOperationResult<T> &result) {
  detail::require_object(document, "BotOperationResult");
  if (!document.contains("ok") || !document.at("ok").is_boolean()) {
    throw std::invalid_argument("BotOperationResult requires boolean ok");
  }
  const auto succeeded = document.at("ok").get<bool>();
  if ((succeeded && document.contains("error")) ||
      (!succeeded && document.contains("value"))) {
    throw std::invalid_argument(
        "BotOperationResult contains conflicting value and error");
  }
  result.value.reset();
  result.error.reset();
  if (succeeded) {
    if (!document.contains("value")) {
      throw std::invalid_argument(
          "successful BotOperationResult requires value");
    }
    result.value = document.at("value").get<T>();
  } else {
    if (!document.contains("error")) {
      throw std::invalid_argument("failed BotOperationResult requires error");
    }
    result.error = document.at("error").get<BotOperationError>();
  }
  result.validate();
}

template <typename T>
[[nodiscard]] inline auto failed_operation(
    const BotOperationErrorCode code, std::string message,
    const bool retryable = false,
    const SubmissionSafety safety = SubmissionSafety::DefinitelyNotSubmitted)
    -> BotOperationResult<T> {
  message = redact_bot_diagnostic(message);
  return BotOperationResult<T>::failure({.code = code,
                                         .message = std::move(message),
                                         .retryable = retryable,
                                         .submission_safety = safety});
}

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_RESULT_HPP_
