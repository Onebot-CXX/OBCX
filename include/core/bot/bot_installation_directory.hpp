#pragma once

#include "core/bot/actor_service_contributor.hpp"
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
  void register_capabilities(
      bot::BotInstallationRef installation,
      std::shared_ptr<BotOperationEndpoint> endpoint,
      std::shared_ptr<CommandCatalogPublisher> command_catalog,
      std::shared_ptr<ActorServiceContributor> actor_services);
  void unregister_installation(const std::string &installation_id) noexcept;

  [[nodiscard]] auto endpoint(const bot::BotInstallationRef &installation) const
      -> std::shared_ptr<BotOperationEndpoint>;
  [[nodiscard]] auto actor_service_contributor(
      const bot::BotInstallationRef &installation) const
      -> std::shared_ptr<ActorServiceContributor>;
  [[nodiscard]] auto command_catalog_publisher(
      const bot::BotInstallationRef &installation) const
      -> std::shared_ptr<CommandCatalogPublisher>;

private:
  struct Entry {
    bot::BotInstallationRef installation;
    std::weak_ptr<BotOperationEndpoint> endpoint;
    std::weak_ptr<CommandCatalogPublisher> command_catalog;
    std::weak_ptr<ActorServiceContributor> actor_services;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Entry> entries_;
};

} // namespace obcx::core
