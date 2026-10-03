#include "onebot11/bot/configuration.hpp"
#include "core/bot/configuration_validation.hpp"

namespace obcx::onebot11::configuration {
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

auto parse_websocket(const toml::table &table, const std::string_view path)
    -> WebSocketConnection {
  static const std::unordered_set<std::string_view> keys = {
      "host", "port", "access_token", "connect_timeout_ms",
      "action_timeout_ms"};
  validate_keys(table, keys, path, legacy_keys);
  WebSocketConnection config;
  config.host = required_string(table, "host", path);
  config.port = required_port(table, path);
  config.access_token = required_string_value(table, "access_token", path);
  config.connect_timeout = required_duration(table, "connect_timeout_ms", path);
  config.action_timeout = required_duration(table, "action_timeout_ms", path);
  return config;
}

auto parse_http(const toml::table &table, const std::string_view path)
    -> HttpConnection {
  static const std::unordered_set<std::string_view> keys = {
      "host",
      "port",
      "access_token",
      "use_tls",
      "connect_timeout_ms",
      "action_timeout_ms",
      "poll_interval_ms"};
  validate_keys(table, keys, path, legacy_keys);
  HttpConnection config;
  config.host = required_string(table, "host", path);
  config.port = required_port(table, path);
  config.access_token = required_string_value(table, "access_token", path);
  config.use_tls = required_bool(table, "use_tls", path);
  config.connect_timeout = required_duration(table, "connect_timeout_ms", path);
  config.action_timeout = required_duration(table, "action_timeout_ms", path);
  config.poll_interval = required_duration(table, "poll_interval_ms", path);
  return config;
}

} // namespace obcx::onebot11::configuration
