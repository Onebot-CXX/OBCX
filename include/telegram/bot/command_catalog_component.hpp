#ifndef OBCX_INCLUDE_CORE_BOT_COMMAND_CATALOG_COMPONENT_HPP_
#define OBCX_INCLUDE_CORE_BOT_COMMAND_CATALOG_COMPONENT_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "core/command/command_platform_adapter.hpp"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <vector>

namespace obcx::core {

class TelegramCommandCatalogComponent final : public BotComponent {
public:
  TelegramCommandCatalogComponent();

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override;
  void install_capabilities(CapabilityRegistry &registry) override;
  void prepare(const CapabilityRegistry &registry) override;
  void start() override;
  void stop() override;

private:
  class Catalog;
  std::shared_ptr<Catalog> catalog_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_COMMAND_CATALOG_COMPONENT_HPP_
