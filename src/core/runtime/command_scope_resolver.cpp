#include "core/runtime/command_scope_resolver.hpp"
#include "core/runtime/process_configuration.hpp"

#include <stdexcept>
#include <utility>

namespace obcx::core {
namespace {

auto configured_installation(const common::RuntimeConfigSnapshot &snapshot,
                             std::string_view platform,
                             std::string_view installation_id)
    -> bot::BotInstallationRef {
  for (const auto &plan : ProcessConfigAccess::plans(snapshot)) {
    const auto &metadata = plan->metadata();
    if (metadata.installation_id != installation_id || !metadata.enabled)
      continue;
    const auto &catalog = ProcessConfigAccess::catalog(snapshot);
    if (metadata.ingress_platform != platform ||
        catalog->ingress_platform(metadata.surface) != platform ||
        !catalog->platform_adapter(metadata.surface))
      break;
    return {metadata.installation_id, metadata.surface};
  }
  throw std::invalid_argument("command installation is unavailable");
}

auto configured_source_projector(const common::RuntimeConfigSnapshot &snapshot,
                                 const bot::BotInstallationRef &installation,
                                 std::string_view platform)
    -> std::shared_ptr<CommandSourceProjector> {
  for (const auto &plan : ProcessConfigAccess::plans(snapshot)) {
    const auto &metadata = plan->metadata();
    if (!metadata.enabled ||
        metadata.installation_id != installation.installation_id ||
        metadata.surface != installation.surface)
      continue;
    const auto adapter = plan->command_adapter();
    if (adapter && adapter->platform() == platform) {
      auto projector =
          std::dynamic_pointer_cast<CommandSourceProjector>(adapter);
      if (projector)
        return projector;
    }
    break;
  }
  throw std::invalid_argument("command ACL source projection is unavailable");
}

} // namespace
} // namespace obcx::core

namespace obcx::command {

struct CommandScopeResolver::Impl {
  std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot;
  std::shared_ptr<bot::ConversationResolver> conversations;
};

auto CommandScopeResolver::resolve_group(const GroupScope &scope) const
    -> bot::BotOperationResult<ResolvedGroupScope> {
  try {
    if (!valid_scope(scope))
      throw std::invalid_argument("invalid command group selector");
    const auto installation = core::configured_installation(
        *impl_->snapshot, scope.platform, scope.bot);
    const auto conversation = impl_->conversations->resolve(
        installation, {bot::ConfiguredConversationKind::Group, scope.group_id,
                       scope.topic_id});
    if (!conversation.ok())
      throw std::invalid_argument("command group scope is unavailable");
    return bot::BotOperationResult<ResolvedGroupScope>::success(
        ResolvedGroupScope{*conversation.value, scope});
  } catch (...) {
    return bot::failed_operation<ResolvedGroupScope>(
        bot::BotOperationErrorCode::InvalidRequest,
        "command group scope has invalid or unsupported evidence");
  }
}

auto CommandScopeResolver::source_from_invocation(
    const CommandInvocation &invocation) const
    -> bot::BotOperationResult<ResolvedCommandSource> {
  try {
    const auto installation = core::configured_installation(
        *impl_->snapshot, invocation.source_platform, invocation.source_bot);
    const auto projector = core::configured_source_projector(
        *impl_->snapshot, installation, invocation.source_platform);
    // Bound the borrowed input before making the envelope copies needed by
    // the existing projector. Do not rebuild a source from historical facts.
    bot::detail::validate_bot_wire(invocation.source_event);
    bot::detail::validate_identifier(invocation.conversation_id,
                                     "command conversation", 1024);
    bot::detail::validate_identifier(invocation.sender, "command sender", 1024);
    core::MessageEnvelope event;
    event.source_platform = invocation.source_platform;
    event.source_bot = invocation.source_bot;
    event.conversation_id = invocation.conversation_id;
    event.payload = invocation.source_event;
    event.raw = invocation.source_event.at("payload");
    auto projection = projector->project_source(event, installation);
    if (!projection.ok() ||
        projection.value->subject.platform != invocation.source_platform ||
        projection.value->subject.bot != invocation.source_bot ||
        projection.value->subject.user_id != invocation.sender ||
        projection.value->source.message.conversation.installation !=
            installation)
      throw std::invalid_argument("command source projection mismatch");
    return bot::BotOperationResult<ResolvedCommandSource>::success(
        ResolvedCommandSource{std::move(projection.value->source),
                              std::move(projection.value->subject)});
  } catch (...) {
    return bot::failed_operation<ResolvedCommandSource>(
        bot::BotOperationErrorCode::InvalidRequest,
        "command source has missing, inconsistent or unsupported evidence");
  }
}

} // namespace obcx::command

namespace obcx::core {

auto CommandScopeResolverFactory::make(
    std::shared_ptr<const common::RuntimeConfigSnapshot> snapshot,
    std::shared_ptr<bot::ConversationResolver> conversations)
    -> std::shared_ptr<command::CommandScopeResolver> {
  if (!snapshot || !conversations)
    throw std::invalid_argument(
        "command scope resolver requires configuration");
  (void)ProcessConfigAccess::plans(*snapshot);
  auto impl = std::make_shared<const command::CommandScopeResolver::Impl>(
      command::CommandScopeResolver::Impl{std::move(snapshot),
                                          std::move(conversations)});
  return std::shared_ptr<command::CommandScopeResolver>(
      new command::CommandScopeResolver(std::move(impl)));
}

auto compile_command_policy(const common::RuntimeConfigSnapshot &snapshot,
                            const bot::ConversationResolver &conversations,
                            const common::CommandGroupPolicyConfig &groups,
                            const common::CommandUserPolicyConfig &users)
    -> std::optional<ActiveCommandPolicy> {
  if (!groups.mode || !groups.entries_present || !users.mode ||
      !users.entries_present)
    return std::nullopt;
  ActiveCommandPolicy policy{.groups = {.mode = *groups.mode},
                             .users = {.mode = *users.mode}};
  try {
    for (const auto &entry : groups.entries) {
      const auto installation =
          configured_installation(snapshot, entry.platform, entry.bot);
      // This ACL dimension is whole-group coverage, independent of the actor's
      // None/Exact/Any availability selector. The anchor validates native kind.
      if (!conversations
               .resolve(installation, {bot::ConfiguredConversationKind::Group,
                                       entry.native_group_id, std::nullopt})
               .ok() ||
          !policy.groups.entries
               .emplace(CommandAccessIdentity{entry.platform, entry.bot,
                                              entry.native_group_id})
               .second)
        return std::nullopt;
    }
    for (const auto &entry : users.entries) {
      const auto installation =
          configured_installation(snapshot, entry.platform, entry.bot);
      const auto projector =
          configured_source_projector(snapshot, installation, entry.platform);
      if (!projector->valid_acl_sender(installation, entry.native_user_id) ||
          !policy.users.entries
               .emplace(CommandAccessIdentity{entry.platform, entry.bot,
                                              entry.native_user_id})
               .second)
        return std::nullopt;
    }
  } catch (...) {
    return std::nullopt;
  }
  if ((policy.groups.mode == common::CommandAccessMode::Unrestricted &&
       !policy.groups.entries.empty()) ||
      (policy.users.mode == common::CommandAccessMode::Unrestricted &&
       !policy.users.entries.empty()))
    return std::nullopt;
  return policy;
}

} // namespace obcx::core
