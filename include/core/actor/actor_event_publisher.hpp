#pragma once

#include "core/actor/actor.hpp"
#include "core/actor/actor_generation_lifecycle.hpp"

#include <boost/asio/awaitable.hpp>
#include <functional>
#include <stdexcept>
#include <utility>

namespace obcx::core {

// Generation-scoped counterpart of ActorResult::emit for actor-owned
// background operations. Not a bot send API or a persistence/retry service.
// The caller owns the original event identity, correlation and payload.
class ActorEventPublisher final {
public:
  using Publish = std::function<boost::asio::awaitable<bool>(
      ActorGenerationLifecycle::TokenPtr, MessageEnvelope)>;

  explicit ActorEventPublisher(Publish publish) : publish_(std::move(publish)) {
    if (!publish_)
      throw std::invalid_argument("actor event publisher requires a route");
  }

  // false: generation closed/stale or downstream processing failed. This
  // never authorizes repeating the physical operation that produced the event.
  // The implementation must own all values across suspension. No queued work
  // may outlive the generation's existing route-drain lease.
  auto publish(ActorGenerationLifecycle::TokenPtr token,
               MessageEnvelope message) const -> boost::asio::awaitable<bool> {
    return publish_(std::move(token), std::move(message));
  }

private:
  Publish publish_;
};

} // namespace obcx::core
