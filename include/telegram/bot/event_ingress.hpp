#ifndef OBCX_INCLUDE_TELEGRAM_BOT_EVENT_INGRESS_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_EVENT_INGRESS_HPP_

#include "core/bot/bot_event_components.hpp"
#include "telegram/bot/capability_ids.hpp"

namespace obcx::core {

class TelegramEventIngressComponent final : public BotComponent {
public:
  TelegramEventIngressComponent(boost::asio::any_io_executor executor,
                                std::string installation_id);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  std::shared_ptr<BotEventCapability> events_;
};

} // namespace obcx::core

#endif
