#pragma once
#include "core/bot/operation_traits.hpp"
#include "onebot11/bot/types.hpp"

namespace obcx::onebot11::bot {
inline void validate_message_lookup(const obcx::bot::BotMessageRef &message) {
  message.validate();
  detail::require_onebot(message.group.installation, "OneBot message lookup");
  detail::validate_user_id(message.group.native_group_id);
  const auto &id = message.native_message_id;
  std::int64_t number{};
  const auto [end, error] =
      std::from_chars(id.data(), id.data() + id.size(), number);
  if (error != std::errc{} || end != id.data() + id.size() ||
      std::to_string(number) != id)
    throw std::invalid_argument(
        "OneBot message lookup requires a canonical message ID");
}
struct OneBotMessageAuthor {
  using obcx_bot_json_factory = void;
  obcx::bot::BotMessageRef message;
  std::string user_id;
  void validate() const {
    validate_message_lookup(message);
    detail::validate_user_id(user_id);
  }
  static auto from_json(const Json &j) -> OneBotMessageAuthor {
    obcx::bot::detail::require_only_keys(j, "OneBotMessageAuthor",
                                         {"message", "user_id"});
    OneBotMessageAuthor value{j.at("message").get<obcx::bot::BotMessageRef>(),
                              j.at("user_id").get<std::string>()};
    value.validate();
    return value;
  }
};
inline void to_json(Json &j, const OneBotMessageAuthor &value) {
  value.validate();
  j = {{"message", value.message}, {"user_id", value.user_id}};
}
struct GetOneBotMessageRequest {
  using obcx_bot_json_factory = void;
  inline static const obcx::bot::ActionId action{"onebot11.message.get"};
  obcx::bot::BotMessageRef message;
  void validate() const { validate_message_lookup(message); }
  static auto from_json(const Json &j) -> GetOneBotMessageRequest {
    obcx::bot::detail::require_only_keys(j, "GetOneBotMessageRequest",
                                         {"action", "message"});
    if (j.contains("action") &&
        j.at("action").get<obcx::bot::ActionId>() != action)
      throw std::invalid_argument("GetOneBotMessageRequest action mismatch");
    GetOneBotMessageRequest value{
        j.at("message").get<obcx::bot::BotMessageRef>()};
    value.validate();
    return value;
  }
};
inline void to_json(Json &j, const GetOneBotMessageRequest &value) {
  value.validate();
  j = {{"action", value.action}, {"message", value.message}};
}
} // namespace obcx::onebot11::bot
namespace obcx::bot {
template <>
struct OperationTraits<onebot11::bot::GetOneBotMessageRequest>
    : OperationContract<onebot11::bot::GetOneBotMessageRequest,
                        onebot11::bot::OneBotMessageAuthor, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.message.group.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.message != request.message)
      throw std::invalid_argument(
          "OneBot message author result scope mismatch");
  }
};
} // namespace obcx::bot
