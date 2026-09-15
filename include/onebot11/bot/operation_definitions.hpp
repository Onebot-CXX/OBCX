#ifndef OBCX_INCLUDE_ONEBOT11_BOT_OPERATION_DEFINITIONS_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_OPERATION_DEFINITIONS_HPP_

// Process-only definitions shared by recipe manifests and handler installation.
#include "core/bot/messaging.hpp"
#include "core/bot/operation_registry.hpp"
#include "onebot11/bot/operations.hpp"

#include <set>

namespace obcx::onebot11::bot {

template <typename Visitor> void for_each_operation(Visitor visit) {
  const std::vector<std::string> dependencies{"onebot11.protocol",
                                              "onebot11.transport"};
  visit(core::OperationDefinition<obcx::bot::SendGroupMessageRequest>{
      dependencies});
  visit(core::OperationDefinition<obcx::bot::SendPrivateMessageRequest>{
      dependencies});
  visit(
      core::OperationDefinition<obcx::bot::DeleteMessageRequest>{dependencies});
  visit(core::OperationDefinition<GetOneBotGroupMemberRequest>{dependencies});
  visit(
      core::OperationDefinition<GetOneBotForwardMessageRequest>{dependencies});
  visit(core::OperationDefinition<ResolveOneBotGroupFileRequest>{dependencies});
  visit(
      core::OperationDefinition<ResolveOneBotPrivateFileRequest>{dependencies});
  visit(core::OperationDefinition<PokeOneBotGroupRequest>{dependencies});
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

#endif // OBCX_INCLUDE_ONEBOT11_BOT_OPERATION_DEFINITIONS_HPP_
