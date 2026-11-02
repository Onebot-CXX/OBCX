#pragma once

#include "core/bot/typed_operation.hpp"
#include <algorithm>
#include <optional>

namespace obcx::bot {

struct SelfInfo {
  using obcx_bot_json_factory = void;
  UserRef user;
  std::optional<std::string> display_name;
  std::optional<std::string> username;

  void validate() const {
    user.validate();
    if (display_name)
      detail::validate_identifier(*display_name, "self display_name", 2048);
    if (username)
      detail::validate_identifier(*username, "self username", 1024);
  }
  static auto from_json(const Json &document) -> SelfInfo {
    detail::validate_domain_wire(document);
    detail::require_only_keys(document, "SelfInfo",
                              {"schema", "user", "display_name", "username"});
    if (!document.at("schema").is_number_integer() ||
        document.at("schema") != 1)
      throw std::invalid_argument("SelfInfo schema mismatch");
    SelfInfo result{.user = document.at("user").get<UserRef>(),
                    .display_name = std::nullopt,
                    .username = std::nullopt};
    if (document.contains("display_name"))
      result.display_name = detail::require_identifier(document, "display_name",
                                                       "SelfInfo", 2048);
    if (document.contains("username"))
      result.username =
          detail::require_identifier(document, "username", "SelfInfo", 1024);
    result.validate();
    return result;
  }
};
inline void to_json(Json &document, const SelfInfo &value) {
  value.validate();
  document = {{"schema", 1}, {"user", value.user}};
  if (value.display_name)
    document["display_name"] = *value.display_name;
  if (value.username)
    document["username"] = *value.username;
}

// Always a cache read; never starts or waits for a provider query.
struct GetSelfRequest {
  using obcx_bot_json_factory = void;
  inline static const ActionId action{"bot.self.get"};
  BotInstallationRef installation;
  void validate() const { installation.validate(); }
  static auto from_json(const Json &document) -> GetSelfRequest {
    detail::validate_domain_wire(document);
    detail::require_only_keys(document, "GetSelfRequest",
                              {"schema", "action", "installation"});
    if (!document.at("schema").is_number_integer() ||
        document.at("schema") != 1)
      throw std::invalid_argument("GetSelfRequest schema mismatch");
    if (document.contains("action") &&
        document.at("action").get<ActionId>() != action)
      throw std::invalid_argument("GetSelfRequest action mismatch");
    GetSelfRequest result{
        document.at("installation").get<BotInstallationRef>()};
    result.validate();
    return result;
  }
};
inline void to_json(Json &document, const GetSelfRequest &request) {
  request.validate();
  document = {{"schema", 1},
              {"action", request.action},
              {"installation", request.installation}};
}

template <>
struct OperationTraits<GetSelfRequest>
    : OperationContract<GetSelfRequest, SelfInfo, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == SurfaceId{"onebot11.qq"} ||
           surface == SurfaceId{"telegram.bot_api"};
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.user.installation != request.installation)
      throw std::invalid_argument("self result installation mismatch");
  }
};

} // namespace obcx::bot
