#pragma once

// Process-private pure codecs. Inputs are completed native data, not envelopes.
// The execution layer must enforce ingress bytes, disposition and origin first;
// its enclosing parameter/result envelope must retain aggregate budgeting.
#include "core/bot/native_reference_codec.hpp"
#include "onebot11/bot/standard_queries.hpp"

namespace obcx::onebot11::bot::standard::detail {

using QueryJson = obcx::bot::Json;
using QueryNativeCodec = obcx::bot::binding::NativeReferenceCodec;

auto encode_get_msg(const GetMessageParameters &,
                    const obcx::bot::BotInstallationRef &expected_installation,
                    const QueryNativeCodec &, const ValueLimits &) -> QueryJson;
auto encode_get_forward_msg(
    const GetForwardMessageParameters &,
    const obcx::bot::BotInstallationRef &expected_installation,
    const QueryNativeCodec &, const ValueLimits &) -> QueryJson;
auto encode_get_stranger_info(const GetStrangerInfoParameters &,
                              const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_friend_list(const GetFriendListParameters &,
                            const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_group_info(const GetGroupInfoParameters &,
                           const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_group_honor_info(const GetGroupHonorInfoParameters &,
                                 const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_group_list(const GetGroupListParameters &,
                           const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_group_member_info(const GetGroupMemberInfoParameters &,
                                  const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;
auto encode_get_group_member_list(const GetGroupMemberListParameters &,
                                  const QueryNativeCodec &, const ValueLimits &)
    -> QueryJson;

auto decode_get_stranger_info(const QueryJson &,
                              const GetStrangerInfoParameters &,
                              const QueryNativeCodec &, const ValueLimits &)
    -> StrangerInfo;
auto decode_get_friend_list(const QueryJson &, const GetFriendListParameters &,
                            const QueryNativeCodec &, const ValueLimits &)
    -> FriendList;
auto decode_get_group_info(const QueryJson &, const GetGroupInfoParameters &,
                           const QueryNativeCodec &, const ValueLimits &)
    -> GroupInfo;
auto decode_get_group_honor_info(const QueryJson &,
                                 const GetGroupHonorInfoParameters &,
                                 const QueryNativeCodec &, const ValueLimits &)
    -> GroupHonorInfo;
auto decode_get_group_list(const QueryJson &, const GetGroupListParameters &,
                           const QueryNativeCodec &, const ValueLimits &)
    -> GroupList;
auto decode_get_group_member_info(const QueryJson &,
                                  const GetGroupMemberInfoParameters &,
                                  const QueryNativeCodec &, const ValueLimits &)
    -> GroupMemberInfo;
auto decode_get_group_member_list(const QueryJson &,
                                  const GetGroupMemberListParameters &,
                                  const QueryNativeCodec &, const ValueLimits &)
    -> GroupMemberList;

} // namespace obcx::onebot11::bot::standard::detail
