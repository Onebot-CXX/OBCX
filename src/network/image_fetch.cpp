#include "network/image_fetch.hpp"

#include "network/detail/media_fetch.hpp"

#include <algorithm>
#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>
#include <charconv>
#include <chrono>
#include <curl/curl.h>
#include <limits>
#include <map>
#include <memory>
#include <ratio>
#include <string_view>
#include <utility>

namespace obcx::network {
namespace {

auto failure_message(ImageFetchFailure failure) -> const char * {
  switch (failure) {
  case ImageFetchFailure::InvalidUrl:
    return "invalid image HTTP URL";
  case ImageFetchFailure::InvalidLimits:
    return "invalid image HTTP limits";
  case ImageFetchFailure::OverLimit:
    return "image response exceeds configured byte limit";
  case ImageFetchFailure::Timeout:
    return "image HTTP request timed out";
  case ImageFetchFailure::Transport:
    return "image HTTP transport failed";
  }
  return "image HTTP transport failed";
}

auto url_part(CURLU *url, CURLUPart part, unsigned int flags)
    -> std::optional<std::string> {
  char *raw = nullptr;
  const auto result = curl_url_get(url, part, &raw, flags);
  const std::unique_ptr<char, decltype(&curl_free)> value(raw, &curl_free);
  if (result == CURLUE_NO_QUERY || result == CURLUE_NO_FRAGMENT) {
    return std::nullopt;
  }
  if (result != CURLUE_OK || raw == nullptr) {
    throw ImageFetchError(ImageFetchFailure::InvalidUrl);
  }
  return std::string{raw};
}

struct ParsedUrl {
  std::string host;
  std::uint16_t port;
  bool use_ssl;
  std::string target;
};

auto parse_url(const std::string &text) -> ParsedUrl {
  if (text.empty() || text.size() > 8192 ||
      std::ranges::any_of(
          text, [](unsigned char c) { return c <= 0x20 || c == 0x7f; })) {
    throw ImageFetchError(ImageFetchFailure::InvalidUrl);
  }
  const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(
      curl_url(), &curl_url_cleanup);
  if (!url) {
    throw ImageFetchError(ImageFetchFailure::Transport);
  }
  if (curl_url_set(url.get(), CURLUPART_URL, text.c_str(),
                   CURLU_DISALLOW_USER) != CURLUE_OK) {
    throw ImageFetchError(ImageFetchFailure::InvalidUrl);
  }
  const auto scheme = url_part(url.get(), CURLUPART_SCHEME, 0);
  if ((scheme != "http" && scheme != "https") ||
      url_part(url.get(), CURLUPART_FRAGMENT, 0).has_value()) {
    throw ImageFetchError(ImageFetchFailure::InvalidUrl);
  }
  auto host = *url_part(url.get(), CURLUPART_HOST, 0);
  const auto port_text =
      *url_part(url.get(), CURLUPART_PORT, CURLU_DEFAULT_PORT);
  unsigned int port = 0;
  const auto [end, error] = std::from_chars(
      port_text.data(), port_text.data() + port_text.size(), port);
  if (host.empty() || error != std::errc{} ||
      end != port_text.data() + port_text.size() || port == 0 || port > 65535 ||
      (scheme == "http" && port == 443)) {
    // HttpClient treats port 443 as HTTPS; do not silently change the scheme.
    throw ImageFetchError(ImageFetchFailure::InvalidUrl);
  }
  auto target = *url_part(url.get(), CURLUPART_PATH, 0);
  if (const auto query = url_part(url.get(), CURLUPART_QUERY, 0)) {
    target += '?' + *query;
  }
  return {.host = std::move(host),
          .port = static_cast<std::uint16_t>(port),
          .use_ssl = scheme == "https",
          .target = std::move(target)};
}

} // namespace

ImageFetchError::ImageFetchError(ImageFetchFailure failure)
    : std::runtime_error(failure_message(failure)), failure_(failure) {}

auto is_valid_media_http_url(std::string_view url) -> bool {
  if (url.size() > 8192) {
    return false;
  }
  try {
    static_cast<void>(parse_url(std::string{url}));
    return true;
  } catch (const ImageFetchError &) {
    return false;
  }
}

auto detail::fetch_media_until(std::string url, std::uint64_t maximum_bytes,
                               HttpDeadline deadline,
                               HttpWriteEligibility eligible)
    -> boost::asio::awaitable<HttpCallResult> {
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
    co_return HttpCallResult{
        boost::asio::error::operation_aborted, false, 0, {}};
  }
  if (!eligible || maximum_bytes == 0 ||
      maximum_bytes > std::string{}.max_size() ||
      maximum_bytes > std::numeric_limits<std::int64_t>::max() ||
      maximum_bytes >
          static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max())) {
    co_return HttpCallResult{
        boost::asio::error::invalid_argument, false, 0, {}};
  }
  try {
    const auto parsed = parse_url(url);
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      co_return HttpCallResult{boost::asio::error::timed_out, false, 0, {}};
    }
    // Widen before subtracting epochs, and round up: zero means unlimited to
    // libcurl. This is only the connect cap; the original deadline stays
    // intact.
    using WideDuration =
        std::chrono::duration<__int128, std::chrono::steady_clock::period>;
    const auto remaining =
        std::chrono::ceil<std::chrono::duration<__int128, std::milli>>(
            WideDuration{
                static_cast<__int128>(deadline.time_since_epoch().count()) -
                static_cast<__int128>(now.time_since_epoch().count())});
    common::ConnectionConfig config;
    config.host = parsed.host;
    config.port = parsed.port;
    config.use_ssl = parsed.use_ssl;
    config.connect_timeout = std::chrono::milliseconds(
        static_cast<std::chrono::milliseconds::rep>(std::min<__int128>(
            remaining.count(), std::numeric_limits<long>::max())));
    config.access_token.clear();
    config.secret.clear();
    config.proxy_host.clear();
    config.proxy_port = 0;
    config.proxy_type.clear();
    config.proxy_username.clear();
    config.proxy_password.clear();

    const auto executor = co_await boost::asio::this_coro::executor;
    HttpClient client(executor, config);
    std::map<std::string, std::string> headers{{"Accept-Encoding", "identity"}};
    co_return co_await HttpCallAccess::get_until(
        client, parsed.target, std::move(headers), maximum_bytes, deadline,
        std::move(eligible));
  } catch (const boost::system::system_error &error) {
    co_return HttpCallResult{error.code(), false, 0, {}};
  } catch (...) {
    co_return HttpCallResult{
        boost::asio::error::invalid_argument, false, 0, {}};
  }
}

