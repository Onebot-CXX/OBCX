#include "network/proxy_http_client.hpp"

#include "common/logger.hpp"
#include "http_client_impl.hpp"

#include <utility>

namespace obcx::network {
namespace {

[[nodiscard]] auto curl_proxy_kind(const ProxyType type)
    -> detail::CurlProxyKind {
  switch (type) {
  case ProxyType::HTTP:
    return detail::CurlProxyKind::Http;
  case ProxyType::HTTPS:
    return detail::CurlProxyKind::Https;
  case ProxyType::SOCKS5:
    return detail::CurlProxyKind::Socks5Hostname;
  }
  throw std::invalid_argument("unsupported proxy type");
}

} // namespace

ProxyHttpClient::ProxyHttpClient(asio::io_context &ioc,
                                 ProxyConfig proxy_config,
                                 const common::ConnectionConfig &config)
    : ProxyHttpClient(ioc.get_executor(), std::move(proxy_config), config) {}

ProxyHttpClient::ProxyHttpClient(asio::any_io_executor executor,
                                 ProxyConfig proxy_config,
                                 const common::ConnectionConfig &config)
    : HttpClient(std::move(executor), config) {
  pimpl_->proxy = detail::CurlProxySettings{
      .kind = curl_proxy_kind(proxy_config.type),
      .host = std::move(proxy_config.host),
      .port = proxy_config.port,
      .username = std::move(proxy_config.username),
      .password = std::move(proxy_config.password),
      .ca_bundle = std::nullopt,
  };
  OBCX_INFO("HTTP proxy client configured");
}

auto ProxyHttpClient::post(std::string_view path, std::string_view body,
                           const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  return HttpClient::post(path, body, headers);
}

auto ProxyHttpClient::get(
    std::string_view path, const std::map<std::string, std::string> &headers,
    const std::optional<std::uint64_t> response_body_limit)
    -> asio::awaitable<HttpResponse> {
  return HttpClient::get(path, headers, response_body_limit);
}

auto ProxyHttpClient::head(std::string_view path,
                           const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  return HttpClient::head(path, headers);
}

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

auto ProxyHttpClient::post_sync(
    std::string_view path, std::string_view body,
    const std::map<std::string, std::string> &headers) -> HttpResponse {
  return HttpClient::post_sync(path, body, headers);
}

auto ProxyHttpClient::get_sync(
    std::string_view path, const std::map<std::string, std::string> &headers)
    -> HttpResponse {
  return HttpClient::get_sync(path, headers);
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void ProxyHttpClient::close() { HttpClient::close(); }

} // namespace obcx::network
