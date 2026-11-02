#pragma once

#include "common/config_snapshot.hpp"
#include "core/actor/actor_manager.hpp"
#include "core/actor/command_availability.hpp"
#include "core/actor/command_scope.hpp"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace obcx::core {

using CommandAvailabilitySnapshot =
    std::map<std::pair<std::string, std::string>, command::GroupScopes>;

// Candidate-only builder. Never registered as a service accessible to actors.
class CommandAvailabilityBuilder final {
public:
  CommandAvailabilityBuilder(
      std::shared_ptr<command::CommandScopeResolver> resolver,
      const std::unordered_map<std::string, ActorInputContract> &contracts);

  [[nodiscard]] auto for_actor(const std::string &actor)
      -> std::shared_ptr<command::AvailabilityPublisher>;
  [[nodiscard]] auto freeze() -> CommandAvailabilitySnapshot;

  // Process-only facade rejection path. Marks the concrete core publisher's
  // candidate invalid without publishing a fabricated selector or command.
  static void invalidate_publication(command::AvailabilityPublisher &publisher);

private:
  struct State;
  class Publisher;
  std::shared_ptr<State> state_;
};

} // namespace obcx::core
