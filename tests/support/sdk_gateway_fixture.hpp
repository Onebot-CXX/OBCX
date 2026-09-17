#ifndef OBCX_TESTS_SUPPORT_SDK_GATEWAY_FIXTURE_HPP_
#define OBCX_TESTS_SUPPORT_SDK_GATEWAY_FIXTURE_HPP_

#include "core/bot/operation_gateway.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

namespace obcx::tests {

class ReplyGateway final : public bot::BotOperationGateway {
public:
  ReplyGateway(bot::ActionId action, bot::OperationReply reply)
      : action_(std::move(action)), reply_(std::move(reply)) {}

  auto supported_actions(const bot::BotInstallationRef &installation) const
      -> bot::BotOperationResult<bot::SupportedActions> override {
    return bot::BotOperationResult<bot::SupportedActions>::success(
        {.installation = installation, .actions = {action_}});
  }
  auto invoke(bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<bot::OperationReply> override {
    envelope.validate();
    observed.emplace(std::move(envelope));
    co_return std::move(reply_);
  }

  std::optional<bot::OperationEnvelope> observed;

private:
  bot::ActionId action_;
  bot::OperationReply reply_;
};

template <typename T> auto await_sdk(boost::asio::awaitable<T> operation) -> T {
  boost::asio::io_context io;
  auto result =
      boost::asio::co_spawn(io, std::move(operation), boost::asio::use_future);
  io.run();
  return result.get();
}

} // namespace obcx::tests

#endif // OBCX_TESTS_SUPPORT_SDK_GATEWAY_FIXTURE_HPP_
