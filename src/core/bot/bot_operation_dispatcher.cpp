#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/infrastructure/process_staging_uuid.hpp"
#include <atomic>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace obcx::core {
namespace {

template <typename T>
auto reject(const bot::BotOperationErrorCode code, const std::string &message)
    -> bot::BotOperationResult<T> {
  return bot::BotOperationResult<T>::failure(
      {.code = code,
       .message = message,
       .retryable = false,
       .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted});
}

struct SuccessObserver {
  boost::asio::any_io_executor executor;
  BotOperationDispatcher::SuccessHandler handler;
};

struct ConfirmationObserver {
  boost::asio::any_io_executor executor;
  BotOperationDispatcher::ConfirmationHandler handler;
};

auto observe_confirmations(std::shared_ptr<const ConfirmationObserver> observer,
                           std::string attempt_id,
                           std::vector<bot::SendOutputDetails> outputs)
    -> boost::asio::awaitable<void> {
  for (auto &output : outputs) {
    try {
      co_await observer->handler({attempt_id, std::move(output)});
    } catch (...) {
      // One failed observer must not discard the remaining confirmed outputs.
    }
  }
}

auto observe_success(std::shared_ptr<const SuccessObserver> observer,
                     SuccessfulBotOperation operation)
    -> boost::asio::awaitable<void> {
  // Keep the callable itself alive: a coroutine lambda can refer to its
  // closure after its first suspension, even though invocation has returned.
  try {
    co_await observer->handler(std::move(operation));
  } catch (...) {
    // Observation cannot change an already completed provider operation.
  }
}

} // namespace

struct BotOperationDispatcher::State {
  explicit State(SurfaceValidator validator)
      : surface_registered(std::move(validator)) {}
  const SurfaceValidator surface_registered;
  mutable std::shared_mutex mutex;
  std::unordered_map<std::string, std::shared_ptr<OperationRegistry>> endpoints;
  std::shared_ptr<const SuccessObserver> success_observer;
  std::shared_ptr<const ConfirmationObserver> confirmation_observer;
  bool notifications_closed = false;
  bool sealed = false;
  bool closed = false;
};

BotOperationDispatcher::BotOperationDispatcher(
    SurfaceValidator surface_registered) {
  if (!surface_registered) {
    throw std::invalid_argument(
        "dispatcher requires registered-surface validation");
  }
  state_ = std::make_shared<State>(std::move(surface_registered));
}

BotOperationDispatcher::~BotOperationDispatcher() { clear_endpoints(); }

void BotOperationDispatcher::register_endpoint(
    std::shared_ptr<OperationRegistry> endpoint) {
  if (!endpoint) {
    throw std::invalid_argument("operation endpoint cannot be null");
  }
  const auto installation = endpoint->installation();
  installation.validate();
  if (!state_->surface_registered(installation.surface)) {
    throw std::invalid_argument("operation endpoint surface is not registered");
  }
  const auto supported = endpoint->supported_actions(installation);
  supported.validate();
  if (!supported.ok()) {
    throw std::invalid_argument(
        "operation endpoint must be prepared before registration");
  }
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed) {
    throw std::logic_error(
        "operation endpoint registration is sealed or closed");
  }
  if (!state_->endpoints
           .emplace(installation.installation_id, std::move(endpoint))
           .second) {
    throw std::invalid_argument("duplicate operation installation id");
  }
}

void BotOperationDispatcher::set_success_handler(
    boost::asio::any_io_executor executor, SuccessHandler handler) {
  if (!executor || !handler) {
    throw std::invalid_argument(
        "operation success observation requires an executor and handler");
  }
  auto observer = std::make_shared<const SuccessObserver>(std::move(executor),
                                                          std::move(handler));
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed || state_->notifications_closed) {
    throw std::logic_error(
        "operation success handler must be set before sealing or shutdown");
  }
  if (state_->success_observer) {
    throw std::logic_error("operation success handler is already set");
  }
  state_->success_observer = std::move(observer);
}

void BotOperationDispatcher::set_confirmation_handler(
    boost::asio::any_io_executor executor, ConfirmationHandler handler) {
  if (!executor || !handler)
    throw std::invalid_argument(
        "confirmation observation requires executor and handler");
  auto observer = std::make_shared<const ConfirmationObserver>(
      std::move(executor), std::move(handler));
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed || state_->notifications_closed ||
      state_->confirmation_observer)
    throw std::logic_error(
        "confirmation handler must be installed once before sealing");
  state_->confirmation_observer = std::move(observer);
}

void BotOperationDispatcher::close_success_notifications() noexcept {
  std::shared_ptr<const SuccessObserver> retired;
  std::shared_ptr<const ConfirmationObserver> retired_confirmations;
  {
    std::unique_lock lock(state_->mutex);
    state_->notifications_closed = true;
    retired.swap(state_->success_observer);
    retired_confirmations.swap(state_->confirmation_observer);
  }
  // Release callback captures and any tracked executor outside the lock.
}

