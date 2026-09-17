#ifndef OBCX_INCLUDE_CORE_BOT_INSTALLATION_DIRECTORY_HPP_
#define OBCX_INCLUDE_CORE_BOT_INSTALLATION_DIRECTORY_HPP_

#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/command/command_platform_adapter.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace obcx::core {

class BotInstallationDirectory {
public:
  void register_installation(
      const BotInstallation &installation,
      const std::optional<CapabilityId> &publisher_capability);
  void register_capabilities(
      bot::BotInstallationRef installation,
      std::shared_ptr<BotOperationEndpoint> endpoint,
      std::shared_ptr<CommandCatalogPublisher> command_catalog = {});
  void unregister_installation(const std::string &installation_id) noexcept;

  [[nodiscard]] auto endpoint(const bot::BotInstallationRef &installation) const
      -> std::shared_ptr<BotOperationEndpoint>;
  [[nodiscard]] auto command_catalog_publisher(
      const bot::BotInstallationRef &installation) const
      -> std::shared_ptr<CommandCatalogPublisher>;

private:
  struct Entry {
    bot::BotInstallationRef installation;
    std::weak_ptr<BotOperationEndpoint> endpoint;
    std::weak_ptr<CommandCatalogPublisher> command_catalog;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_INSTALLATION_DIRECTORY_HPP_
