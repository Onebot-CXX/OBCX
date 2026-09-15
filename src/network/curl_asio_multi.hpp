#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace obcx::network::detail {

enum class CurlHttpMethod : std::uint8_t { Get, Post, Head };
enum class CurlProxyKind : std::uint8_t { Http, Https, Socks5Hostname };

struct CurlProxySettings {
  CurlProxyKind kind;
  std::string host;
  std::uint16_t port;
  std::optional<std::string> username;
  std::optional<std::string> password;
  std::optional<std::string> ca_bundle;
};

struct CurlRequest {
  CurlHttpMethod method;
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
  std::chrono::milliseconds connect_timeout;
  std::chrono::milliseconds total_timeout;
  std::uint64_t maximum_response_bytes;
  std::uint64_t maximum_header_bytes;
  std::optional<std::string> ca_bundle;
  std::optional<CurlProxySettings> proxy;
};

struct CurlResponse {
  long status_code;
  std::string body;
  std::vector<std::string> header_lines;
};

struct CurlRuntimeCapabilities {
  std::string version;
  std::string ssl_backend;
  bool asynchronous_dns;
  bool tls;
  bool https_proxy;
  bool http;
  bool https;
  bool socks5_hostname;
};

[[nodiscard]] auto curl_runtime_capabilities() -> CurlRuntimeCapabilities;
[[nodiscard]] auto curl_error_code(int native_code)
    -> boost::system::error_code;
[[nodiscard]] auto curl_error_definitely_not_submitted(
    const boost::system::error_code &error) noexcept -> bool;

class CurlAsioMulti final : public std::enable_shared_from_this<CurlAsioMulti> {
public:
  explicit CurlAsioMulti(boost::asio::any_io_executor executor);
  CurlAsioMulti(const CurlAsioMulti &) = delete;
  auto operator=(const CurlAsioMulti &) -> CurlAsioMulti & = delete;
  ~CurlAsioMulti();

  [[nodiscard]] auto perform(CurlRequest request)
      -> boost::asio::awaitable<CurlResponse>;

  // Cancels every transfer. Completion handlers remain owned until they run.
  void shutdown() noexcept;

private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace obcx::network::detail
