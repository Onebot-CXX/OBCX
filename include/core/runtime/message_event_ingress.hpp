#pragma once

#include "common/message_type.hpp"
#include "core/actor/actor.hpp"
#include "core/bot/bot_event_components.hpp"
#include "core/bot/ids.hpp"
#include "core/bot/platform_adapter.hpp"

#include <string>

namespace obcx::core {

class BotOperationDispatcher;
struct SuccessfulBotOperation;
auto bot_send_confirmed_envelope(const std::string &source_platform,
                                 const bot::SendConfirmation &confirmation)
    -> MessageEnvelope;
auto bot_send_succeeded_envelope(const std::string &source_platform,
                                 const SuccessfulBotOperation &operation)
    -> MessageEnvelope;
auto message_envelope_from_event(const BotEventContext &context,
                                 const common::MessageEvent &event,
                                 const PlatformAdapter &binding,
                                 BotOperationDispatcher &dispatcher)
    -> MessageEnvelope;

auto raw_message_envelope_from_event(const std::string &source_platform,
                                     const std::string &source_bot,
                                     const common::MessageEvent &event)
    -> MessageEnvelope;

auto notice_envelope_from_event(const BotEventContext &context,
                                const common::NoticeEvent &event,
                                const PlatformAdapter &binding)
    -> MessageEnvelope;

auto raw_notice_envelope_from_event(const std::string &source_platform,
                                    const std::string &source_bot,
                                    const common::NoticeEvent &event)
    -> MessageEnvelope;

auto raw_heartbeat_envelope_from_event(const std::string &source_platform,
                                       const std::string &source_bot,
                                       const common::HeartbeatEvent &event)
    -> MessageEnvelope;

auto bot_message_sent_envelope(const std::string &source_platform,
                               const std::string &source_bot,
                               const bot::ActionId &action) -> MessageEnvelope;

} // namespace obcx::core
