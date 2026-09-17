#ifndef OBCX_INCLUDE_ONEBOT11_BOT_CLIENT_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_CLIENT_HPP_

#include "core/bot/typed_operation.hpp"
#include "onebot11/bot/operations.hpp"

namespace obcx::onebot11::bot {

template <typename Request>
concept OwnedRequest = std::same_as<Request, GetOneBotGroupMemberRequest> ||
                       std::same_as<Request, GetOneBotForwardMessageRequest> ||
                       std::same_as<Request, ResolveOneBotGroupFileRequest> ||
                       std::same_as<Request, ResolveOneBotPrivateFileRequest> ||
                       std::same_as<Request, PokeOneBotGroupRequest>;

class Client {
public:
  explicit Client(obcx::bot::BotOperationGateway &gateway)
      : gateway_(gateway) {}

  template <OwnedRequest Request> auto execute(Request request) {
    return obcx::bot::invoke(gateway_, std::move(request));
  }

private:
  obcx::bot::BotOperationGateway &gateway_;
};

} // namespace obcx::onebot11::bot

#endif // OBCX_INCLUDE_ONEBOT11_BOT_CLIENT_HPP_
