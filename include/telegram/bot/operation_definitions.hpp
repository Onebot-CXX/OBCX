#ifndef OBCX_INCLUDE_TELEGRAM_BOT_OPERATION_DEFINITIONS_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_OPERATION_DEFINITIONS_HPP_

#include "core/bot/operation_registry.hpp"
#include "telegram/bot/operations.hpp"

#include <set>

namespace obcx::telegram::bot {

template <typename Visitor>
void for_each_operation(bool include_upload, Visitor visit) {
  const std::vector<std::string> dependencies{"telegram.protocol",
                                              "telegram.transport"};
  visit(core::OperationDefinition<obcx::bot::SendGroupMessageRequest>{
      dependencies});
  visit(core::OperationDefinition<obcx::bot::SendPrivateMessageRequest>{
      dependencies});
  visit(
      core::OperationDefinition<obcx::bot::DeleteMessageRequest>{dependencies});
  visit(
      core::OperationDefinition<SendTelegramTopicMessageRequest>{dependencies});
  visit(
      core::OperationDefinition<EditTelegramMessageTextRequest>{dependencies});
  visit(core::OperationDefinition<SendTelegramPhotoRequest>{dependencies});
  visit(core::OperationDefinition<SendTelegramMediaGroupUrlsRequest>{
      dependencies});
  if (include_upload) {
    auto upload_dependencies = dependencies;
    upload_dependencies.push_back("telegram.media-upload");
    visit(core::OperationDefinition<SendTelegramMediaGroupUploadsRequest>{
        std::move(upload_dependencies)});
  }
  visit(core::OperationDefinition<FetchTelegramFileRequest>{dependencies});
}

inline auto operation_actions(bool include_upload)
    -> std::vector<obcx::bot::ActionId> {
  std::vector<obcx::bot::ActionId> actions;
  for_each_operation(include_upload, [&](const auto &definition) {
    actions.push_back(definition.description().action);
  });
  return actions;
}

inline auto operation_dependencies(bool include_upload)
    -> std::vector<std::string> {
  std::set<std::string> dependencies;
  for_each_operation(include_upload, [&](const auto &definition) {
    const auto &required = definition.description().required_capabilities;
    dependencies.insert(required.begin(), required.end());
  });
  return {dependencies.begin(), dependencies.end()};
}

} // namespace obcx::telegram::bot

#endif // OBCX_INCLUDE_TELEGRAM_BOT_OPERATION_DEFINITIONS_HPP_
