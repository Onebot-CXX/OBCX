#pragma once
#include "onebot11/bot/standard_lookup.hpp"
#include "standard_query_codec.hpp"
namespace obcx::onebot11::bot::standard::detail {
auto decode_get_msg(const QueryJson &, const GetMessageParameters &,
                    const QueryNativeCodec &, const ValueLimits &)
    -> DetachedMessage;
auto decode_get_forward_msg(const QueryJson &,
                            const GetForwardMessageParameters &,
                            const QueryNativeCodec &, const ValueLimits &)
    -> DetachedForwardMessage;
void validate_lookup_result(const DetachedMessage &,
                            const GetMessageParameters &, const ValueLimits &);
void validate_lookup_result(const DetachedForwardMessage &,
                            const GetForwardMessageParameters &,
                            const ValueLimits &);
} // namespace obcx::onebot11::bot::standard::detail
