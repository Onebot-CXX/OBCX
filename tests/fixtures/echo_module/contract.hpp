#pragma once
#include "core/bot/typed_operation.hpp"

namespace obcx::tests::echo {
inline const bot::SurfaceId surface{"test.echo"};

struct Request {
  using obcx_bot_json_factory = void;
  inline static const bot::ActionId action{"test.echo.reply"};
  bot::BotInstallationRef installation;
  std::string text;
  void validate() const {
    installation.validate();
    if (installation.surface != surface || text.empty() || text.size() > 256) {
      throw std::invalid_argument("invalid echo request");
    }
  }
  static auto from_json(const bot::Json &value) -> Request {
    if (value.contains("action") && value.at("action") != action.value()) {
      throw std::invalid_argument("echo payload action conflict");
    }
    Request result{value.at("installation").get<bot::BotInstallationRef>(),
                   value.at("text").get<std::string>()};
    result.validate();
    return result;
  }
};
inline void to_json(bot::Json &value, const Request &request) {
  request.validate();
  value = {{"installation", request.installation}, {"text", request.text}};
}
struct Result {
  std::string text;
  void validate() const {
    if (text.empty() || text.size() > 512) {
      throw std::invalid_argument("invalid echo result");
    }
  }
};
inline void to_json(bot::Json &value, const Result &result) {
  result.validate();
  value = {{"text", result.text}};
}
inline void from_json(const bot::Json &value, Result &result) {
  result.text = value.at("text").get<std::string>();
  result.validate();
}
} // namespace obcx::tests::echo

namespace obcx::bot {
template <>
struct OperationTraits<tests::echo::Request>
    : OperationContract<tests::echo::Request, tests::echo::Result, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == tests::echo::surface;
  }
  static auto installation(const tests::echo::Request &request)
      -> const BotInstallationRef & {
    return request.installation;
  }
  static void validate_result(const tests::echo::Request &request,
                              const tests::echo::Result &result) {
    request.validate();
    result.validate();
  }
};
} // namespace obcx::bot
