#pragma once

#include "network/http_client.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace obcx::network {

struct ImageFetchRequest {
  std::string url;
  std::chrono::milliseconds timeout;
  std::uint64_t maximum_bytes;
  // nullopt downloads the body; a positive value requests bytes 0..value-1.
  // maximum_bytes still applies if the server ignores Range.
  std::optional<std::uint64_t> prefix_bytes;
};

enum class ImageFetchFailure : std::uint8_t {
  InvalidUrl,
  InvalidLimits,
  OverLimit,
  Timeout,
  Transport,
};

class ImageFetchError : public std::runtime_error {
public:
  explicit ImageFetchError(ImageFetchFailure failure);
  [[nodiscard]] auto failure() const noexcept -> ImageFetchFailure {
    return failure_;
  }

private:
  ImageFetchFailure failure_;
};

// Syntax-only validation using the fetch parser, without DNS or network I/O.
// Success is not destination authorization; access is the caller/deployment's
// responsibility.
[[nodiscard]] auto is_valid_media_http_url(std::string_view url) -> bool;

// Direct HTTP(S), without proxy credentials, redirects, persistence or retries.
// Destination access is the caller/deployment's responsibility: this is not a
// DNS-pinning/SSRF sandbox. HTTP status and image interpretation are caller
// policy. Cancellation propagates as Asio operation_aborted; other transport
// diagnostics never contain the input URL.
// Keep the caller's executor running through transport cleanup.
[[nodiscard]] auto fetch_image(ImageFetchRequest request)
    -> boost::asio::awaitable<HttpResponse>;

} // namespace obcx::network
