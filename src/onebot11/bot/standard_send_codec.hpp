#pragma once

#include "core/bot/native_reference_codec.hpp"
#include "onebot11/bot/standard_send.hpp"

namespace obcx::onebot11::bot::standard::detail {
using SendCodec = obcx::bot::binding::NativeReferenceCodec;

#define OBCX_DECLARE_STANDARD_SEND_CODEC(Name, name)                           \
  auto encode_##name(const Name##Parameters &, const SendCodec &,              \
                     const ValueLimits &) -> obcx::bot::Json;                  \
  auto decode_##name(const obcx::bot::Json &, const Name##Parameters &,        \
                     const SendCodec &, const ValueLimits &)                   \
      -> SendMessageData;                                                      \
  auto send_sdk_encode(const Name##Parameters &, const ValueLimits &)          \
      -> obcx::bot::Json;                                                      \
  auto send_sdk_decode_##name(const obcx::bot::Json &, const ValueLimits &)    \
      -> Name##Parameters;
OBCX_DECLARE_STANDARD_SEND_CODEC(SendPrivateMsg, send_private_msg)
OBCX_DECLARE_STANDARD_SEND_CODEC(SendGroupMsg, send_group_msg)
OBCX_DECLARE_STANDARD_SEND_CODEC(SendMsg, send_msg)
#undef OBCX_DECLARE_STANDARD_SEND_CODEC

void send_sdk_validate_envelope(const SendPrivateMsgParameters &,
                                const CallControl &, const ValueLimits &);
void send_sdk_validate_envelope(const SendGroupMsgParameters &,
                                const CallControl &, const ValueLimits &);
void send_sdk_validate_envelope(const SendMsgParameters &, const CallControl &,
                                const ValueLimits &);

void validate_send_result(const SendMessageData &,
                          const obcx::bot::ConversationRef &, const SendCodec &,
                          const ValueLimits &);
} // namespace obcx::onebot11::bot::standard::detail
