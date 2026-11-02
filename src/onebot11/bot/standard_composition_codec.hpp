#pragma once

#include "onebot11/bot/standard_composition.hpp"
#include "standard_content_codec.hpp"

namespace obcx::onebot11::bot::standard::detail {

// Conservative storage requirement for the two fixed-size per-sequence scratch
// arrays, including their control objects. Overflow checked; call only after
// borrowed admission. This is NOT ValueLimits payload bytes or a total heap
// guarantee: allocator overhead, JSON/string capacity and native codec scratch
// still require the future CallLimits preparation reservation mechanism.
auto composition_scratch_bytes(std::size_t sequence_count) -> std::size_t;

// These entry points charge the complete borrowed destination and MessageInput,
// then the EncodedContent wrapper and every generated JSON value/key/string.
// Bottom-up CQ encoding also charges each retained child CQ string before its
// escaped copy enters its parent. Moved JSON subtrees are charged only once.
// Validation charges the prospective output without constructing it.
// No reference decoding or graph allocation precedes complete borrowed
// admission. The ledger overload does not reset the caller's allowance; callers
// must not precharge these same portions. Other envelope/request portions
// remain theirs. Pure, unregistered: no I/O, evidence upgrade, acquisition or
// send effects.
// Explicit native message depth for nesting under operation.reply. Validation
// uses the same borrowed/derived-value checks without materializing JSON/CQ.
// The returned bool is the authoritative auto_escape value.
auto validate_composition(const MessageInput &,
                          const obcx::bot::ConversationRef &,
                          const obcx::bot::binding::NativeReferenceCodec &,
                          ContentBudget &, std::size_t message_depth) -> bool;
auto encode_composition(const MessageInput &,
                        const obcx::bot::ConversationRef &,
                        const obcx::bot::binding::NativeReferenceCodec &,
                        ContentBudget &, std::size_t message_depth)
    -> EncodedContent;
auto encode_composition(const MessageInput &,
                        const obcx::bot::ConversationRef &,
                        const obcx::bot::binding::NativeReferenceCodec &,
                        ContentBudget &) -> EncodedContent;
auto encode_composition(const MessageInput &,
                        const obcx::bot::ConversationRef &,
                        const obcx::bot::binding::NativeReferenceCodec &,
                        const ValueLimits &) -> EncodedContent;

} // namespace obcx::onebot11::bot::standard::detail
