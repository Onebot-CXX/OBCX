#pragma once

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/operation_registry.hpp"
#include "core/bot/self_identity_cache.hpp"

namespace obcx::core {

class OneBot11OperationsComponent final : public BotComponent {
public:
  OneBot11OperationsComponent(std::string installation_id,
                              boost::asio::any_io_executor executor,
                              std::chrono::milliseconds self_refresh_interval);

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Endpoint;
  std::shared_ptr<Endpoint> endpoint_;
  std::shared_ptr<OperationRegistry> operations_;
  std::shared_ptr<SelfIdentityCache> self_identity_;
};

} // namespace obcx::core
