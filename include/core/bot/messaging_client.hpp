#pragma once

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
