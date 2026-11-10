#include "onebot11/bot/standard_client.hpp"
#include "core/bot/owned_structured_operation_gateway.hpp"

#include "core/bot/structured_operation_gateway.hpp"
#include "standard_operation_binding.hpp"

namespace obcx::onebot11::bot::standard {
namespace {
namespace bot_model = obcx::bot;
using ScalarReply =
    bot_model::BotOperationResult<std::unique_ptr<SendLikeOutcome>>;

auto unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard send_like did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}

auto admission_error(const bot_model::BotOperationError &error) -> bool {
  return error.submission_safety ==
             bot_model::SubmissionSafety::DefinitelyNotSubmitted &&
         error.code != bot_model::BotOperationErrorCode::ProviderRejected &&
         error.code != bot_model::BotOperationErrorCode::MalformedResponse &&
         !error.provider_code && !error.retryable;
}
// No-data mutations and event actions need only the captured mode, not a
// retained copy of request parameters or event context.
void validate_scalar_result(ExecutionMode mode,
                            const SendLikeOutcome &outcome) {
  switch (mode) {
  case ExecutionMode::Direct:
  case ExecutionMode::Async:
  case ExecutionMode::RateLimited:
    break;
  default:
    throw std::invalid_argument("invalid standard execution mode");
  }
  outcome.validate();
  if (outcome.evidence.completion ==
          NativeCompletion::ProviderReportedCompleted &&
      mode != ExecutionMode::Direct)
    throw std::invalid_argument("standard completion mode mismatch");
  if (const auto *pending = std::get_if<AcceptedPending>(&outcome.state);
      pending && pending->requested_mode != mode)
    throw std::invalid_argument("standard pending mode mismatch");
}
auto mutation_unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard mutation did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}