void BotOperationDispatcher::seal_registrations() {
  std::unique_lock lock(state_->mutex);
  if (state_->closed) {
    throw std::logic_error("operation dispatcher is closed");
  }
  state_->sealed = true;
}

void BotOperationDispatcher::clear_endpoints() noexcept {
  close_success_notifications();
  decltype(State::endpoints) retired;
  {
    std::unique_lock lock(state_->mutex);
    state_->closed = true;
    retired.swap(state_->endpoints);
  }
  for (const auto &[id, endpoint] : retired) {
    (void)id;
    endpoint->close();
  }
}

void BotOperationDispatcher::refresh_self_identities() {
  std::shared_lock lock(state_->mutex);
  if (state_->closed)
    return;
  for (const auto &[id, endpoint] : state_->endpoints) {
    (void)id;
    endpoint->refresh_self_identity();
  }
}

auto BotOperationDispatcher::normalize_live_message(
    const bot::BotInstallationRef &installation,
    const common::MessageEvent &event, bot::TimePoint received_at,
    std::string_view observation_id,
    const std::optional<SelfIdentityOrigin> &origin) const
    -> bot::MessageReceived {
  installation.validate();
  std::shared_ptr<OperationRegistry> endpoint;
  {
    std::shared_lock lock(state_->mutex);
    if (state_->closed)
      throw std::logic_error("message normalization dispatcher is closed");
    const auto it = state_->endpoints.find(installation.installation_id);
    if (it == state_->endpoints.end() ||
        it->second->installation() != installation)
      throw std::invalid_argument("message installation was not found");
    endpoint = it->second;
  }
  return endpoint->normalize_live_message(event, received_at, observation_id,
                                          origin);
}

void BotOperationDispatcher::admit_message(const bot::MessageReceived &message,
                                           std::uint64_t expected_generation) {
  const auto &installation = message.message.conversation.installation;
  std::shared_lock lock(state_->mutex);
  if (state_->closed)
    throw std::logic_error("message admission is closed");
  const auto it = state_->endpoints.find(installation.installation_id);
  if (it == state_->endpoints.end() ||
      it->second->installation() != installation)
    throw std::invalid_argument("message installation was not found");
  if (!it->second->admit_message(message, expected_generation))
    throw std::invalid_argument(
        "message belongs to a retired evidence generation");
}

auto BotOperationDispatcher::endpoint_count() const noexcept -> std::size_t {
  std::shared_lock lock(state_->mutex);
  return state_->endpoints.size();
}

auto BotOperationDispatcher::supported_actions(
    const bot::BotInstallationRef &installation) const
    -> bot::BotOperationResult<bot::SupportedActions> {
  try {
    installation.validate();
  } catch (...) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::InvalidRequest,
        "operation installation is invalid");
  }
  std::shared_ptr<OperationRegistry> endpoint;
  {
    std::shared_lock lock(state_->mutex);
    if (state_->closed) {
      return reject<bot::SupportedActions>(
          bot::BotOperationErrorCode::Cancelled,
          "operation dispatcher is closed");
    }
    const auto found = state_->endpoints.find(installation.installation_id);
    if (found == state_->endpoints.end()) {
      return reject<bot::SupportedActions>(
          bot::BotOperationErrorCode::RouteNotFound,
          "operation installation was not found");
    }
    endpoint = found->second;
  }
  return endpoint->supported_actions(installation);
}

auto BotOperationDispatcher::capture_send_evidence(
    const std::shared_ptr<State> &state,
    const bot::BotInstallationRef &installation,
    const bot::ActionId &action) noexcept
    -> std::optional<SendEvidenceAdmission> {
  try {
    std::shared_lock lock(state->mutex);
    if (state->closed)
      return std::nullopt;
    const auto found = state->endpoints.find(installation.installation_id);
    if (found == state->endpoints.end())
      return std::nullopt;
    return OperationRegistry::capture_send_evidence(found->second->state_,
                                                    installation, action);
  } catch (...) {
    return std::nullopt;
  }
}

auto BotOperationDispatcher::invoke(bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  // Capture synchronously, before this lazy coroutine can be queued/awaited.
  // The registry receives this exact token, including a miss; never recapture.
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return invoke_owned(state_, std::move(envelope), std::move(admission));
}

