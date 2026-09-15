#ifndef OBCX_INCLUDE_CORE_BOT_MESSAGING_CLIENT_HPP_
#define OBCX_INCLUDE_CORE_BOT_MESSAGING_CLIENT_HPP_

#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"

namespace obcx::bot {

class MessagingClient {
public:
  explicit MessagingClient(BotOperationGateway &gateway) : gateway_(gateway) {}

  auto execute(SendGroupMessageRequest request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }
  auto execute(SendPrivateMessageRequest request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }
  auto execute(DeleteMessageRequest request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }

private:
  BotOperationGateway &gateway_;
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_MESSAGING_CLIENT_HPP_
