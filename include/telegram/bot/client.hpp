#ifndef OBCX_INCLUDE_TELEGRAM_BOT_CLIENT_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_CLIENT_HPP_

#include "core/bot/typed_operation.hpp"
#include "telegram/bot/operations.hpp"

namespace obcx::telegram::bot {

template <typename Request>
concept OwnedRequest =
    std::same_as<Request, SendTelegramTopicMessageRequest> ||
    std::same_as<Request, EditTelegramMessageTextRequest> ||
    std::same_as<Request, SendTelegramPhotoRequest> ||
    std::same_as<Request, SendTelegramMediaGroupUrlsRequest> ||
    std::same_as<Request, SendTelegramMediaGroupUploadsRequest> ||
    std::same_as<Request, FetchTelegramFileRequest>;

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

} // namespace obcx::telegram::bot

#endif // OBCX_INCLUDE_TELEGRAM_BOT_CLIENT_HPP_
