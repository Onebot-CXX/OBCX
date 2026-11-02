#pragma once

#include "core/bot/validation.hpp"

namespace obcx::bot {

// Ordinary SDK values share public JSON encoding. Owning platform headers must
// specialize media values to transfer binary buffers without numeric arrays.
// encode may consume byte buffers only; identity/result-validation metadata
// must remain intact until the typed invocation finishes validating its reply.
template <typename T> struct GatewayCodec {
  static auto encode(T &value) -> Json {
    Json payload(value);
    detail::validate_domain_wire(payload);
    return payload;
  }
  static auto decode(const Json &payload) -> T {
    detail::validate_domain_wire(payload);
    return payload.template get<T>();
  }
};

} // namespace obcx::bot
