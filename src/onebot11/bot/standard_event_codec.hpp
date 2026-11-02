#pragma once

#include "common/json_utils.hpp"
#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_events.hpp"

namespace obcx::onebot11::bot::standard::detail {

// Borrows and preflights the entire tree, including unknown fields, before
// projection. All fields of supported notices are required. Throws
// std::invalid_argument for malformed, unsupported or over-budget input, with
// payload-independent diagnostics. No native payload is retained.
//
// This does not admit an origin or construct references. A later trusted
// admission layer must validate installation/origin/self independently; any
// routable reference conversion must separately enforce native positive-ID
// rules rather than changing the reported numbers in this observation.
auto decode_notice(const common::json &document, const ValueLimits &limits)
    -> NoticeObservation;

// Sibling decoders preflight the complete borrowed JSON event and reserve
// retained projection in one ledger before copying strings/restricted values.
// Unknown root fields are bounded then discarded; actual unknown status fields
// are retained only through StatusData::extra. Required online=null is retained
// as nullopt, not omitted. No service decoder or fresh child allowance is used.
// Native DOM/framing residency and duplicate-key rejection remain upstream;
// ValueLimits measures logical values and raw bytes, not physical heap usage.
// No registration, admission, evidence storage, transport or identity updates.
auto decode_lifecycle(const common::json &, const ValueLimits &)
    -> LifecycleObservation;
auto decode_group_upload(const common::json &, const ValueLimits &)
    -> GroupUploadObservation;
auto decode_friend_request(const common::json &, const ValueLimits &)
    -> FriendRequestObservation;
auto decode_group_request(const common::json &, const ValueLimits &)
    -> GroupRequestObservation;
auto decode_heartbeat(const common::json &, const ValueLimits &)
    -> HeartbeatObservation;

auto decode_standard_event(const common::json &, const ValueLimits &)
    -> StandardEventObservation;
auto parse_standard_event_frame(std::string_view, const ValueLimits &)
    -> common::json;

} // namespace obcx::onebot11::bot::standard::detail
