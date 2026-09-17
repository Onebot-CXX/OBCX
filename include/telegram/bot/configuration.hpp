#ifndef OBCX_INCLUDE_TELEGRAM_BOT_CONFIGURATION_HPP_
#define OBCX_INCLUDE_TELEGRAM_BOT_CONFIGURATION_HPP_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

namespace obcx::telegram::configuration {

enum class ProxyType : std::uint8_t {
  Http,
  Https,
  Socks5,
};

struct ProxyConfig {
  std::string host;
  std::uint16_t port{};
  ProxyType type{};
  std::string username;
  std::string password;

  auto operator==(const ProxyConfig &) const -> bool = default;
};

struct HttpConnection {
  std::string host;
  std::uint16_t port{};
  std::string access_token;
  std::string bot_username;
  bool use_tls{};
  std::chrono::milliseconds connect_timeout{};
  std::chrono::milliseconds action_timeout{};
  std::chrono::milliseconds poll_timeout{};
  std::chrono::milliseconds poll_force_close{};
  std::chrono::milliseconds poll_retry_interval{};
  std::optional<ProxyConfig> proxy;

  auto operator==(const HttpConnection &) const -> bool = default;
};

[[nodiscard]] auto parse_http(const toml::table &table, std::string_view path)
    -> HttpConnection;

} // namespace obcx::telegram::configuration

#endif
