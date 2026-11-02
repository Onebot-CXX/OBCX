#include "core/actor/command_source.hpp"

namespace obcx::command {

auto source_message(const CommandInvocation &invocation)
    -> bot::BotOperationResult<bot::MessageReceived> {
  try {
    auto source =
        invocation.source_event.at("message").get<bot::MessageReceived>();
    if (source.message.conversation.installation.installation_id !=
        invocation.source_bot)
      throw std::invalid_argument("command source installation mismatch");
    return bot::BotOperationResult<bot::MessageReceived>::success(
        std::move(source));
  } catch (...) {
    return bot::failed_operation<bot::MessageReceived>(
        bot::BotOperationErrorCode::InvalidRequest,
        "typed command source is unavailable");
  }
}

} // namespace obcx::command
