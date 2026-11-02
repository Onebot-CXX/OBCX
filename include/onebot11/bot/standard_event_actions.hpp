#pragma once
#include "onebot11/bot/standard_composition.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include "onebot11/bot/standard_execution.hpp"
#include "onebot11/bot/standard_mutations.hpp"
#include <utility>

namespace obcx::onebot11::bot::standard {
// Each representation uses the authentic anonymous context from the handle.
enum class AnonymousBanForm : std::uint8_t { Object, AnonymousFlag, Flag };
struct SetGroupAnonymousBanParameters {
  SetGroupAnonymousBanParameters(EventActionHandle source,
                                 AnonymousBanForm form,
                                 DurationSeconds duration)
      : source(std::move(source)), form(std::move(form)),
        duration(std::move(duration)) {}
  EventActionHandle source;
  AnonymousBanForm form;
  DurationSeconds duration;
};
struct SetFriendAddRequestParameters {
  SetFriendAddRequestParameters(EventActionHandle source, bool approve,
                                std::string remark)
      : source(std::move(source)), approve(std::move(approve)),
        remark(std::move(remark)) {}
  EventActionHandle source;
  bool approve;
  std::string remark;
};
struct SetGroupAddRequestParameters {
  SetGroupAddRequestParameters(EventActionHandle source, bool approve,
                               std::string reason)
      : source(std::move(source)), approve(std::move(approve)),
        reason(std::move(reason)) {}
  EventActionHandle source;
  bool approve;
  std::string reason;
};
struct PrivateMessageQuickOperation {
  PrivateMessageQuickOperation(std::optional<MessageInput> reply,
                               std::optional<bool> auto_escape)
      : reply(std::move(reply)), auto_escape(std::move(auto_escape)) {}
  std::optional<MessageInput> reply;
  std::optional<bool> auto_escape;
};
struct GroupMessageQuickOperation {
  GroupMessageQuickOperation(std::optional<MessageInput> reply,
                             std::optional<bool> auto_escape,
                             std::optional<bool> at_sender,
                             std::optional<bool> delete_message,
                             std::optional<bool> kick, std::optional<bool> ban,
                             std::optional<DurationSeconds> ban_duration)
      : reply(std::move(reply)), auto_escape(std::move(auto_escape)),
        at_sender(std::move(at_sender)),
        delete_message(std::move(delete_message)), kick(std::move(kick)),
        ban(std::move(ban)), ban_duration(std::move(ban_duration)) {}
  std::optional<MessageInput> reply;
  std::optional<bool> auto_escape;
  std::optional<bool> at_sender;
  std::optional<bool> delete_message;
  std::optional<bool> kick;
  std::optional<bool> ban;
  std::optional<DurationSeconds> ban_duration;
};
struct FriendRequestQuickOperation {
  FriendRequestQuickOperation(std::optional<bool> approve,
                              std::optional<std::string> remark)
      : approve(std::move(approve)), remark(std::move(remark)) {}
  std::optional<bool> approve;
  std::optional<std::string> remark;
};
struct GroupRequestQuickOperation {
  GroupRequestQuickOperation(std::optional<bool> approve,
                             std::optional<std::string> reason)
      : approve(std::move(approve)), reason(std::move(reason)) {}
  std::optional<bool> approve;
  std::optional<std::string> reason;
};
using QuickOperation =
    std::variant<PrivateMessageQuickOperation, GroupMessageQuickOperation,
                 FriendRequestQuickOperation, GroupRequestQuickOperation>;
struct HandleQuickOperationParameters {
  HandleQuickOperationParameters(EventActionHandle source,
                                 QuickOperation operation)
      : source(std::move(source)), operation(std::move(operation)) {}
  EventActionHandle source;
  QuickOperation operation;
};
} // namespace obcx::onebot11::bot::standard
