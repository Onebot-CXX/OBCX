#ifndef OBCX_INCLUDE_ONEBOT11_BOT_EVENT_INGRESS_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_EVENT_INGRESS_HPP_

#include "core/bot/bot_event_components.hpp"
#include "onebot11/bot/capability_ids.hpp"

namespace obcx::core {

class OneBot11EventIngressComponent final : public BotComponent {
public:
  OneBot11EventIngressComponent(boost::asio::any_io_executor executor,
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