auto fetch_image(ImageFetchRequest request)
    -> boost::asio::awaitable<HttpResponse> {
  if (request.timeout.count() <= 0 ||
      request.timeout.count() > std::numeric_limits<long>::max() ||
      request.maximum_bytes == 0 ||
      request.maximum_bytes >
          static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max()) ||
      (request.prefix_bytes &&
       (*request.prefix_bytes == 0 ||
        *request.prefix_bytes > request.maximum_bytes))) {
    throw ImageFetchError(ImageFetchFailure::InvalidLimits);
  }
  const auto parsed = parse_url(request.url);
  common::ConnectionConfig config;
  config.host = parsed.host;
  config.port = parsed.port;
  config.use_ssl = parsed.use_ssl;
  config.connect_timeout = request.timeout;
  config.access_token.clear();
  config.secret.clear();
  config.proxy_host.clear();
  config.proxy_port = 0;
  config.proxy_type.clear();
  config.proxy_username.clear();
  config.proxy_password.clear();

  const auto executor = co_await boost::asio::this_coro::executor;
  HttpClient client(executor, config);
  client.set_timeout(request.timeout);
  client.set_response_body_limit(request.maximum_bytes);
  std::map<std::string, std::string> headers{{"Accept-Encoding", "identity"}};
  if (request.prefix_bytes) {
    headers.emplace("Range",
                    "bytes=0-" + std::to_string(*request.prefix_bytes - 1));
  }
  try {
    co_return co_await client.get(parsed.target, headers);
  } catch (const HttpClientError &error) {
    if (error.kind() == HttpClientErrorKind::Cancelled) {
      throw boost::system::system_error(boost::asio::error::operation_aborted);
    }
    if (error.kind() == HttpClientErrorKind::Timeout) {
      throw ImageFetchError(ImageFetchFailure::Timeout);
    }
    if (std::string_view{error.what()}.find("body limit exceeded") !=
        std::string_view::npos) {
      throw ImageFetchError(ImageFetchFailure::OverLimit);
    }
    throw ImageFetchError(ImageFetchFailure::Transport);
  }
}

} // namespace obcx::network
