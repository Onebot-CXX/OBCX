#ifndef OBCX_INCLUDE_ONEBOT11_BOT_OPERATION_COMPONENT_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_OPERATION_COMPONENT_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/operation_registry.hpp"

namespace obcx::core {

class OneBot11OperationsComponent final : public BotComponent {
public:
  explicit OneBot11OperationsComponent(std::string installation_id);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Endpoint;
  std::shared_ptr<Endpoint> endpoint_;
  std::shared_ptr<OperationRegistry> operations_;
};

} // namespace obcx::core

#endif
