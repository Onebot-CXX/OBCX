#pragma once

#include "core/bot/actor_service_contributor.hpp"
#include "onebot11/bot/standard_actor_events.hpp"

namespace obcx::core {
class BotInstallationDirectory;
}

namespace obcx::onebot11::bot::standard::detail {
// Only the native ingress installs this process-private contributor, using the
// same exact installation and event capability as transport admission.
auto make_actor_service_contributor(obcx::bot::BotInstallationRef,
                                    std::shared_ptr<StandardEventCapabilityV1>)
    -> std::shared_ptr<core::ActorServiceContributor>;
// Preparation binds only the configured identity; live lookup is deferred to
// lifecycle activation, after startup has registered native installations.
auto make_planned_standard_actor_service_contributor(
    obcx::bot::BotInstallationRef,
    std::weak_ptr<core::BotInstallationDirectory>)
    -> std::shared_ptr<core::ActorServiceContributor>;
} // namespace obcx::onebot11::bot::standard::detail
