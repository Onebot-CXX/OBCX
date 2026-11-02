#pragma once

#include "core/bot/operation_registry.hpp"
#include "core/bot/self_info.hpp"
#include "telegram/bot/domain_operations.hpp"

#include <set>

namespace obcx::telegram::bot {

inline auto sticker_result_policy()
    -> core::MessageResultPolicy<SendStickerRequest> {
  const auto envelope = obcx::bot::SendOutcomeEnvelope::for_reply(
      obcx::bot::SendOutcomeEnvelope::standalone());
  return {.context = [envelope](const SendStickerRequest &request)
              -> core::MessageOutcomeContext {
            return {request.target, nullptr, envelope};
          },
          .outcome = [](obcx::bot::SendOutcome &result) noexcept
              -> obcx::bot::SendOutcome & { return result; },
          .normalize_outcome =
              [envelope](const SendStickerRequest &request,
                         obcx::bot::SendOutcome outcome) {
                return normalize_sticker_outcome(request, std::move(outcome),
                                                 envelope);
              },
          .normalize_result =
              [](const SendStickerRequest &, const obcx::bot::SendOutcome *,
                 obcx::bot::SendOutcome outcome) { return outcome; },
          .make_failure =
              [](const SendStickerRequest &,
                 obcx::bot::SendFailure failure) -> obcx::bot::SendOutcome {
            return obcx::bot::SendOutcome{std::move(failure)};
          }};
}

template <typename Visitor>
void for_each_operation(bool include_upload, Visitor visit) {
  const std::vector<std::string> dependencies{"telegram.protocol",
                                              "telegram.transport"};
  auto send_dependencies = dependencies;
  if (include_upload)
    send_dependencies.push_back("telegram.media-upload");
  visit(core::OperationDefinition<obcx::bot::GetSelfRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::SendMessageRequest>{
      send_dependencies});
  visit(
      core::OperationDefinition<obcx::bot::DeleteMessageRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::GetMemberRequest>{dependencies});
  visit(
      core::OperationDefinition<obcx::bot::ResolveReplyRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::ReadMediaRequest>{dependencies});
  visit(core::OperationDefinition<obcx::bot::GetCapabilitiesRequest>{
      dependencies});
  visit(core::OperationDefinition<SendStickerRequest>{dependencies,
                                                      sticker_result_policy()});
  visit(core::OperationDefinition<EditTextRequest>{dependencies});
  visit(core::OperationDefinition<EditFormattedTextRequest>{dependencies});
  visit(core::OperationDefinition<CreateTopicRequest>{dependencies});
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
