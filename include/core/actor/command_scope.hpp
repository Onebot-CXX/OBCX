#pragma once

#include "core/actor/actor_commands.hpp"
#include "core/actor/command_availability.hpp"
#include "core/bot/bot_events.hpp"
#include "core/bot/operation_result.hpp"
#include "core/bot/references.hpp"

#include <memory>

namespace obcx::core {
class CommandScopeResolverFactory;
}

namespace obcx::command {

class ResolvedGroupScope final {
public:
  [[nodiscard]] auto conversation() const noexcept
      -> const bot::ConversationRef &;

private:
  friend class CommandScopeResolver;
  friend class ResolvedCommandSource;
  ResolvedGroupScope(bot::ConversationRef conversation, GroupScope selector);
  bot::ConversationRef conversation_;
  // None and Exact(1) can share a conversation, but not an ACL selector.
  GroupScope selector_;
};

// Owning, validated command projection. The original ACL subject stays private:
// comparing only ConversationRef would merge absent topic and exact topic 1.
// This value neither authenticates a caller nor grants fresh reply/media
// access.
class ResolvedCommandSource final {
public:
  [[nodiscard]] auto message() const noexcept -> const bot::MessageReceived &;
  [[nodiscard]] auto conversation_kind() const noexcept -> ConversationKind;
  [[nodiscard]] auto matches(const ResolvedGroupScope &scope) const noexcept
      -> bool;

private:
  friend class CommandScopeResolver;
  ResolvedCommandSource(bot::MessageReceived message, Subject subject);
  bot::MessageReceived message_;
  Subject subject_;
};

// Pure generation-owned configuration/command boundary. A group anchor does
// not mean that a send fans out to topics; selectors stay private for matching.
class CommandScopeResolver final {
public:
  [[nodiscard]] auto resolve_group(const GroupScope &scope) const
      -> bot::BotOperationResult<ResolvedGroupScope>;
  // Reuses the configured command adapter's source checks. Only pass the
  // coordinator-delivered invocation; this is not archive/live admission.
  [[nodiscard]] auto source_from_invocation(const CommandInvocation &invocation)
      const -> bot::BotOperationResult<ResolvedCommandSource>;
  void publish_scopes(AvailabilityPublisher &publisher,
                      std::string_view command,
                      std::span<const ResolvedGroupScope> scopes) const;

private:
  friend class obcx::core::CommandScopeResolverFactory;
  struct Impl;
  explicit CommandScopeResolver(std::shared_ptr<const Impl> impl);
  std::shared_ptr<const Impl> impl_;
};

} // namespace obcx::command
