#include "telegram/bot/configuration.hpp"
#include "core/bot/configuration_validation.hpp"

namespace obcx::telegram::configuration {
namespace {
using core::configuration::bot_configuration_error;
using core::configuration::required_bool;
using core::configuration::required_duration;
using core::configuration::required_port;
using core::configuration::required_string;
using core::configuration::required_string_value;
using core::configuration::validate_keys;
const std::unordered_set<std::string_view> legacy_keys = {"type",
                                                          "timeout",
                                                          "connect_timeout",
                                                          "action_timeout",
                                                          "poll_timeout",
                                                          "poll_force_close",
                                                          "poll_retry_interval",
                                                          "heartbeat_interval",
                                                          "use_ssl",
                                                          "secret"};
} // namespace

auto parse_proxy_type(const std::string_view value, const std::string_view path)
    -> ProxyType {
  if (value == "http") {
    return ProxyType::Http;
  }
  if (value == "https") {
    return ProxyType::Https;
  }
  if (value == "socks5") {
    return ProxyType::Socks5;
  }
  bot_configuration_error("unsupported_bot_proxy_type", std::string{path},
                          std::string{path} +
                              " must be http, https, or socks5");
}

auto parse_http(const toml::table &table, const std::string_view path)
    -> HttpConnection {
  static const std::unordered_set<std::string_view> keys = {
      "host",
      "port",
      "access_token",
      "bot_username",
      "use_tls",
      "connect_timeout_ms",
      "action_timeout_ms",
      "poll_timeout_ms",
      "poll_force_close_ms",
      "poll_retry_interval_ms",
      "proxy_host",
      "proxy_port",
      "proxy_type",
      "proxy_username",
      "proxy_password"};
  validate_keys(table, keys, path, legacy_keys);
  HttpConnection config;
  config.host = required_string(table, "host", path);
  config.port = required_port(table, path);
  config.access_token = required_string(table, "access_token", path);
  config.bot_username = required_string_value(table, "bot_username", path);
  config.use_tls = required_bool(table, "use_tls", path);
  if (!config.use_tls) {
    bot_configuration_error(
        "invalid_bot_tls_configuration", std::string{path} + ".use_tls",
        std::string{path} + ".use_tls must be true for telegram.bot_api");
  }
  config.connect_timeout = required_duration(table, "connect_timeout_ms", path);
  config.action_timeout = required_duration(table, "action_timeout_ms", path);
  config.poll_timeout = required_duration(table, "poll_timeout_ms", path);
  config.poll_force_close =
      required_duration(table, "poll_force_close_ms", path);
  config.poll_retry_interval =
      required_duration(table, "poll_retry_interval_ms", path);
  if (config.poll_force_close < config.poll_timeout) {
    bot_configuration_error(
        "invalid_bot_configuration_value",
        std::string{path} + ".poll_force_close_ms",
        std::string{path} +
            ".poll_force_close_ms must be at least poll_timeout_ms");
  }

  const auto proxy_host = table.contains("proxy_host")
                              ? required_string(table, "proxy_host", path)
                              : std::string{};
  const auto proxy_port_node = table.get("proxy_port");
  if (proxy_host.empty() != (proxy_port_node == nullptr)) {
    bot_configuration_error(
        "invalid_bot_proxy_configuration", std::string{path} + ".proxy_host",
        std::string{path} + " requires proxy_host and proxy_port together");
  }
  if (!proxy_host.empty()) {
    ProxyConfig proxy;
    proxy.host = proxy_host;
    if (const auto *node = table.get("proxy_port")) {
      const auto value = node->value<std::int64_t>();
      if (!value || *value <= 0 || *value > 65'535) {
        bot_configuration_error(
            "invalid_bot_proxy_configuration",
            std::string{path} + ".proxy_port",
            std::string{path} +
                ".proxy_port must be an integer from 1 to 65535");
      }
      proxy.port = static_cast<std::uint16_t>(*value);
    }
    proxy.type = parse_proxy_type(required_string(table, "proxy_type", path),
                                  std::string{path} + ".proxy_type");
    proxy.username = required_string_value(table, "proxy_username", path);
    proxy.password = required_string_value(table, "proxy_password", path);
    config.proxy = std::move(proxy);
  } else if (table.contains("proxy_type") || table.contains("proxy_username") ||
             table.contains("proxy_password")) {
    bot_configuration_error(
        "invalid_bot_proxy_configuration", std::string{path} + ".proxy_host",
        std::string{path} +
            " requires proxy_host and proxy_port before proxy options");
  }
  return config;
}

} // namespace obcx::telegram::configuration
