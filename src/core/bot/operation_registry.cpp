#include "core/bot/operation_registry.hpp"

#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>

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

struct OperationRegistry::State {
  explicit State(bot::BotInstallationRef identity)
      : installation(std::move(identity)) {}

  bot::BotInstallationRef installation;
  mutable std::shared_mutex mutex;
  std::map<bot::ActionId, std::shared_ptr<const OperationBinding>> bindings;
  bool sealed = false;
  bool closed = false;
};

OperationRegistry::OperationRegistry(bot::BotInstallationRef installation) {
  installation.validate();
  state_ = std::make_shared<State>(std::move(installation));
}

OperationRegistry::~OperationRegistry() { close(); }

void OperationRegistry::install(OperationBinding binding) {
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed) {
    throw std::logic_error("operation registry no longer accepts registration");
  }
  binding.description_.action.validate();
  if (!binding.invoke_ || !binding.supports_surface_ ||
      !binding.supports_surface_(state_->installation.surface)) {
    throw std::invalid_argument(
        "operation binding is incomplete or belongs to another surface");
  }
  auto entry = std::make_shared<const OperationBinding>(std::move(binding));
  const auto action = entry->description_.action;
  if (!state_->bindings.emplace(action, std::move(entry)).second) {
    throw std::invalid_argument("duplicate operation action: " +
                                action.value());
  }
}

void OperationRegistry::seal(
    const bot::BotInstallationRef &capability_owner,
    const std::span<const std::string> available_capabilities) {
  capability_owner.validate();
  std::unique_lock lock(state_->mutex);
  if (state_->sealed || state_->closed) {
    throw std::logic_error("operation registry is already sealed or closed");
  }
  if (capability_owner != state_->installation) {
    throw std::invalid_argument(
        "operation capabilities belong to another installation");
  }
  std::set<std::string> available;
  for (const auto &capability : available_capabilities) {
    (void)bot::ActionId{capability};
    available.insert(capability);
  }
  for (const auto &[action, binding] : state_->bindings) {
    (void)action;
    for (const auto &dependency : binding->description_.required_capabilities) {
      if (!available.contains(dependency)) {
        throw std::invalid_argument("missing operation capability: " +
                                    dependency);
      }
    }
  }
  state_->sealed = true;
}

void OperationRegistry::close() noexcept {
  std::unique_lock lock(state_->mutex);
  state_->closed = true;
  // Invocations already admitted retain their own binding/handler lease.
  state_->bindings.clear();
}

auto OperationRegistry::installation() const -> bot::BotInstallationRef {
  return state_->installation;
}

auto OperationRegistry::declared_actions() const -> std::vector<bot::ActionId> {
  std::shared_lock lock(state_->mutex);
  std::vector<bot::ActionId> actions;
  for (const auto &[action, binding] : state_->bindings) {
    (void)binding;
    actions.push_back(action);
  }
  return actions;
}

auto OperationRegistry::descriptions() const
    -> std::vector<OperationDescription> {
  std::shared_lock lock(state_->mutex);
  std::vector<OperationDescription> descriptions;
  descriptions.reserve(state_->bindings.size());
  for (const auto &[action, binding] : state_->bindings) {
    (void)action;
    descriptions.push_back(binding->description_);
  }
  return descriptions;
}

auto OperationRegistry::supported_actions(
    const bot::BotInstallationRef &installation) const
    -> bot::BotOperationResult<bot::SupportedActions> {
  try {
    installation.validate();
  } catch (...) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::InvalidRequest,
        "operation installation is invalid");
  }
  std::shared_lock lock(state_->mutex);
  if (installation.installation_id != state_->installation.installation_id) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::RouteNotFound,
        "operation installation was not found");
  }
  if (installation.surface != state_->installation.surface) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::SurfaceMismatch,
        "operation installation surface does not match");
  }
  if (state_->closed) {
    return reject<bot::SupportedActions>(bot::BotOperationErrorCode::Cancelled,
                                         "operation endpoint is closed");
  }
  if (!state_->sealed) {
    return reject<bot::SupportedActions>(
        bot::BotOperationErrorCode::RouteNotFound,
        "operation endpoint is not prepared");
  }
  bot::SupportedActions result{.installation = state_->installation};
  for (const auto &[action, binding] : state_->bindings) {
    (void)binding;
    result.actions.push_back(action);
  }
  return bot::BotOperationResult<bot::SupportedActions>::success(
      std::move(result));
}

auto OperationRegistry::invoke(bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  // Capture ownership synchronously, before a lazily-started coroutine can
  // outlive this registry object. No raw `this` survives in the coroutine.
  return invoke_owned(state_, std::move(envelope));
}

auto OperationRegistry::invoke_owned(std::shared_ptr<State> state,
                                     bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  try {
    envelope.validate();
  } catch (...) {
    co_return reject<bot::Json>(bot::BotOperationErrorCode::InvalidRequest,
                                "operation envelope is invalid");
  }
  std::shared_ptr<const OperationBinding> binding;
  {
    std::shared_lock lock(state->mutex);
    if (envelope.installation.installation_id !=
        state->installation.installation_id) {
      co_return reject<bot::Json>(bot::BotOperationErrorCode::RouteNotFound,
                                  "operation installation was not found");
    }
    if (envelope.installation.surface != state->installation.surface) {
      co_return reject<bot::Json>(
          bot::BotOperationErrorCode::SurfaceMismatch,
          "operation installation surface does not match");
    }
    if (state->closed) {
      co_return reject<bot::Json>(bot::BotOperationErrorCode::Cancelled,
                                  "operation endpoint is closed");
    }
    if (!state->sealed) {
      co_return reject<bot::Json>(bot::BotOperationErrorCode::RouteNotFound,
                                  "operation endpoint is not prepared");
    }
    const auto found = state->bindings.find(envelope.action);
    if (found == state->bindings.end()) {
      co_return reject<bot::Json>(bot::BotOperationErrorCode::UnsupportedAction,
                                  "operation action is not installed");
    }
    binding = found->second;
  }
  co_return co_await binding->invoke_(std::move(envelope));
}

} // namespace obcx::core
