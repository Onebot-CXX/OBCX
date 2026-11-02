#include "onebot11/bot/response_parser.hpp"
#include "core/bot/operation_parser_helpers.hpp"
#include "onebot11/bot/diagnostics.hpp"

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
  if (response.empty() ||
      response.size() > obcx::bot::detail::domain_maximum_bytes) {
    return malformed("OneBot", side_effecting);
  }
  const auto document = Json::parse(response, nullptr, false);
  if (document.is_discarded() || !document.is_object() ||
      !document.contains("status") || !document.at("status").is_string()) {
    return malformed("OneBot", side_effecting);
  }

  if (!document.contains("retcode") ||
      !document.at("retcode").is_number_integer() ||
      (document.at("retcode").is_number_unsigned() &&
       document.at("retcode").get<std::uint64_t>() > INT64_MAX)) {
    return malformed("OneBot", side_effecting);
  }
  const auto code = document.at("retcode").get<std::int64_t>();
  const auto &status = document.at("status").get_ref<const std::string &>();
  if (status == "failed" && code != 0)
    return onebot_rejected(document);
  // async means accepted/queued, not a proven rejection; unknown statuses or
  // contradictory status/retcode pairs cannot authorize retry after a send.
  if (status != "ok" || code != 0)
    return malformed("OneBot", side_effecting);
  if (!document.contains("data")) {
    return malformed("OneBot", side_effecting);
  }
  return BotOperationResult<Json>::success(document.at("data"));
}

} // namespace obcx::onebot11::bot
