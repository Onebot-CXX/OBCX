#pragma once

#include "network/http_call.hpp"

#include <cstdint>
#include <string>

namespace obcx::network::detail {

// Process-private full-resource GET; status interpretation belongs to the
// caller.
auto fetch_media_until(std::string url, std::uint64_t maximum_bytes,
                       HttpDeadline deadline, HttpWriteEligibility eligible)
    -> boost::asio::awaitable<HttpCallResult>;

} // namespace obcx::network::detail
