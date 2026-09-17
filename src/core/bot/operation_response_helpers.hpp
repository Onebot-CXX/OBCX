#ifndef OBCX_SRC_CORE_BOT_OPERATION_RESPONSE_HELPERS_HPP_
#define OBCX_SRC_CORE_BOT_OPERATION_RESPONSE_HELPERS_HPP_
#include "core/bot/messaging.hpp"

namespace obcx::core::operation_detail {
inline auto provider_id(const bot::Json &document, const std::string_view key)
    -> std::optional<std::string> {
  const auto field = std::string{key};
  if (!document.is_object() || !document.contains(field)) {
    return std::nullopt;
  }
  const auto &value = document.at(field);
  if (value.is_string()) {
    const auto id = value.get<std::string>();
    return id.empty() ? std::nullopt : std::optional{id};
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  return std::nullopt;
}

template <typename T>
inline auto provider_failure(const bot::BotOperationResult<bot::Json> &parsed)
    -> bot::BotOperationResult<T> {
  if (parsed.error) {
    return bot::BotOperationResult<T>::failure(parsed.error.value());
  }
  return bot::failed_operation<T>(
      bot::BotOperationErrorCode::MalformedResponse,
      "provider failure did not contain a typed error");
}

template <typename T>
inline auto malformed_side_effect(std::string message)
    -> bot::BotOperationResult<T> {
  return bot::failed_operation<T>(bot::BotOperationErrorCode::MalformedResponse,
                                  std::move(message), false,
                                  bot::SubmissionSafety::PossiblySubmitted);
}

template <typename T>
inline auto malformed_read(std::string message) -> bot::BotOperationResult<T> {
  return bot::failed_operation<T>(
      bot::BotOperationErrorCode::MalformedResponse, std::move(message), true,
      bot::SubmissionSafety::DefinitelyNotSubmitted);
}

inline auto parsed_value(const bot::BotOperationResult<bot::Json> &parsed)
    -> const bot::Json & {
  if (!parsed.value) {
    throw std::logic_error("successful provider parse has no value");
  }
  return parsed.value.value();
}

inline auto optional_string(const bot::Json &document,
                            const std::string_view key) -> std::string {
  const auto field = std::string{key};
  return document.is_object() && document.contains(field) &&
                 document.at(field).is_string()
             ? document.at(field).get<std::string>()
             : std::string{};
}

} // namespace obcx::core::operation_detail
#endif
