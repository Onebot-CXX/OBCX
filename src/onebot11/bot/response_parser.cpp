#include "onebot11/bot/response_parser.hpp"
#include "../../core/bot/operation_parser_helpers.hpp"
#include "diagnostics.hpp"

#include <limits>

namespace obcx::onebot11::bot {
namespace {
using core::parser_detail::malformed;
using core::parser_detail::safe_message;
using core::parser_detail::scalar_string;
using obcx::bot::BotOperationError;
using obcx::bot::BotOperationErrorCode;
using obcx::bot::BotOperationResult;
using obcx::bot::Json;
using obcx::bot::SubmissionSafety;

auto onebot_rejected(const Json &document) -> BotOperationResult<Json> {
  std::optional<std::string> provider_code;
  std::optional<std::int64_t> numeric_code;
  if (document.contains("retcode")) {
    provider_code = scalar_string(document.at("retcode"));
    if (document.at("retcode").is_number_integer()) {
      numeric_code = document.at("retcode").get<std::int64_t>();
    }
  }
  BotOperationError error{
      .code = BotOperationErrorCode::ProviderRejected,
      .message = safe_message(document, "message", "wording",
                              "OneBot rejected the operation",
                              onebot11::bot::redact_diagnostic),
      .provider_code = std::move(provider_code),
      // The current OneBot transport reserves negative retcodes for failures
      // before an action result is accepted; positive provider retcodes remain
      // terminal unless a later tested adapter rule says otherwise.
      .retryable = numeric_code.has_value() && *numeric_code < 0,
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  return BotOperationResult<Json>::failure(std::move(error));
}

} // namespace

auto parse_onebot11_operation_response(const std::string_view response,
                                       const bool side_effecting)
    -> obcx::bot::BotOperationResult<obcx::bot::Json> {
  if (response.empty()) {
    return malformed("OneBot", side_effecting);
  }
  const auto document = Json::parse(response, nullptr, false);
  if (document.is_discarded() || !document.is_object() ||
      !document.contains("status") || !document.at("status").is_string()) {
    return malformed("OneBot", side_effecting);
  }

  bool retcode_ok = true;
  if (document.contains("retcode")) {
    if (!document.at("retcode").is_number_integer()) {
      return malformed("OneBot", side_effecting);
    }
    retcode_ok = document.at("retcode").get<std::int64_t>() == 0;
  }
  if (document.at("status").get<std::string>() != "ok" || !retcode_ok) {
    return onebot_rejected(document);
  }
  if (!document.contains("data")) {
    return malformed("OneBot", side_effecting);
  }
  return BotOperationResult<Json>::success(document.at("data"));
}

} // namespace obcx::onebot11::bot
