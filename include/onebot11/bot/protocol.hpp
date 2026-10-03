#ifndef OBCX_INCLUDE_ONEBOT11_BOT_PROTOCOL_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_PROTOCOL_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "onebot11/bot/capability_ids.hpp"
#include <memory>

namespace obcx::core {

class OneBot11ProtocolComponent final : public BotComponent {
public:
  OneBot11ProtocolComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<adapter::onebot11::ProtocolAdapter> protocol_;
};

} // namespace obcx::core

#endif
