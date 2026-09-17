#ifndef OBCX_INCLUDE_CORE_BOT_GATEWAY_CODEC_HPP_
#define OBCX_INCLUDE_CORE_BOT_GATEWAY_CODEC_HPP_

#include "core/bot/validation.hpp"

namespace obcx::bot {

// Ordinary SDK values share public JSON encoding. Owning platform headers must
// specialize media values to transfer binary buffers without numeric arrays.
// encode may consume byte buffers only; identity/result-validation metadata
// must remain intact until the typed invocation finishes validating its reply.
template <typename T> struct GatewayCodec {
  static auto encode(T &value) -> Json { return Json(value); }
  static auto decode(Json payload) -> T { return payload.template get<T>(); }
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_GATEWAY_CODEC_HPP_