template <typename Request>
auto mutation_owned(bot_model::BotOperationGateway &gateway, Request request)
    -> boost::asio::awaitable<ScalarReply> {
  using Reply = ScalarReply;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard mutation requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    const auto mode = request.control.execution;
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(mutation_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      reply.validate();
      validate_scalar_result(mode, *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(mutation_unknown_error());
      outcome->state =
          ExecutionFailure{mutation_unknown_error(), FailureStage::Projection};
      validate_scalar_result(mode, *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard mutation request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(mutation_unknown_error());
  }
}
auto send_message_unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard send did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}

template <typename Request>
requires(std::same_as<Request, SendPrivateMsgRequest> ||
         std::same_as<Request, SendGroupMsgRequest> ||
         std::same_as<Request, SendMsgRequest>)
auto send_message_owned(bot_model::BotOperationGateway &gateway,
                        Request request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<std::unique_ptr<
        typename bot_model::OperationTraits<Request>::result_type>>> {
  using Reply = bot_model::BotOperationResult<std::unique_ptr<
      typename bot_model::OperationTraits<Request>::result_type>>;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard send requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    const Request validation_context = request;
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(send_message_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
      // Send-only stronger-evidence precedence: exact owned association and
      // complete request-bound/native validation outrank a malformed outer
      // wrapper. Never erase a genuine confirmed ID or valid pending/failure
      // carrier merely because unrelated enclosing metadata is inconsistent.
      try {
        reply.validate();
      } catch (...) {
        co_return Reply::success(std::move(outcome));
      }
    } catch (...) {
      // A partially invalid owned send result cannot prove non-submission.
      // Recover only its already-validated scalar native evidence; Projection
      // with NotSubmitted is forbidden by the shared bot contract.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(send_message_unknown_error());
      outcome->state = ExecutionFailure{send_message_unknown_error(),
                                        FailureStage::Projection};
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard send request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(send_message_unknown_error());
  }
}
auto query_unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard query did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}

template <typename Request>
requires(std::same_as<Request, GetStrangerInfoRequest> ||
         std::same_as<Request, GetFriendListRequest> ||
         std::same_as<Request, GetGroupInfoRequest> ||
         std::same_as<Request, GetGroupListRequest> ||
         std::same_as<Request, GetGroupMemberInfoRequest> ||
         std::same_as<Request, GetGroupMemberListRequest> ||
         std::same_as<Request, GetGroupHonorInfoRequest>)
auto query_owned(bot_model::BotOperationGateway &gateway, Request request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<std::unique_ptr<
        typename bot_model::OperationTraits<Request>::result_type>>> {
  using Reply = bot_model::BotOperationResult<std::unique_ptr<
      typename bot_model::OperationTraits<Request>::result_type>>;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard query requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    const Request validation_context = request;
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(query_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      reply.validate();
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(query_unknown_error());
      outcome->state =
          ExecutionFailure{query_unknown_error(), FailureStage::Projection};
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard query request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(query_unknown_error());
  }
}
auto login_unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard login did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}

auto login_owned(bot_model::BotOperationGateway &gateway,
                 GetLoginInfoRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetLoginInfoOutcome>>> {
  using Reply =
      bot_model::BotOperationResult<std::unique_ptr<GetLoginInfoOutcome>>;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard login requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<GetLoginInfoRequest>::installation(request);
    auto correlation = request.control.correlation;
    const auto &action =
        bot_model::OperationTraits<GetLoginInfoRequest>::action();
    const GetLoginInfoRequest validation_context = request;
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(login_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome =
        reply.value->template take<GetLoginInfoOutcome>(installation, action);
    outcome->evidence.validate();
    try {
      reply.validate();
      bot_model::OperationTraits<GetLoginInfoRequest>::validate_result(
          validation_context, *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(login_unknown_error());
      outcome->state =
          ExecutionFailure{login_unknown_error(), FailureStage::Projection};
      bot_model::OperationTraits<GetLoginInfoRequest>::validate_result(
          validation_context, *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard login request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(login_unknown_error());
  }
}
auto service_unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard service did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}

template <typename Request>
requires(std::same_as<Request, GetCookiesRequest> ||
         std::same_as<Request, GetCsrfTokenRequest> ||
         std::same_as<Request, GetCredentialsRequest> ||
         std::same_as<Request, GetStatusRequest> ||
         std::same_as<Request, GetVersionInfoRequest> ||
         std::same_as<Request, CanSendImageRequest> ||
         std::same_as<Request, CanSendRecordRequest> ||
         std::same_as<Request, SetRestartRequest> ||
         std::same_as<Request, CleanCacheRequest>)
auto service_owned(bot_model::BotOperationGateway &gateway, Request request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<std::unique_ptr<
        typename bot_model::OperationTraits<Request>::result_type>>> {
  using Reply = bot_model::BotOperationResult<std::unique_ptr<
      typename bot_model::OperationTraits<Request>::result_type>>;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard service requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    const Request validation_context = request;
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(service_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      reply.validate();
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(service_unknown_error());
      outcome->state =
          ExecutionFailure{service_unknown_error(), FailureStage::Projection};
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard service request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(service_unknown_error());
  }
}
template <typename Request>
requires(std::same_as<Request, GetMsgRequest> ||
         std::same_as<Request, GetForwardMsgRequest>)
auto lookup_owned(bot_model::BotOperationGateway &gateway, Request request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<std::unique_ptr<
        typename bot_model::OperationTraits<Request>::result_type>>> {
  using Reply = bot_model::BotOperationResult<std::unique_ptr<
      typename bot_model::OperationTraits<Request>::result_type>>;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard query requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    const Request validation_context = request;
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(query_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      reply.validate();
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(query_unknown_error());
      outcome->state =
          ExecutionFailure{query_unknown_error(), FailureStage::Projection};
      bot_model::OperationTraits<Request>::validate_result(validation_context,
                                                           *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard query request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(query_unknown_error());
  }
}
auto media_unknown_error() -> bot_model::BotOperationError {
  return {.code = bot_model::BotOperationErrorCode::OutcomeUnknown,
          .message = "standard media did not produce a valid owned outcome",
          .retryable = false,
          .submission_safety = bot_model::SubmissionSafety::PossiblySubmitted};
}
template <typename Request>
requires(std::same_as<Request, GetImageRequest> ||
         std::same_as<Request, GetRecordRequest>)
auto media_owned(bot_model::BotOperationGateway &gateway, Request request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<std::unique_ptr<
        typename bot_model::OperationTraits<Request>::result_type>>> {
  using Reply = bot_model::BotOperationResult<std::unique_ptr<
      typename bot_model::OperationTraits<Request>::result_type>>;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard media requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    const auto validation_context = detail::media_result_context(request);
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(media_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      reply.validate();
      detail::validate_media_result(validation_context, *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(media_unknown_error());
      outcome->state =
          ExecutionFailure{media_unknown_error(), FailureStage::Projection};
      detail::validate_media_result(validation_context, *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard media request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(media_unknown_error());
  }
}

} // namespace

auto Client::send_like(SendLikeRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return send_like_owned(gateway_, std::move(request));
}

auto Client::send_like_owned(bot_model::BotOperationGateway &gateway,
                             SendLikeRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  using Reply = ScalarReply;
  auto *structured =
      dynamic_cast<bot_model::StructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard send_like requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation = request.parameters.user.installation;
    const auto mode = request.control.execution;
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<SendLikeRequest>::action();
    invoking = true;
    auto reply =
        co_await structured->invoke(std::move(request), std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->take<SendLikeOutcome>(installation, action);
    outcome->evidence.validate();
    try {
      reply.validate();
      validate_scalar_result(mode, *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(unknown_error());
      outcome->state =
          ExecutionFailure{unknown_error(), FailureStage::Projection};
      validate_scalar_result(mode, *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard send_like request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(unknown_error());
  }
}
auto Client::delete_msg(DeleteMsgRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_kick(SetGroupKickRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_ban(SetGroupBanRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_whole_ban(SetGroupWholeBanRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_admin(SetGroupAdminRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_anonymous(SetGroupAnonymousRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_card(SetGroupCardRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_name(SetGroupNameRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_leave(SetGroupLeaveRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::set_group_special_title(SetGroupSpecialTitleRequest request)
    -> boost::asio::awaitable<ScalarReply> {
  return mutation_owned(gateway_, std::move(request));
}

auto Client::get_stranger_info(GetStrangerInfoRequest request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<
        std::unique_ptr<GetStrangerInfoOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_login_info(GetLoginInfoRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetLoginInfoOutcome>>> {
  return login_owned(gateway_, std::move(request));
}

auto Client::get_friend_list(GetFriendListRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetFriendListOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_group_info(GetGroupInfoRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetGroupInfoOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_group_list(GetGroupListRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetGroupListOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_group_member_info(GetGroupMemberInfoRequest request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<
        std::unique_ptr<GetGroupMemberInfoOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_group_member_list(GetGroupMemberListRequest request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<
        std::unique_ptr<GetGroupMemberListOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_group_honor_info(GetGroupHonorInfoRequest request)
    -> boost::asio::awaitable<bot_model::BotOperationResult<
        std::unique_ptr<GetGroupHonorInfoOutcome>>> {
  return query_owned(gateway_, std::move(request));
}

auto Client::get_cookies(GetCookiesRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<GetCookiesOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::get_csrf_token(GetCsrfTokenRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetCsrfTokenOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::get_credentials(GetCredentialsRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetCredentialsOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::get_status(GetStatusRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<GetStatusOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::get_version_info(GetVersionInfoRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetVersionInfoOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::can_send_image(CanSendImageRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<CanSendImageOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::can_send_record(CanSendRecordRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<CanSendRecordOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::set_restart(SetRestartRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<SetRestartOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::clean_cache(CleanCacheRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<CleanCacheOutcome>>> {
  return service_owned(gateway_, std::move(request));
}

auto Client::send_private_msg(SendPrivateMsgRequest request)
    -> boost::asio::awaitable<
        obcx::bot::BotOperationResult<std::unique_ptr<SendPrivateMsgOutcome>>> {
  return send_message_owned(gateway_, std::move(request));
}
auto Client::send_group_msg(SendGroupMsgRequest request)
    -> boost::asio::awaitable<
        obcx::bot::BotOperationResult<std::unique_ptr<SendGroupMsgOutcome>>> {
  return send_message_owned(gateway_, std::move(request));
}
auto Client::send_msg(SendMsgRequest request) -> boost::asio::awaitable<
    obcx::bot::BotOperationResult<std::unique_ptr<SendMsgOutcome>>> {
  return send_message_owned(gateway_, std::move(request));
}
auto Client::get_msg(GetMsgRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<GetMsgOutcome>>> {
  return lookup_owned(gateway_, std::move(request));
}
auto Client::get_forward_msg(GetForwardMsgRequest request)
    -> boost::asio::awaitable<
        bot_model::BotOperationResult<std::unique_ptr<GetForwardMsgOutcome>>> {
  return lookup_owned(gateway_, std::move(request));
}
auto Client::get_image(GetImageRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<GetImageOutcome>>> {
  return media_owned(gateway_, std::move(request));
}

auto Client::get_record(GetRecordRequest request) -> boost::asio::awaitable<
    bot_model::BotOperationResult<std::unique_ptr<GetRecordOutcome>>> {
  return media_owned(gateway_, std::move(request));
}

namespace {
template <typename Request>
auto event_action_owned(bot_model::BotOperationGateway &gateway,
                        Request request)
    -> boost::asio::awaitable<ScalarReply> {
  using Reply = ScalarReply;
  auto *structured =
      dynamic_cast<bot_model::OwnedStructuredOperationGateway *>(&gateway);
  if (!structured)
    co_return Reply::failure(
        {.code = bot_model::BotOperationErrorCode::UnsupportedAction,
         .message = "standard mutation requires a structured gateway",
         .retryable = false,
         .submission_safety =
             bot_model::SubmissionSafety::DefinitelyNotSubmitted});

  bool invoking = false;
  try {
    request.validate();
    // Capture matching metadata before moving request and before invocation.
    // In particular, argument evaluation order must not consume correlation.
    const auto installation =
        bot_model::OperationTraits<Request>::installation(request);
    const auto mode = request.control.execution;
    auto correlation = request.control.correlation;
    const auto &action = bot_model::OperationTraits<Request>::action();
    invoking = true;
    auto reply = co_await structured->invoke_owned(std::move(request),
                                                   std::move(correlation));
    if (!reply.value) {
      reply.validate();
      if (admission_error(*reply.error))
        co_return Reply::failure(std::move(*reply.error));
      co_return Reply::failure(mutation_unknown_error());
    }

    // Extract first: outer validation must not discard associated native
    // evidence. Neither the concrete outcome nor its installation is copied
    // while validating/wrapping the extracted ownership.
    auto outcome = reply.value->template take<
        typename bot_model::OperationTraits<Request>::result_type>(installation,
                                                                   action);
    outcome->evidence.validate();
    try {
      reply.validate();
      validate_scalar_result(mode, *outcome);
    } catch (...) {
      // NotSubmitted/NotObserved has no native header to recover. A malformed
      // enclosing result cannot prove admission failure, and the shared bot
      // contract forbids Projection with NotSubmitted evidence.
      if (outcome->evidence.submission == Submission::NotSubmitted)
        co_return Reply::failure(mutation_unknown_error());
      outcome->state =
          ExecutionFailure{mutation_unknown_error(), FailureStage::Projection};
      validate_scalar_result(mode, *outcome);
    }
    co_return Reply::success(std::move(outcome));
  } catch (...) {
    if (!invoking)
      co_return Reply::failure(
          {.code = bot_model::BotOperationErrorCode::InvalidRequest,
           .message = "standard mutation request failed validation",
           .retryable = false,
           .submission_safety =
               bot_model::SubmissionSafety::DefinitelyNotSubmitted});
    co_return Reply::failure(mutation_unknown_error());
  }
}
} // namespace
auto Client::set_group_anonymous_ban(SetGroupAnonymousBanRequest request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        std::unique_ptr<SetGroupAnonymousBanOutcome>>> {
  return event_action_owned(gateway_, std::move(request));
}
auto Client::set_friend_add_request(SetFriendAddRequestRequest request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        std::unique_ptr<SetFriendAddRequestOutcome>>> {
  return event_action_owned(gateway_, std::move(request));
}
auto Client::set_group_add_request(SetGroupAddRequestRequest request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        std::unique_ptr<SetGroupAddRequestOutcome>>> {
  return event_action_owned(gateway_, std::move(request));
}
auto Client::handle_quick_operation(HandleQuickOperationRequest request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        std::unique_ptr<HandleQuickOperationOutcome>>> {
  return event_action_owned(gateway_, std::move(request));
}
} // namespace obcx::onebot11::bot::standard
