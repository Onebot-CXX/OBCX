#include "core/actor/command_scope.hpp"
#include "core/command/command_availability_builder.hpp"

#include <stdexcept>
#include <utility>

namespace obcx::command {

ResolvedGroupScope::ResolvedGroupScope(bot::ConversationRef conversation,
                                       GroupScope selector)
    : conversation_(std::move(conversation)), selector_(std::move(selector)) {}

auto ResolvedGroupScope::conversation() const noexcept
    -> const bot::ConversationRef & {
  return conversation_;
}

ResolvedCommandSource::ResolvedCommandSource(bot::MessageReceived message,
                                             Subject subject)
    : message_(std::move(message)), subject_(std::move(subject)) {}

auto ResolvedCommandSource::message() const noexcept
    -> const bot::MessageReceived & {
  return message_;
}

auto ResolvedCommandSource::conversation_kind() const noexcept
    -> ConversationKind {
  return subject_.conversation;
}

auto ResolvedCommandSource::matches(
    const ResolvedGroupScope &scope) const noexcept -> bool {
  return message_.message.conversation.installation ==
             scope.conversation_.installation &&
         command::matches(scope.selector_, subject_);
}

CommandScopeResolver::CommandScopeResolver(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

void CommandScopeResolver::publish_scopes(
    AvailabilityPublisher &publisher, std::string_view command,
    std::span<const ResolvedGroupScope> scopes) const {
  GroupScopes selectors;
  selectors.reserve(scopes.size());
  for (const auto &scope : scopes) {
    auto resolved = resolve_group(scope.selector_);
    if (!resolved.ok() ||
        resolved.value->conversation() != scope.conversation()) {
      core::CommandAvailabilityBuilder::invalidate_publication(publisher);
      throw std::invalid_argument(
          "command scope is unavailable in this generation");
    }
    selectors.push_back(scope.selector_);
  }
  // Use the existing core-owned copy, full-selector deduplication and
  // ownership/duplicate/freeze contract, including explicit empty publication.
  publisher.publish(command, selectors);
}

} // namespace obcx::command
