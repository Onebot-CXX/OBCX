#include "telegram/bot/response_parser.hpp"
#include "../../core/bot/operation_parser_helpers.hpp"
#include "diagnostics.hpp"

#include <limits>

namespace obcx::telegram::bot {
namespace {
using core::parser_detail::malformed;
using core::parser_detail::safe_message;
using core::parser_detail::scalar_string;
using obcx::bot::BotOperationError;
using obcx::bot::BotOperationErrorCode;
using obcx::bot::BotOperationResult;
using obcx::bot::Json;
using obcx::bot::SubmissionSafety;

auto telegram_retry_after(const Json &document)
    -> std::optional<std::chrono::milliseconds> {
  if (!document.contains("parameters") ||
      !document.at("parameters").is_object()) {
    return std::nullopt;
  }
  const auto &parameters = document.at("parameters");
  if (!parameters.contains("retry_after") ||
      !parameters.at("retry_after").is_number_integer()) {
    return std::nullopt;
  }
  const auto seconds = parameters.at("retry_after").get<std::int64_t>();
  if (seconds < 0 ||
      seconds > std::numeric_limits<std::int64_t>::max() / 1000) {
    return std::nullopt;
  }
  return std::chrono::milliseconds{seconds * 1000};
}

auto telegram_rejected(const Json &document) -> BotOperationResult<Json> {
  std::optional<std::string> provider_code;
  std::int64_t numeric_code = 0;
  if (document.contains("error_code")) {
    provider_code = scalar_string(document.at("error_code"));
    if (document.at("error_code").is_number_integer()) {
      numeric_code = document.at("error_code").get<std::int64_t>();
    }
  }
  if (provider_code) {
    *provider_code = telegram::bot::redact_diagnostic(*provider_code);
  }
  const auto retry_after = telegram_retry_after(document);
  const bool retryable = retry_after.has_value() || numeric_code == 408 ||
                         numeric_code == 429 || numeric_code >= 500;
  BotOperationError error{
      .code = BotOperationErrorCode::ProviderRejected,
      .message = safe_message(document, "description", "message",
                              "Telegram rejected the operation",
                              telegram::bot::redact_diagnostic),
      .provider_code = std::move(provider_code),
      .retry_after = retry_after,
      .retryable = retryable,
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  return BotOperationResult<Json>::failure(std::move(error));
}

} // namespace

auto parse_telegram_operation_response(const std::string_view response,
                                       const bool side_effecting)
    -> obcx::bot::BotOperationResult<obcx::bot::Json> {
  if (response.empty()) {
    return malformed("Telegram", side_effecting);
  }
  const auto document = Json::parse(response, nullptr, false);
  if (document.is_discarded() || !document.is_object() ||
      !document.contains("ok") || !document.at("ok").is_boolean()) {
    return malformed("Telegram", side_effecting);
  }
  if (!document.at("ok").get<bool>()) {
    return telegram_rejected(document);
  }
  if (!document.contains("result")) {
    return malformed("Telegram", side_effecting);
  }
  return BotOperationResult<Json>::success(document.at("result"));
}

} // namespace obcx::telegram::bot
