#pragma once

#include "core/bot/typed_operation.hpp"
#include <algorithm>
#include <optional>

namespace obcx::bot {

struct SelfInfo {
  using obcx_bot_json_factory = void;
  BotInstallationRef installation;
  std::string user_id;
  std::string display_name;
  std::optional<std::string> username;

  void validate() const {
    installation.validate();
    detail::validate_identifier(user_id, "self user_id", 20);
    if (user_id.front() == '0' || !std::ranges::all_of(user_id, [](char c) {
          return c >= '0' && c <= '9';
        }))
      throw std::invalid_argument("self user_id must be a positive native ID");
    if (!display_name.empty())
      detail::validate_identifier(display_name, "self display_name", 2048);
    if (username)
      detail::validate_identifier(*username, "self username", 1024);
  }
  static auto from_json(const Json &document) -> SelfInfo {
    detail::require_only_keys(
        document, "SelfInfo",
        {"installation", "user_id", "display_name", "username"});
    SelfInfo result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .user_id = document.at("user_id").get<std::string>(),
        .display_name = document.at("display_name").get<std::string>()};
    if (document.contains("username"))
      result.username = document.at("username").get<std::string>();
    result.validate();
    return result;
  }
};
inline void to_json(Json &document, const SelfInfo &value) {
  value.validate();
  document = {{"installation", value.installation},
              {"user_id", value.user_id},
              {"display_name", value.display_name}};
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
    detail::require_only_keys(document, "GetSelfRequest",
                              {"action", "installation"});
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
  document = {{"action", request.action},
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
    if (result.installation != request.installation)
      throw std::invalid_argument("self result installation mismatch");
  }
};

class SelfClient {
public:
  explicit SelfClient(BotOperationGateway &gateway) : gateway_(gateway) {}
  auto execute(GetSelfRequest request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }

private:
  BotOperationGateway &gateway_;
};
} // namespace obcx::bot
