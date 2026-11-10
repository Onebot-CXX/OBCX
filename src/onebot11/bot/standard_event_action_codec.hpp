#pragma once
#include "core/bot/bot_messaging.hpp"
#include "onebot11/bot/standard_operations.hpp"
#include "standard_event_context.hpp"
namespace obcx::onebot11::bot::standard::detail {
// Borrow the private source and prospective parameters; never copy context.
void validate_event_action(const SetGroupAnonymousBanRequest &);
void validate_event_action(const SetFriendAddRequestRequest &);
void validate_event_action(const SetGroupAddRequestRequest &);
void validate_event_action(const HandleQuickOperationRequest &);
auto encode_event_action(const SetGroupAnonymousBanRequest &) -> common::json;
auto encode_event_action(const SetFriendAddRequestRequest &) -> common::json;
auto encode_event_action(const SetGroupAddRequestRequest &) -> common::json;
auto encode_event_action(const HandleQuickOperationRequest &) -> common::json;
auto quick_operation_target(const HandleQuickOperationRequest &)
    -> std::optional<obcx::bot::ConversationRef>;
auto quick_operation_effect(const HandleQuickOperationRequest &,
                            const HandleQuickOperationOutcome &)
    -> std::optional<obcx::bot::SendOutcome>;
void validate_event_parameters(const SetGroupAnonymousBanParameters &);
void validate_event_parameters(const SetFriendAddRequestParameters &);
void validate_event_parameters(const SetGroupAddRequestParameters &);
void validate_event_parameters(const HandleQuickOperationParameters &);
} // namespace obcx::onebot11::bot::standard::detail
