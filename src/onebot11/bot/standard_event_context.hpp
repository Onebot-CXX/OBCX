#pragma once

// Process-private. Never install this header in the SDK.
#include "common/json_utils.hpp"
#include "core/bot/self_identity_cache.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include <atomic>
#include <boost/asio/any_io_executor.hpp>

namespace obcx::onebot11::bot::standard::detail {
enum class EventActionSource {
  PrivateMessage,
  GroupMessage,
  FriendRequest,
  GroupRequest
};
struct StandardEventContext {
  obcx::bot::BotInstallationRef installation;
  std::shared_ptr<core::SelfIdentityCache> identity;
  core::SelfIdentityOrigin origin;
  std::shared_ptr<std::atomic_bool> active;
  EventActionSource source;
  // Already strictly parsed, bounded, authentic source. Never publicly exposed.
  common::json native;
};
enum class EventIngressDisposition { Rejected, ApiOnly, EventCompatible };
struct EventIngressFrame {
  EventIngressDisposition disposition;
  // Present only after strict event preflight, including the entire batch.
  common::json document;
};
struct StandardEventAccess {
  static auto create(boost::asio::any_io_executor,
                     obcx::bot::BotInstallationRef)
      -> std::shared_ptr<StandardEventCapabilityV1>;
  static void configure(StandardEventCapabilityV1 &,
                        const obcx::bot::BotInstallationRef &,
                        std::shared_ptr<core::SelfIdentityCache>, ValueLimits);
  // Does not publish. Structural rejection blocks BOTH event paths. API-only
  // frames remain on the existing response path, without event value limits.
  static auto receive(StandardEventCapabilityV1 &, std::string_view)
      -> EventIngressFrame;
  static auto event_candidate(const common::json &) -> bool;
  // One preflighted entry at a time, so HTTP can stop the batch on transition.
  // Unsupported typed projections do not prevent legacy conversion.
  static void admit(StandardEventCapabilityV1 &, common::json,
                    const std::optional<core::SelfIdentityOrigin> &);
  static auto context(const EventActionHandle &)
      -> const StandardEventContext &;
  static void validate(const EventActionHandle &);
};
} // namespace obcx::onebot11::bot::standard::detail
