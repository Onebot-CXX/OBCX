#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace obcx::network {
class HttpClient;

namespace detail {

using HttpDeadline = std::chrono::steady_clock::time_point;
using HttpWriteEligibility = std::function<bool()>;

struct HttpCallResult {
  boost::system::error_code error;
  bool response_complete;
  long status_code;
  std::string body;
};

// Private transport admission, not a guarantee against writes after retirement.
// The caller owns authority synchronization inside the eligibility callback.
class HttpCallAccess final {
public:
  static auto get_until(HttpClient &client, std::string path,
                        std::map<std::string, std::string> headers,
                        std::uint64_t maximum_response_bytes,
                        HttpDeadline deadline, HttpWriteEligibility eligible)
      -> boost::asio::awaitable<HttpCallResult>;

  static auto post_until(HttpClient &client, std::string path, std::string body,
                         std::map<std::string, std::string> headers,
                         std::uint64_t maximum_response_bytes,
                         HttpDeadline deadline, HttpWriteEligibility eligible)
      -> boost::asio::awaitable<HttpCallResult>;
};

} // namespace detail
} // namespace obcx::network
