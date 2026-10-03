#include "onebot11/bot/protocol.hpp"

namespace obcx::core {

OneBot11ProtocolComponent::OneBot11ProtocolComponent()
    : protocol_(std::make_shared<adapter::onebot11::ProtocolAdapter>()) {}

auto OneBot11ProtocolComponent::descriptor() const -> ComponentDescriptor {
  return {
      .id = ComponentId{"onebot11.protocol"},
      .provides = {CapabilityId{
          std::string{obcx::onebot11::bot::capability_ids::protocol}}},
      .required = {},
  };
}

void OneBot11ProtocolComponent::install_capabilities(
    CapabilityRegistry &registry) {
  registry.install(
      ComponentId{"onebot11.protocol"},
      CapabilityId{std::string{obcx::onebot11::bot::capability_ids::protocol}},
      protocol_);
}

void OneBot11ProtocolComponent::prepare(const CapabilityRegistry &) {}
void OneBot11ProtocolComponent::start() {}
void OneBot11ProtocolComponent::stop() {}

} // namespace obcx::core
