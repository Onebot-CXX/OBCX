#ifndef OBCX_INCLUDE_TELEGRAM_BOT_PROTOCOL_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_PROTOCOL_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "telegram/adapter/protocol_adapter.hpp"
#include "telegram/bot/capability_ids.hpp"
#include <memory>

namespace obcx::core {

class TelegramProtocolComponent final : public BotComponent {
public:
  TelegramProtocolComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<adapter::telegram::ProtocolAdapter> protocol_;
};

} // namespace obcx::core

#endif
