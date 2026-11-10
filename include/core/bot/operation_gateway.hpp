#pragma once

#include "core/bot/operation_result.hpp"
#include "core/bot/references.hpp"

#include <boost/asio/awaitable.hpp>

#include <algorithm>
#include <optional>
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
    detail::validate_bot_wire(document);
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

// Actor-known attempt matching only. Not authorization, provider idempotency,
// deduplication or retry policy. Preserve the supplied spelling exactly.
struct OperationCorrelation {
  std::string attempt_id;

  void validate() const {
    detail::validate_identifier(attempt_id, "operation attempt_id", 1024);
    detail::validate_utf8(attempt_id);
    // validate_identifier excludes C0/DEL; also exclude Unicode C1 controls.
    for (std::size_t i = 1; i < attempt_id.size(); ++i) {
      const auto previous = static_cast<unsigned char>(attempt_id[i - 1]);
      const auto current = static_cast<unsigned char>(attempt_id[i]);
      if (previous == 0xc2 && current >= 0x80 && current <= 0x9f)
        throw std::invalid_argument(
            "operation attempt_id contains a control character");
    }
  }
};

struct OperationEnvelope {
  BotInstallationRef installation;
  ActionId action;
  // SDK DTO values, with binary media fields. Never an arbitrary provider call.
  Json payload;
  std::optional<OperationCorrelation> correlation;

  void validate() const {
    installation.validate();
    action.validate();
    if (correlation)
      correlation->validate();
    detail::require_object(payload, "operation payload");
    if (payload.contains("action")) {
      const auto &payload_action = payload.at("action");
      detail::validate_bot_wire(payload_action);
      if (!payload_action.is_string()) {
        throw std::invalid_argument("bot ID must be a string");
      }
      const auto spelling =
          detail::checked_bot_id(payload_action.get_ref<const std::string &>());
      if (spelling != action.value()) {
        throw std::invalid_argument(
            "operation payload action disagrees with envelope");
      }
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
