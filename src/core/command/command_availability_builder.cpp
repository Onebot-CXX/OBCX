#include "core/command/command_availability_builder.hpp"

#include <algorithm>
#include <mutex>
#include <set>
#include <stdexcept>

namespace obcx::core {

struct CommandAvailabilityBuilder::State {
  std::mutex mutex;
  bool frozen = false;
  bool invalid = false;
  std::map<std::string, std::set<std::string>> declarations;
  std::shared_ptr<command::CommandScopeResolver> resolver;
  CommandAvailabilitySnapshot scopes;
};

class CommandAvailabilityBuilder::Publisher final
    : public command::AvailabilityPublisher {
public:
  Publisher(std::shared_ptr<State> state, std::string actor)
      : state_(std::move(state)), actor_(std::move(actor)) {}

  void invalidate() {
    std::scoped_lock lock(state_->mutex);
    if (state_->frozen)
      throw std::logic_error("command availability is frozen");
    state_->invalid = true;
  }

  void publish(const std::string_view command,
               const command::GroupScopes &scopes) override {
    std::scoped_lock lock(state_->mutex);
    if (state_->frozen) {
      throw std::logic_error("command availability is frozen");
    }
    const auto reject = [this](const char *message) {
      state_->invalid = true;
      throw std::invalid_argument(message);
    };
    const auto key = std::pair{actor_, std::string{command}};
    if (!state_->declarations.at(actor_).contains(key.second)) {
      reject("command availability publication is not owned or declared");
    }
    if (state_->scopes.contains(key)) {
      reject("command availability publication is duplicated");
    }
    for (const auto &scope : scopes) {
      if (!state_->resolver->resolve_group(scope).ok()) {
        reject("command availability scope is invalid");
      }
    }
    // Copy in core, including allocation/control of the stored containers.
    auto copied = scopes;
    std::ranges::sort(copied);
    copied.erase(std::unique(copied.begin(), copied.end()), copied.end());
    state_->scopes.emplace(key, std::move(copied));
  }

private:
  std::shared_ptr<State> state_;
  std::string actor_;
};

CommandAvailabilityBuilder::CommandAvailabilityBuilder(
    std::shared_ptr<command::CommandScopeResolver> resolver,
    const std::unordered_map<std::string, ActorInputContract> &contracts)
    : state_(std::make_shared<State>()) {
  if (!resolver)
    throw std::invalid_argument("command availability requires scope resolver");
  state_->resolver = std::move(resolver);
  for (const auto &[actor, contract] : contracts) {
    auto &commands = state_->declarations[actor];
    for (const auto &registration : contract.commands) {
      if (registration.actor_scoped) {
        commands.insert(registration.name);
      }
    }
  }
}

auto CommandAvailabilityBuilder::for_actor(const std::string &actor)
    -> std::shared_ptr<command::AvailabilityPublisher> {
  std::scoped_lock lock(state_->mutex);
  if (state_->frozen || !state_->declarations.contains(actor)) {
    throw std::logic_error("command availability publisher is unavailable");
  }
  return std::make_shared<Publisher>(state_, actor);
}

void CommandAvailabilityBuilder::invalidate_publication(
    command::AvailabilityPublisher &publisher) {
  auto *owned = dynamic_cast<Publisher *>(&publisher);
  if (!owned)
    throw std::invalid_argument(
        "command availability publisher is not core-owned");
  owned->invalidate();
}

auto CommandAvailabilityBuilder::freeze() -> CommandAvailabilitySnapshot {
  std::scoped_lock lock(state_->mutex);
  if (state_->frozen || state_->invalid) {
    throw std::logic_error("command availability cannot be finalized");
  }
  state_->frozen = true;
  return std::move(state_->scopes);
}

} // namespace obcx::core
