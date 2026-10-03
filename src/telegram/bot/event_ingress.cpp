#include "telegram/bot/event_ingress.hpp"
#include "telegram/bot/transport.hpp"

namespace obcx::core {

TelegramEventIngressComponent::TelegramEventIngressComponent(
    boost::asio::any_io_executor executor, std::string installation_id)
    : events_(std::make_shared<BotEventCapability>(
          std::move(executor),
          BotEventContext{.installation_id = std::move(installation_id),
                          .surface = obcx::bot::SurfaceId{"telegram.bot_api"},
                          .platform = "telegram"})) {}

auto TelegramEventIngressComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.event-ingress"},
      .provides = {CapabilityId{std::string{bot_capability_ids::events}}},
      .required = {CapabilityId{std::string{
                       obcx::telegram::bot::capability_ids::protocol}},
                   CapabilityId{std::string{
                       obcx::telegram::bot::capability_ids::transport}}},
  };
}

void TelegramEventIngressComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(ComponentId{"telegram.event-ingress"},
                   CapabilityId{std::string{bot_capability_ids::events}},
                   events_);
}

void TelegramEventIngressComponent::prepare(
    const CapabilityRegistry &registry) {
  auto transport = registry.get<TelegramTransport>(CapabilityId{
      std::string{obcx::telegram::bot::capability_ids::transport}});
  transport->set_event_callback([events = events_](const common::Event &event) {
    events->publish(event);
  });
  events_->activate();
}

void TelegramEventIngressComponent::start() {}
void TelegramEventIngressComponent::stop() { events_->close(); }

} // namespace obcx::core
