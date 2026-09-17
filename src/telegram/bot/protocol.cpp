#include "telegram/bot/protocol.hpp"

namespace obcx::core {

TelegramProtocolComponent::TelegramProtocolComponent()
    : protocol_(std::make_shared<adapter::telegram::ProtocolAdapter>()) {}

auto TelegramProtocolComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"telegram.protocol"},
      .provides = {CapabilityId{
          std::string{obcx::telegram::bot::capability_ids::protocol}}},
      .required = {},
  };
}

void TelegramProtocolComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(
      ComponentId{"telegram.protocol"},
      CapabilityId{std::string{obcx::telegram::bot::capability_ids::protocol}},
      protocol_);
}

void TelegramProtocolComponent::prepare(const CapabilityRegistry &) {}
void TelegramProtocolComponent::start() {}
void TelegramProtocolComponent::stop() {}

} // namespace obcx::core
