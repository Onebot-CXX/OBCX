#include "core/bot/bot_operation_dispatcher.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>

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

} // namespace

struct BotOperationDispatcher::State {
  explicit State(SurfaceValidator validator)
      : surface_registered(std::move(validator)) {}
  const SurfaceValidator surface_registered;
  mutable std::shared_mutex mutex;
  std::unordered_map<std::string, std::shared_ptr<OperationRegistry>> endpoints;
  SuccessHandler success_handler;
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

void BotOperationDispatcher::set_success_handler(SuccessHandler handler) {
  if (!handler) {
    throw std::invalid_argument("operation success handler cannot be empty");
  }
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed) {
    throw std::logic_error(
        "operation success handler must be set before sealing");
  }
  if (state_->success_handler) {
    throw std::logic_error("operation success handler is already set");
  }
  state_->success_handler = std::move(handler);
}

void BotOperationDispatcher::seal_registrations() {
  std::unique_lock lock(state_->mutex);
  if (state_->closed) {
    throw std::logic_error("operation dispatcher is closed");
  }
  state_->sealed = true;
}

void BotOperationDispatcher::clear_endpoints() noexcept {
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

auto BotOperationDispatcher::invoke(bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  return invoke_owned(state_, std::move(envelope));
}

auto BotOperationDispatcher::invoke_owned(std::shared_ptr<State> state,
                                          bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  try {
    envelope.validate();
  } catch (...) {
    co_return reject<bot::Json>(bot::BotOperationErrorCode::InvalidRequest,
                                "operation envelope is invalid");
  }
  const SuccessfulBotOperation completed{
      .installation = envelope.installation,
      .action = envelope.action,
  };
  std::shared_ptr<OperationRegistry> endpoint;
  SuccessHandler success_handler;
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
    success_handler = state->success_handler;
  }
  auto reply = co_await endpoint->invoke(std::move(envelope));
  if (reply.ok() && success_handler) {
    auto executor = co_await boost::asio::this_coro::executor;
    try {
      boost::asio::co_spawn(executor, success_handler(completed),
                            boost::asio::detached);
    } catch (...) {
      // Observation is best effort and cannot change a completed operation.
    }
  }
  co_return reply;
}

} // namespace obcx::core
