#ifndef OBCX_INCLUDE_CORE_MESSAGE_EVENT_INGRESS_HPP_
#define OBCX_INCLUDE_CORE_MESSAGE_EVENT_INGRESS_HPP_

#include "common/message_type.hpp"
#include "core/actor/actor.hpp"
#include "core/bot/ids.hpp"

#include <string>

namespace obcx::core {

auto raw_message_envelope_from_event(const std::string &source_platform,
                                     const std::string &source_bot,
                                     const common::MessageEvent &event)
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

#endif // OBCX_INCLUDE_CORE_MESSAGE_EVENT_INGRESS_HPP_
