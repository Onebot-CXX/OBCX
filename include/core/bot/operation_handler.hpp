#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_HANDLER_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_HANDLER_HPP_

// Process-only binding helper. The dispatcher never knows transport exceptions.
#include "core/bot/operation_registry.hpp"
#include "network/http_client.hpp"

namespace obcx::core {

template <bot::TypedOperation Request, typename Implementation,
          typename Redactor>
auto bind_operation_handler(std::shared_ptr<Implementation> implementation,
                            Redactor redact) ->
    typename OperationDefinition<Request>::Handler {
  using Traits = bot::OperationTraits<Request>;
  using Reply = bot::BotOperationResult<typename Traits::result_type>;
  return [implementation = std::move(implementation),
          redact](const Request &request) -> boost::asio::awaitable<Reply> {
    try {
      co_return co_await implementation->execute(request);
    } catch (const std::exception &exception) {
      bool not_submitted = !Traits::side_effecting;
      if (const auto *http =
              dynamic_cast<const network::HttpClientError *>(&exception)) {
        not_submitted =
            not_submitted ||
            http->submission_state() ==
                network::HttpRequestSubmissionState::DefinitelyNotSubmitted;
      }
      bot::BotOperationError error{
          .code = not_submitted ? bot::BotOperationErrorCode::TransportFailure
                                : bot::BotOperationErrorCode::OutcomeUnknown,
          .message = redact(exception.what()),
          .retryable = not_submitted,
          .submission_safety =
              not_submitted ? bot::SubmissionSafety::DefinitelyNotSubmitted
                            : bot::SubmissionSafety::PossiblySubmitted};
      try {
        error.validate();
      } catch (...) {
        error.message = "operation transport failed";
      }
      co_return Reply::failure(std::move(error));
    }
  };
}

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_HANDLER_HPP_
