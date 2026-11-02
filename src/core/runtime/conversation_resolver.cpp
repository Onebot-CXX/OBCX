#include "core/runtime/conversation_resolver.hpp"
#include "core/runtime/process_configuration.hpp"

#include <utility>

namespace obcx::core {
namespace {
class RuntimeConversationResolver final : public bot::ConversationResolver {
public:
  explicit RuntimeConversationResolver(
      std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot)
      : snapshot_(std::move(snapshot)) {}

  auto resolve(const bot::BotInstallationRef &installation,
               const bot::ConfiguredConversation &target) const
      -> bot::BotOperationResult<bot::ConversationRef> override {
    try {
      installation.validate();
      target.validate();
      bool configured = false;
      for (const auto &plan : ProcessConfigAccess::plans(*snapshot_)) {
        const auto &metadata = plan->metadata();
        if (metadata.enabled &&
            metadata.installation_id == installation.installation_id &&
            metadata.surface == installation.surface) {
          configured = true;
          break;
        }
      }
      if (!configured)
        throw std::invalid_argument("installation is not configured");
      const auto binding = ProcessConfigAccess::catalog(*snapshot_)
                               ->domain_binding(installation.surface);
      if (!binding)
        throw std::invalid_argument("platform binding is unavailable");
      auto value = binding->configured_conversation(installation, target);
      binding->codec().validate(value);
      if (value.installation != installation)
        throw std::invalid_argument("configured conversation scope mismatch");
      return bot::BotOperationResult<bot::ConversationRef>::success(
          std::move(value));
    } catch (...) {
      return bot::failed_operation<bot::ConversationRef>(
          bot::BotOperationErrorCode::InvalidRequest,
          "configured conversation is invalid or unavailable");
    }
  }

private:
  std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot_;
};
} // namespace

auto make_conversation_resolver(
    std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot)
    -> std::shared_ptr<bot::ConversationResolver> {
  if (!snapshot)
    throw std::invalid_argument("conversation resolver requires configuration");
  return std::make_shared<RuntimeConversationResolver>(std::move(snapshot));
}
} // namespace obcx::core
