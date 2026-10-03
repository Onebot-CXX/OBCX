#pragma once

#include <chrono>
#include <cstdint>
#include <string>

// Existing caller-owned networking input, not a Bot installation schema or a
// means of obtaining process credentials. Kept out of messaging SDK headers.
namespace obcx::common {

/**
 * \if CHINESE
 * @brief 连接配置
 * \endif
 * \if ENGLISH
 * @brief Connection configuration
 * \endif
 */
struct ConnectionConfig {
  std::string host = "localhost";
  uint16_t port = 8080;
  std::string access_token;
  std::string secret;
  std::chrono::milliseconds connect_timeout{5000};
  std::chrono::milliseconds action_timeout{30000};
  std::chrono::milliseconds poll_timeout{
      25000}; // Long-poll timeout sent to server (e.g., Telegram getUpdates)
  std::chrono::milliseconds poll_force_close{30000};
  std::chrono::milliseconds poll_retry_interval{3000};
  std::chrono::milliseconds heartbeat_interval{30000};
  bool use_ssl = false;

  // Proxy configuration
  std::string proxy_host;
  uint16_t proxy_port = 0;
  std::string proxy_type = "http"; // "http", "https", "socks5"
  std::string proxy_username;
  std::string proxy_password;
};

} // namespace obcx::common
