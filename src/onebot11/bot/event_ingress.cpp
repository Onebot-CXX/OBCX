#include "onebot11/bot/event_ingress.hpp"
#include "onebot11/bot/transport.hpp"
#include "standard_actor_events.hpp"
#include "standard_event_context.hpp"

namespace obcx::core {

OneBot11EventIngressComponent::OneBot11EventIngressComponent(
    boost::asio::any_io_executor executor, std::string installation_id)
    : events_(std::make_shared<BotEventCapability>(
          executor,
          BotEventContext{.installation_id = installation_id,
                          .surface = obcx::bot::SurfaceId{"onebot11.qq"},
                          .platform = "qq"})),
      standard_events_(
          obcx::onebot11::bot::standard::detail::StandardEventAccess::create(
              std::move(executor), {std::move(installation_id),
                                    obcx::bot::SurfaceId{"onebot11.qq"}})) {}

auto OneBot11EventIngressComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"onebot11.event-ingress"},
      .provides =
          {CapabilityId{std::string{bot_capability_ids::events}},
           CapabilityId{std::string{ActorServiceContributor::capability_id}},
           CapabilityId{std::string{
               obcx::onebot11::bot::standard::standard_event_capability_v1}}},
      .required = {CapabilityId{std::string{
                       obcx::onebot11::bot::capability_ids::protocol}},
                   CapabilityId{std::string{
                       obcx::onebot11::bot::capability_ids::transport}}},
  };
}

void OneBot11EventIngressComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(ComponentId{"onebot11.event-ingress"},
                   CapabilityId{std::string{bot_capability_ids::events}},
                   events_);
  registry.install(
      ComponentId{"onebot11.event-ingress"},
      CapabilityId{std::string{
          obcx::onebot11::bot::standard::standard_event_capability_v1}},
      standard_events_);
  registry.install<ActorServiceContributor>(
      ComponentId{"onebot11.event-ingress"},
      CapabilityId{std::string{ActorServiceContributor::capability_id}},
      obcx::onebot11::bot::standard::detail::make_actor_service_contributor(
          {events_->context().installation_id, events_->context().surface},
          standard_events_));
}

void OneBot11EventIngressComponent::prepare(
    const CapabilityRegistry &registry) {
  auto transport = registry.get<OneBot11Transport>(CapabilityId{
      std::string{obcx::onebot11::bot::capability_ids::transport}});
  transport->set_origin_capture(
      [events = events_] { return events->capture_origin(); });
  transport->set_event_callback(
      [events = events_](const common::Event &event,
                         const std::optional<SelfIdentityOrigin> &origin) {
        return events->publish(event, origin);
      });
  events_->activate();
}

void OneBot11EventIngressComponent::start() {}
void OneBot11EventIngressComponent::stop() {
  standard_events_->close();
  events_->close();
}

} // namespace obcx::core