auto BotOperationDispatcher::invoke_owned(
    std::shared_ptr<State> state, bot::OperationEnvelope envelope,
    std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<bot::OperationReply> {
  try {
    envelope.validate();
  } catch (...) {
    co_return reject<bot::Json>(bot::BotOperationErrorCode::InvalidRequest,
                                "operation envelope is invalid");
  }
  SuccessfulBotOperation completed{
      .installation = envelope.installation,
      .action = envelope.action,
  };
  std::shared_ptr<OperationRegistry> endpoint;
  {
    std::shared_lock lock(state->mutex);
    if (state->closed) {
      co_return reject<bot::Json>(bot::BotOperationErrorCode::Cancelled,
                                  "operation dispatcher is closed");
    }
    const auto found =
        state->endpoints.find(envelope.installation.installation_id);
    if (found == state->endpoints.end()) {
      co_return reject<bot::Json>(bot::BotOperationErrorCode::RouteNotFound,
                                  "operation installation was not found");
    }
    endpoint = found->second;
  }
  const auto correlation = envelope.correlation;
  auto completion = co_await OperationRegistry::invoke_owned(
      endpoint->state_, std::move(envelope), std::move(admission));
  // Preparing observations (including bounded copies) is best effort too. A
  // callback/queue/allocation failure must not replace the caller's typed
  // reply.
  try {
    bool logical_success = completion.reply.ok();
    std::vector<bot::SendOutputDetails> confirmed_outputs;
    if (completion.message_outcome) {
      static std::atomic_uint64_t next_attempt{0};
      completed.attempt_id =
          correlation ? correlation->attempt_id
                      : std::string{detail::process_staging_uuid()} + ":send:" +
                            std::to_string(next_attempt.fetch_add(
                                1, std::memory_order_relaxed));
      const auto *receipt =
          std::get_if<bot::SendReceipt>(&*completion.message_outcome);
      logical_success = completion.reply.ok() && receipt;
      if (logical_success)
        completed.send_receipt = *receipt;
      confirmed_outputs = std::visit(
          [](auto &value) { return std::move(value.output_details); },
          *completion.message_outcome);
    }
    if (!confirmed_outputs.empty()) {
      try {
        std::shared_lock lock(state->mutex);
        if (const auto &observer = state->confirmation_observer) {
          boost::asio::post(observer->executor,
                            [observer, attempt = completed.attempt_id,
                             outputs = std::move(confirmed_outputs)]() mutable {
                              try {
                                boost::asio::co_spawn(
                                    observer->executor,
                                    observe_confirmations(observer,
                                                          std::move(attempt),
                                                          std::move(outputs)),
                                    boost::asio::detached);
                              } catch (...) {
                              }
                            });
        }
      } catch (...) {
        // Notification admission/failure never changes the completed outcome.
      }
    }
    if (logical_success) {
      try {
        // Enqueue and admission closure share this lock. Do not snapshot the
        // observer before provider I/O: it could enqueue after executor drain.
        std::shared_lock lock(state->mutex);
        if (const auto &observer = state->success_observer) {
          boost::asio::post(observer->executor, [observer, completed] {
            // post never calls this inline under the dispatcher admission lock.
            try {
              boost::asio::co_spawn(observer->executor,
                                    observe_success(observer, completed),
                                    boost::asio::detached);
            } catch (...) {
              // Launch failure is also best effort, like handler failure.
            }
          });
        }
      } catch (...) {
        // Observation is best effort and cannot change a completed operation.
      }
    }
  } catch (...) {
    // The normalized operation result is independent from observation work.
  }
  co_return std::move(completion.reply);
}

auto BotOperationDispatcher::invoke_structured(bot::OperationEnvelope envelope,
                                               std::type_index result_type)
    -> boost::asio::awaitable<bot::StructuredOperationReply> {
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return invoke_structured_owned(state_, std::move(envelope), result_type,
                                 std::move(admission));
}

auto BotOperationDispatcher::invoke_structured_owned(
    std::shared_ptr<State> state, bot::OperationEnvelope envelope,
    std::type_index result_type, std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<bot::StructuredOperationReply> {
  try {
    envelope.validate();
  } catch (...) {
    co_return reject<bot::StructuredOperationValue>(
        bot::BotOperationErrorCode::InvalidRequest,
        "operation envelope is invalid");
  }
  SuccessfulBotOperation completed{
      .installation = envelope.installation,
      .action = envelope.action,
  };
  std::shared_ptr<OperationRegistry> endpoint;
  {
    std::shared_lock lock(state->mutex);
    if (state->closed) {
      co_return reject<bot::StructuredOperationValue>(
          bot::BotOperationErrorCode::Cancelled,
          "operation dispatcher is closed");
    }
    const auto found =
        state->endpoints.find(envelope.installation.installation_id);
    if (found == state->endpoints.end()) {
      co_return reject<bot::StructuredOperationValue>(
          bot::BotOperationErrorCode::RouteNotFound,
          "operation installation was not found");
    }
    endpoint = found->second;
  }
  const auto correlation = envelope.correlation;
  auto completion = co_await OperationRegistry::invoke_structured_owned(
      endpoint->state_, std::move(envelope), result_type, std::move(admission));
  observe_structured_completion(state, std::move(completed), correlation,
                                completion);
  co_return std::move(completion.reply);
}

auto BotOperationDispatcher::invoke_owned_structured(
    bot::OwnedStructuredOperationEnvelope envelope, std::type_index result_type)
    -> boost::asio::awaitable<bot::StructuredOperationReply> {
  auto admission =
      capture_send_evidence(state_, envelope.installation, envelope.action);
  return invoke_owned_structured_state(state_, std::move(envelope), result_type,
                                       std::move(admission));
}

auto BotOperationDispatcher::invoke_owned_structured_state(
    std::shared_ptr<State> state,
    bot::OwnedStructuredOperationEnvelope envelope, std::type_index result_type,
    std::optional<SendEvidenceAdmission> admission)
    -> boost::asio::awaitable<bot::StructuredOperationReply> {
  try {
    envelope.validate();
  } catch (...) {
    co_return reject<bot::StructuredOperationValue>(
        bot::BotOperationErrorCode::InvalidRequest,
        "operation envelope is invalid");
  }
  SuccessfulBotOperation completed{
      .installation = envelope.installation,
      .action = envelope.action,
  };
  std::shared_ptr<OperationRegistry> endpoint;
  {
    std::shared_lock lock(state->mutex);
    if (state->closed) {
      co_return reject<bot::StructuredOperationValue>(
          bot::BotOperationErrorCode::Cancelled,
          "operation dispatcher is closed");
    }
    const auto found =
        state->endpoints.find(envelope.installation.installation_id);
    if (found == state->endpoints.end()) {
      co_return reject<bot::StructuredOperationValue>(
          bot::BotOperationErrorCode::RouteNotFound,
          "operation installation was not found");
    }
    endpoint = found->second;
  }
  const auto correlation = envelope.correlation;
  auto completion = co_await OperationRegistry::invoke_owned_structured_state(
      endpoint->state_, std::move(envelope), result_type, std::move(admission));
  observe_structured_completion(state, std::move(completed), correlation,
                                completion);
  co_return std::move(completion.reply);
}

void BotOperationDispatcher::observe_structured_completion(
    const std::shared_ptr<State> &state, SuccessfulBotOperation completed,
    const std::optional<bot::OperationCorrelation> &correlation,
    StructuredOperationCompletion &completion) {
  // Sidecar effects have already passed the binding's bounded scope/evidence
  // policy. Logical completion is independent of physical confirmation.
  try {
    const bool logical_success =
        completion.disposition == StructuredCompletionDisposition::Completed &&
        completion.reply.ok();
    std::vector<bot::SendOutputDetails> confirmed_outputs;
    if (completion.message_outcome) {
      static std::atomic_uint64_t next_attempt{0};
      completed.attempt_id = correlation
                                 ? correlation->attempt_id
                                 : std::string{detail::process_staging_uuid()} +
                                       ":structured-send:" +
                                       std::to_string(next_attempt.fetch_add(
                                           1, std::memory_order_relaxed));
      if (logical_success) {
        if (const auto *receipt =
                std::get_if<bot::SendReceipt>(&*completion.message_outcome))
          completed.send_receipt = *receipt;
      }
      confirmed_outputs = std::visit(
          [](auto &value) { return std::move(value.output_details); },
          *completion.message_outcome);
    }
    if (!confirmed_outputs.empty()) {
      try {
        std::shared_lock lock(state->mutex);
        if (const auto &observer = state->confirmation_observer) {
          boost::asio::post(observer->executor,
                            [observer, attempt = completed.attempt_id,
                             outputs = std::move(confirmed_outputs)]() mutable {
                              try {
                                boost::asio::co_spawn(
                                    observer->executor,
                                    observe_confirmations(observer,
                                                          std::move(attempt),
                                                          std::move(outputs)),
                                    boost::asio::detached);
                              } catch (...) {
                              }
                            });
        }
      } catch (...) {
      }
    }
    if (logical_success) {
      try {
        // Admission and closure share the lock; post never invokes user code
        // inline. Do not retain an observer across provider I/O.
        std::shared_lock lock(state->mutex);
        if (const auto &observer = state->success_observer) {
          boost::asio::post(
              observer->executor,
              [observer, completed = std::move(completed)]() mutable {
                try {
                  boost::asio::co_spawn(
                      observer->executor,
                      observe_success(observer, std::move(completed)),
                      boost::asio::detached);
                } catch (...) {
                }
              });
        }
      } catch (...) {
      }
    }
  } catch (...) {
    // Notification preparation cannot replace the original opaque result.
  }
}

} // namespace obcx::core
