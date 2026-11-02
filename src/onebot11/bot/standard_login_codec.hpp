#pragma once

#include "core/bot/native_reference_codec.hpp"
#include "onebot11/bot/standard_login.hpp"

namespace obcx::onebot11::bot::standard::detail {
auto encode_get_login_info(const GetLoginInfoParameters &,
                           const obcx::bot::binding::NativeReferenceCodec &,
                           const ValueLimits &) -> obcx::bot::Json;
auto decode_get_login_info(const obcx::bot::Json &,
                           const GetLoginInfoParameters &,
                           const obcx::bot::binding::NativeReferenceCodec &,
                           const ValueLimits &) -> LoginInfo;
} // namespace obcx::onebot11::bot::standard::detail
