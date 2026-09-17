#ifndef OBCX_INCLUDE_ONEBOT11_BOT_CONFIGURATION_HPP_
#define OBCX_INCLUDE_ONEBOT11_BOT_CONFIGURATION_HPP_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

namespace obcx::onebot11::configuration {

struct WebSocketConnection {
  std::string host;
  std::uint16_t port{};
  std::string access_token;
  std::chrono::milliseconds connect_timeout{};
  std::chrono::milliseconds action_timeout{};

  auto operator==(const WebSocketConnection &) const -> bool = default;
};

struct HttpConnection {
  std::string host;
  std::uint16_t port{};
  std::string access_token;
  bool use_tls{};
  std::chrono::milliseconds connect_timeout{};
  std::chrono::milliseconds action_timeout{};
  std::chrono::milliseconds poll_interval{};

  auto operator==(const HttpConnection &) const -> bool = default;
};

[[nodiscard]] auto parse_websocket(const toml::table &table,
                                   std::string_view path)
    -> WebSocketConnection;
[[nodiscard]] auto parse_http(const toml::table &table, std::string_view path)
    -> HttpConnection;

} // namespace obcx::onebot11::configuration

#endif
