#pragma once

// Process-only definitions shared by recipe manifests and handler installation.
#include "core/bot/bot_queries.hpp"
#include "core/bot/operation_registry.hpp"
#include "core/bot/self_info.hpp"
#include "onebot11/bot/forward_observation.hpp"
#include "onebot11/bot/forward_policy.hpp"
#include "onebot11/bot/operations.hpp"

#include <set>

namespace obcx::onebot11::bot {

template <typename Visitor> void for_each_operation(Visitor visit) {
  const std::vector<std::string> dependencies{"onebot11.protocol",
                                              "onebot11.transport"};
  visit(core::OperationDefinition<obcx::bot::SendMessageRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::GetMemberRequest>{dependencies});
  visit(
      core::OperationDefinition<obcx::bot::ResolveReplyRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::ReadMediaRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::GetCapabilitiesRequest>{
      dependencies});
  visit(
      core::OperationDefinition<obcx::bot::DeleteMessageRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::GetSelfRequest>{dependencies});
  visit(core::OperationDefinition<GetOneBotUserInfoRequest>{dependencies});
  visit(core::OperationDefinition<GetOneBotGroupMemberRequest>{dependencies});
  visit(
      core::OperationDefinition<GetOneBotForwardMessageRequest>{dependencies});
  visit(core::OperationDefinition<ObserveOneBotForwardMessageRequest>{
      dependencies});
  visit(core::OperationDefinition<ReadOneBotFileRequest>{dependencies});
  visit(core::OperationDefinition<ResolveOneBotGroupFileRequest>{dependencies});
  visit(
      core::OperationDefinition<ResolveOneBotPrivateFileRequest>{dependencies});
  visit(core::OperationDefinition<PokeOneBotGroupRequest>{dependencies});
  visit(core::OperationDefinition<SetOneBotMessageEmojiLikeRequest>{
      dependencies});
  visit(core::OperationDefinition<SendOneBotGroupForwardMessageRequest>{
      dependencies, forward_message_policy()});
}

inline auto operation_actions() -> std::vector<obcx::bot::ActionId> {
  std::vector<obcx::bot::ActionId> actions;
  for_each_operation([&](const auto &definition) {
    actions.push_back(definition.description().action);
  });
  return actions;
}

inline auto operation_dependencies() -> std::vector<std::string> {
  std::set<std::string> dependencies;
  for_each_operation([&](const auto &definition) {
    const auto &required = definition.description().required_capabilities;
    dependencies.insert(required.begin(), required.end());
  });
  return {dependencies.begin(), dependencies.end()};
}

} // namespace obcx::onebot11::bot
