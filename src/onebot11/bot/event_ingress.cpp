#include "onebot11/bot/event_ingress.hpp"
#include "onebot11/bot/transport.hpp"

namespace obcx::core {

OneBot11EventIngressComponent::OneBot11EventIngressComponent(
    boost::asio::any_io_executor executor, std::string installation_id)
    : events_(std::make_shared<BotEventCapability>(
          std::move(executor),
          BotEventContext{.installation_id = std::move(installation_id),
                          .surface = obcx::bot::SurfaceId{"onebot11.qq"},
                          .platform = "qq"})) {}

auto OneBot11EventIngressComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"onebot11.event-ingress"},
      .provides = {CapabilityId{std::string{bot_capability_ids::events}}},
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
}

void OneBot11EventIngressComponent::prepare(
    const CapabilityRegistry &registry) {
  auto transport = registry.get<OneBot11Transport>(CapabilityId{
      std::string{obcx::onebot11::bot::capability_ids::transport}});
  transport->set_event_callback([events = events_](const common::Event &event) {
    events->publish(event);
  });
  events_->activate();
}

void OneBot11EventIngressComponent::start() {}
void OneBot11EventIngressComponent::stop() { events_->close(); }

} // namespace obcx::core
