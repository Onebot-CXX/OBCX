#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_GATEWAY_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_GATEWAY_HPP_

#include "core/bot/operation_result.hpp"
#include "core/bot/references.hpp"

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <vector>

namespace obcx::bot {

struct SupportedActions {
  BotInstallationRef installation;
  std::vector<ActionId> actions;

  void validate() const {
    installation.validate();
    auto sorted = actions;
    for (const auto &action : sorted) {
      action.validate();
    }
    std::ranges::sort(sorted);
    if (std::ranges::adjacent_find(sorted) != sorted.end()) {
      throw std::invalid_argument("supported actions contain a duplicate ID");
    }
  }

  [[nodiscard]] auto supports(const ActionId &action) const -> bool {
    return std::ranges::find(actions, action) != actions.end();
  }

  using obcx_bot_json_factory = void;
  static auto from_json(const Json &document) -> SupportedActions {
    detail::require_object(document, "SupportedActions");
    if (!document.contains("installation") || !document.contains("actions") ||
        !document.at("actions").is_array()) {
      throw std::invalid_argument(
          "SupportedActions requires installation and actions");
    }
    SupportedActions result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .actions = document.at("actions").get<std::vector<ActionId>>()};
    result.validate();
    return result;
  }

  auto operator==(const SupportedActions &) const -> bool = default;
};

inline void to_json(Json &document, const SupportedActions &supported) {
  supported.validate();
  auto sorted = supported.actions;
  std::ranges::sort(sorted);
  document = {{"installation", supported.installation},
              {"actions", std::move(sorted)}};
}

struct OperationEnvelope {
  BotInstallationRef installation;
  ActionId action;
  // SDK DTO values, with binary media fields. Never an arbitrary provider call.
  Json payload;

  void validate() const {
    installation.validate();
    action.validate();
    detail::require_object(payload, "operation payload");
    if (payload.contains("action") &&
        payload.at("action").get<ActionId>() != action) {
      throw std::invalid_argument(
          "operation payload action disagrees with envelope");
    }
  }
};

using OperationReply = BotOperationResult<Json>;

class BotOperationGateway {
public:
  BotOperationGateway() = default;
  BotOperationGateway(const BotOperationGateway &) = delete;
  auto operator=(const BotOperationGateway &) -> BotOperationGateway & = delete;
  BotOperationGateway(BotOperationGateway &&) = delete;
  auto operator=(BotOperationGateway &&) -> BotOperationGateway & = delete;
  virtual ~BotOperationGateway() = default;

  [[nodiscard]] virtual auto supported_actions(const BotInstallationRef &) const
      -> BotOperationResult<SupportedActions> = 0;

  // The coroutine frame must own its envelope across executor suspension.
  virtual auto invoke(OperationEnvelope envelope)
      -> boost::asio::awaitable<OperationReply> = 0;
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_GATEWAY_HPP_
