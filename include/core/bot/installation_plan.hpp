#ifndef OBCX_INCLUDE_CORE_BOT_INSTALLATION_PLAN_HPP_
#define OBCX_INCLUDE_CORE_BOT_INSTALLATION_PLAN_HPP_

#include "common/bot_installation_metadata.hpp"
#include "core/bot/component_descriptor.hpp"

#include <boost/asio/io_context.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace obcx::core {

class BotComponent;
class ICommandPlatformAdapter;

struct BotRecipeDescription {
  std::string recipe_id;
  bot::SurfaceId surface;
  std::string transport;
  std::vector<ComponentDescriptor> components;
  std::vector<bot::ActionId> advertised_actions;
  std::optional<CapabilityId> command_publisher;
};

// Process-only type erasure. A module captures its validated typed connection
// value in the factory; neither raw TOML nor provider types cross this API.
class BotInstallationPlan final {
public:
  using ComponentFactory =
      std::function<std::vector<std::unique_ptr<BotComponent>>(
          boost::asio::io_context &)>;

  BotInstallationPlan(common::BotInstallationMetadata metadata,
                      BotRecipeDescription recipe,
                      std::string connection_digest, ComponentFactory factory,
                      std::shared_ptr<ICommandPlatformAdapter> command_adapter);
  ~BotInstallationPlan();
  BotInstallationPlan(const BotInstallationPlan &) = delete;
  auto operator=(const BotInstallationPlan &) -> BotInstallationPlan & = delete;
  BotInstallationPlan(BotInstallationPlan &&) = delete;
  auto operator=(BotInstallationPlan &&) -> BotInstallationPlan & = delete;

  [[nodiscard]] auto metadata() const noexcept
      -> const common::BotInstallationMetadata & {
    return metadata_;
  }
  [[nodiscard]] auto recipe() const noexcept -> const BotRecipeDescription & {
    return recipe_;
  }
  [[nodiscard]] auto fingerprint() const noexcept -> const std::string & {
    return fingerprint_;
  }
  [[nodiscard]] auto command_adapter() const noexcept
      -> const std::shared_ptr<ICommandPlatformAdapter> & {
    return command_adapter_;
  }
  [[nodiscard]] auto create_components(boost::asio::io_context &executor) const
      -> std::vector<std::unique_ptr<BotComponent>>;

private:
  const common::BotInstallationMetadata metadata_;
  const BotRecipeDescription recipe_;
  const std::string fingerprint_;
  const ComponentFactory factory_;
  const std::shared_ptr<ICommandPlatformAdapter> command_adapter_;
};

} // namespace obcx::core

#endif
