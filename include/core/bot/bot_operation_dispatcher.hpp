#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_DISPATCHER_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_DISPATCHER_HPP_

#include "core/bot/operation_registry.hpp"

#include <functional>
#include <memory>

namespace obcx::core {

using BotOperationEndpoint = OperationRegistry;

struct SuccessfulBotOperation {
  bot::BotInstallationRef installation;
  bot::ActionId action;
};

class BotOperationDispatcher final : public bot::BotOperationGateway {
public:
  using SurfaceValidator = std::function<bool(const bot::SurfaceId &)>;
  using SuccessHandler = std::function<boost::asio::awaitable<void>(
      SuccessfulBotOperation operation)>;

  explicit BotOperationDispatcher(SurfaceValidator surface_registered);
  ~BotOperationDispatcher() override;

  void register_endpoint(std::shared_ptr<OperationRegistry> endpoint);
  void set_success_handler(SuccessHandler handler);
  void seal_registrations();
  void clear_endpoints() noexcept;
  [[nodiscard]] auto endpoint_count() const noexcept -> std::size_t;

  [[nodiscard]] auto supported_actions(
      const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedActions> override;
  auto invoke(bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply> override;

private:
  struct State;
  static auto invoke_owned(std::shared_ptr<State> state,
                           bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply>;
  std::shared_ptr<State> state_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_DISPATCHER_HPP_
