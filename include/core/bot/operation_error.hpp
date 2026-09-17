#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_ERROR_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_ERROR_HPP_

#include "core/bot/validation.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>

namespace obcx::bot {

enum class SubmissionSafety : std::uint8_t {
  DefinitelyNotSubmitted,
  PossiblySubmitted,
};

enum class BotOperationErrorCode : std::uint8_t {
  InvalidRequest,
  RouteNotFound,
  SurfaceMismatch,
  UnsupportedAction,
  ProviderRejected,
  MalformedResponse,
  TransportFailure,
  MediaTooLarge,
  Cancelled,
  OutcomeUnknown,
};

[[nodiscard]] inline auto redact_bot_diagnostic(const std::string_view value)
    -> std::string {
  std::string lowered;
  lowered.reserve(value.size());
  for (const auto character : value) {
    lowered.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  constexpr std::array sensitive_markers = {
      std::string_view{"authorization:"}, std::string_view{"bearer "},
      std::string_view{"access_token"},   std::string_view{"api_key="},
      std::string_view{"apikey="},        std::string_view{"secret="},
      std::string_view{"token="},         std::string_view{"proxy_username"},
      std::string_view{"proxy_password"}, std::string_view{"password="},
      std::string_view{"http://"},        std::string_view{"https://"},
  };
  for (const auto marker : sensitive_markers) {
    if (lowered.contains(marker)) {
      return "[redacted provider diagnostic]";
    }
  }
  return std::string{value};
}

[[nodiscard]] constexpr auto submission_safety_id(const SubmissionSafety safety)
    -> std::string_view {
  switch (safety) {
  case SubmissionSafety::DefinitelyNotSubmitted:
    return "definitely_not_submitted";
  case SubmissionSafety::PossiblySubmitted:
    return "possibly_submitted";
  }
  return {};
}

[[nodiscard]] inline auto submission_safety_from_id(
    const std::string_view value) -> SubmissionSafety {
  if (value == "definitely_not_submitted") {
    return SubmissionSafety::DefinitelyNotSubmitted;
  }
  if (value == "possibly_submitted") {
    return SubmissionSafety::PossiblySubmitted;
  }
  throw std::invalid_argument("unsupported submission safety: " +
                              std::string{value});
}

[[nodiscard]] constexpr auto error_code_id(const BotOperationErrorCode code)
    -> std::string_view {
  switch (code) {
  case BotOperationErrorCode::InvalidRequest:
    return "invalid_request";
  case BotOperationErrorCode::RouteNotFound:
    return "route_not_found";
  case BotOperationErrorCode::SurfaceMismatch:
    return "surface_mismatch";
  case BotOperationErrorCode::UnsupportedAction:
    return "unsupported_action";
  case BotOperationErrorCode::ProviderRejected:
    return "provider_rejected";
  case BotOperationErrorCode::MalformedResponse:
    return "malformed_response";
  case BotOperationErrorCode::TransportFailure:
    return "transport_failure";
  case BotOperationErrorCode::MediaTooLarge:
    return "media_too_large";
  case BotOperationErrorCode::Cancelled:
    return "cancelled";
  case BotOperationErrorCode::OutcomeUnknown:
    return "outcome_unknown";
  }
  return {};
}

[[nodiscard]] inline auto error_code_from_id(const std::string_view value)
    -> BotOperationErrorCode {
  constexpr std::array codes = {
      BotOperationErrorCode::InvalidRequest,
      BotOperationErrorCode::RouteNotFound,
      BotOperationErrorCode::SurfaceMismatch,
      BotOperationErrorCode::UnsupportedAction,
      BotOperationErrorCode::ProviderRejected,
      BotOperationErrorCode::MalformedResponse,
      BotOperationErrorCode::TransportFailure,
      BotOperationErrorCode::MediaTooLarge,
      BotOperationErrorCode::Cancelled,
      BotOperationErrorCode::OutcomeUnknown,
  };
  for (const auto code : codes) {
    if (error_code_id(code) == value) {
      return code;
    }
  }
  throw std::invalid_argument("unsupported bot operation error code: " +
                              std::string{value});
}

struct BotOperationError {
  BotOperationErrorCode code{BotOperationErrorCode::InvalidRequest};
  std::string message;
  std::optional<std::string> provider_code;
  std::optional<std::chrono::milliseconds> retry_after;
  bool retryable{};
  SubmissionSafety submission_safety{SubmissionSafety::DefinitelyNotSubmitted};

  void validate() const {
    if (error_code_id(code).empty() ||
        submission_safety_id(submission_safety).empty()) {
      throw std::invalid_argument(
          "bot error contains an invalid code or submission safety");
    }
    detail::validate_identifier(message, "error message", 2048);
    if (redact_bot_diagnostic(message) != message) {
      throw std::invalid_argument("error message must be redacted");
    }
    if (provider_code.has_value()) {
      detail::validate_identifier(*provider_code, "provider_code", 256);
      if (redact_bot_diagnostic(*provider_code) != *provider_code) {
        throw std::invalid_argument("provider_code must be redacted");
      }
    }
    if (retry_after.has_value() && retry_after->count() < 0) {
      throw std::invalid_argument("retry_after cannot be negative");
    }
    if (code == BotOperationErrorCode::OutcomeUnknown &&
        submission_safety != SubmissionSafety::PossiblySubmitted) {
      throw std::invalid_argument("outcome_unknown must be possibly submitted");
    }
  }

  auto operator==(const BotOperationError &) const -> bool = default;
};

inline void to_json(Json &document, const SubmissionSafety safety) {
  document = submission_safety_id(safety);
}

inline void from_json(const Json &document, SubmissionSafety &safety) {
  if (!document.is_string()) {
    throw std::invalid_argument("SubmissionSafety must be a string");
  }
  safety = submission_safety_from_id(document.get<std::string>());
}

inline void to_json(Json &document, const BotOperationErrorCode code) {
  document = error_code_id(code);
}

inline void from_json(const Json &document, BotOperationErrorCode &code) {
  if (!document.is_string()) {
    throw std::invalid_argument("BotOperationErrorCode must be a string");
  }
  code = error_code_from_id(document.get<std::string>());
}

inline void to_json(Json &document, const BotOperationError &error) {
  error.validate();
  document = {{"code", error.code},
              {"message", error.message},
              {"retryable", error.retryable},
              {"submission_safety", error.submission_safety}};
  if (error.provider_code.has_value()) {
    document["provider_code"] = *error.provider_code;
  }
  if (error.retry_after.has_value()) {
    document["retry_after_ms"] = error.retry_after->count();
  }
}

inline void from_json(const Json &document, BotOperationError &error) {
  detail::require_object(document, "BotOperationError");
  if (!document.contains("code") || !document.contains("submission_safety")) {
    throw std::invalid_argument(
        "BotOperationError requires code and submission_safety");
  }
  error.code = document.at("code").get<BotOperationErrorCode>();
  error.message =
      detail::require_string(document, "message", "BotOperationError");
  if (!document.contains("retryable") ||
      !document.at("retryable").is_boolean()) {
    throw std::invalid_argument("BotOperationError requires boolean retryable");
  }
  error.retryable = document.at("retryable").get<bool>();
  error.submission_safety =
      document.at("submission_safety").get<SubmissionSafety>();
  error.provider_code.reset();
  if (document.contains("provider_code")) {
    error.provider_code =
        detail::require_string(document, "provider_code", "BotOperationError");
  }
  error.retry_after.reset();
  if (document.contains("retry_after_ms")) {
    if (!document.at("retry_after_ms").is_number_integer()) {
      throw std::invalid_argument(
          "BotOperationError retry_after_ms must be an integer");
    }
    error.retry_after = std::chrono::milliseconds{
        document.at("retry_after_ms").get<std::int64_t>()};
  }
  error.validate();
}

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_ERROR_HPP_
