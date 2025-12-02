#include "qbittorrent_client.hpp"
#include "common/logger.hpp"
#include <fmt/format.h>
#include <sstream>
#include <iomanip>
#include <set>
#include <algorithm>
#include <boost/beast/http.hpp>

namespace plugins {

QBittorrentClient::QBittorrentClient(const std::string &host, int port,
                                     bool use_ssl,
                                     const std::string &username,
                                     const std::string &password)
    : host_(host), port_(port), use_ssl_(use_ssl),
      username_(username), password_(password) {

  // Initialize HttpClient with qBittorrent config
  obcx::common::ConnectionConfig config;
  config.host = host;
  config.port = port;
  config.use_ssl = use_ssl;

  http_client_ = std::make_unique<obcx::network::HttpClient>(ioc_, config);
  OBCX_INFO("HTTP Client initialized for {}://{}:{}", use_ssl ? "https" : "http", host, port);

  // Run io_context in a background thread to handle async operations
  ioc_thread_ = std::thread([this]() {
    auto work_guard = boost::asio::make_work_guard(ioc_);
    ioc_.run();
  });
}

QBittorrentClient::~QBittorrentClient() {
  // Stop io_context and wait for thread to finish
  ioc_.stop();
  if (ioc_thread_.joinable()) {
    ioc_thread_.join();
  }
}

std::string QBittorrentClient::build_url(const std::string &endpoint) {
  std::string protocol = use_ssl_ ? "https" : "http";
  return fmt::format("{}://{}:{}{}", protocol, host_, port_, endpoint);
}

std::string QBittorrentClient::url_encode(const std::string &value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      escaped << c;
    } else {
      escaped << '%' << std::setw(2)
              << static_cast<int>(static_cast<unsigned char>(c));
    }
  }

  return escaped.str();
}

boost::asio::awaitable<std::string>
QBittorrentClient::http_post(const std::string &path, const std::string &body,
                              const std::string &cookie,
                              const std::string &content_type) {
  try {
    // Build headers
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = content_type;
    if (!cookie.empty()) {
      headers["Cookie"] = cookie;
    }
    // Disable compression to avoid gzip responses
    headers["Accept-Encoding"] = "identity";

    OBCX_DEBUG("POST {}", path);

    // Use HttpClient's sync API
    auto response = http_client_->post_sync(path, body, headers);

    OBCX_DEBUG("POST {} returned status {}", path, response.status_code);

    // Check HTTP status code
    if (!response.is_success()) {
      throw std::runtime_error(
          fmt::format("HTTP POST failed with status {}: {}",
                      response.status_code, response.body));
    }

    // For login endpoint, extract Set-Cookie from response headers
    auto set_cookie_field = response.raw_response.find(boost::beast::http::field::set_cookie);
    if (set_cookie_field != response.raw_response.end()) {
      std::string cookie_value(set_cookie_field->value());
      // Extract only cookie value before semicolon
      size_t semi = cookie_value.find(';');
      if (semi != std::string::npos) {
        cookie_value = cookie_value.substr(0, semi);
      }
      OBCX_DEBUG("Extracted cookie: {}", cookie_value);
      co_return cookie_value;
    }

    // If no cookie, return response body
    co_return response.body;
  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP POST failed: {}", e.what());
    throw std::runtime_error(fmt::format("HTTP POST to {} failed: {}", path, e.what()));
  }
}

boost::asio::awaitable<std::string>
QBittorrentClient::http_get(const std::string &path, const std::string &cookie) {
  try {
    // Build headers
    std::map<std::string, std::string> headers;
    if (!cookie.empty()) {
      headers["Cookie"] = cookie;
    }
    // Disable compression to avoid gzip responses
    headers["Accept-Encoding"] = "identity";

    OBCX_DEBUG("GET {}", path);

    // Use HttpClient's sync API
    auto response = http_client_->get_sync(path, headers);

    OBCX_DEBUG("GET {} returned status {}", path, response.status_code);

    // Check HTTP status code
    if (!response.is_success()) {
      throw std::runtime_error(
          fmt::format("HTTP GET failed with status {}: {}",
                      response.status_code, response.body));
    }

    co_return response.body;
  } catch (const std::exception &e) {
    OBCX_ERROR("HTTP GET failed: {}", e.what());
    throw std::runtime_error(fmt::format("HTTP GET to {} failed: {}", path, e.what()));
  }
}


boost::asio::awaitable<std::string> QBittorrentClient::login() {
  std::string path = "/api/v2/auth/login";
  std::string body = fmt::format("username={}&password={}",
                                 url_encode(username_), url_encode(password_));

  OBCX_INFO("Logging in to qBittorrent at {}:{}", host_, port_);

  // Retry logic for unstable DDNS resolution
  const int max_retries = 3;
  std::string last_error;

  for (int attempt = 1; attempt <= max_retries; attempt++) {
    // Delay before retry (except first attempt)
    if (attempt > 1) {
      int delay_s = attempt - 1;  // 1s, 2s
      OBCX_WARN("Retrying login in {}s (attempt {}/{})", delay_s, attempt, max_retries);
      boost::asio::steady_timer timer(ioc_, std::chrono::seconds(delay_s));
      co_await timer.async_wait(boost::asio::use_awaitable);
    }

    try {
      std::string cookie = co_await http_post(path, body);

      if (cookie.empty()) {
        throw std::runtime_error(
            fmt::format("qBittorrent登录失败 - 用户名或密码错误 (用户名: {})",
                        username_));
      }

      if (attempt > 1) {
        OBCX_INFO("Successfully logged in to qBittorrent (attempt {})", attempt);
      } else {
        OBCX_INFO("Successfully logged in to qBittorrent");
      }
      co_return cookie;

    } catch (const std::runtime_error &e) {
      last_error = e.what();
      std::string error_str(last_error);

      // Check if it's a connection error (DNS/network issue)
      bool is_connection_error =
          error_str.find("Connection timed out") != std::string::npos ||
          error_str.find("No route to host") != std::string::npos ||
          error_str.find("Connection refused") != std::string::npos;

      if (!is_connection_error || attempt >= max_retries) {
        // Not a connection error, or final attempt - throw immediately
        throw;
      }

      OBCX_WARN("Login attempt {}/{} failed (DNS/network issue): {}",
                attempt, max_retries, last_error);
    }
  }

  // All retries exhausted
  throw std::runtime_error(
      fmt::format("qBittorrent登录失败，已重试{}次: {}", max_retries, last_error));
}

boost::asio::awaitable<AddTorrentResult>
QBittorrentClient::add_torrent(const std::string &cookie,
                                const std::string &source, bool is_magnet,
                                const std::string &save_path) {
  std::string path = "/api/v2/torrents/add";
  std::string body;

  // Build request body with save path
  if (is_magnet) {
    body = fmt::format("urls={}", url_encode(source));
  } else {
    // For torrent file, need to send file content
    // For now, just use file path (simplified)
    body = fmt::format("urls=file://{}", url_encode(source));
  }

  // Add save path if specified
  if (!save_path.empty()) {
    body += fmt::format("&savepath={}", url_encode(save_path));
  }

  OBCX_INFO("Adding torrent to qBittorrent (save_path: {})",
            save_path.empty() ? "default" : save_path);

  // Small delay to ensure session is ready after login
  boost::asio::steady_timer session_timer(ioc_, std::chrono::milliseconds(500));
  co_await session_timer.async_wait(boost::asio::use_awaitable);

  // Get existing torrents before adding
  auto torrents_before = co_await get_all_torrents(cookie);
  std::set<std::string> existing_hashes;
  for (const auto &torrent : torrents_before) {
    existing_hashes.insert(torrent["hash"].get<std::string>());
  }

  std::string response = co_await http_post(path, body, cookie);

  // qBittorrent returns "Ok." on success, "Fails." if torrent already exists
  bool is_ok = (response.find("Ok.") != std::string::npos || response == "Ok.");
  bool is_fail = (response.find("Fails.") != std::string::npos || response == "Fails.");

  if (!is_ok && !is_fail) {
    throw std::runtime_error("Failed to add torrent: " + response);
  }

  if (is_fail) {
    OBCX_WARN("Torrent add returned 'Fails.' - likely already exists");
  } else {
    OBCX_INFO("Torrent add request successful");
  }

  // Wait for qBittorrent to process and retry getting torrent list
  // Sometimes qBittorrent needs more time to add the torrent
  nlohmann::json torrents_after;
  for (int retry = 0; retry < 3; retry++) {
    int wait_time = (retry == 0) ? 2 : 3;  // 2s, 3s, 3s
    boost::asio::steady_timer timer(ioc_, std::chrono::seconds(wait_time));
    co_await timer.async_wait(boost::asio::use_awaitable);

    torrents_after = co_await get_all_torrents(cookie);

    // If we got "Ok." and found a new torrent, we're done
    if (!is_fail) {
      // Check if there's a new torrent
      for (const auto &torrent : torrents_after) {
        std::string hash = torrent["hash"].get<std::string>();
        if (existing_hashes.find(hash) == existing_hashes.end()) {
          OBCX_INFO("Found new torrent with hash: {} after {}s wait", hash,
                    (retry == 0 ? 2 : 2 + retry * 3));
          co_return AddTorrentResult{hash, false};
        }
      }
    } else {
      // For "Fails.", we don't need to retry - it already exists
      break;
    }

    if (retry < 2) {
      OBCX_WARN("New torrent not found yet, waiting longer (retry {}/3)", retry + 1);
    }
  }

  // If we got "Fails.", we know it already exists
  if (is_fail) {
    if (is_magnet) {
      // Extract hash from magnet link
      size_t hash_pos = source.find("btih:");
      if (hash_pos != std::string::npos) {
        hash_pos += 5; // Skip "btih:"
        size_t hash_end = source.find_first_of("&", hash_pos);
        std::string extracted_hash = source.substr(hash_pos,
            hash_end == std::string::npos ? std::string::npos : hash_end - hash_pos);

        // Convert to lowercase for comparison
        std::transform(extracted_hash.begin(), extracted_hash.end(),
                       extracted_hash.begin(), ::tolower);

        // Check if this hash exists in existing torrents
        for (const auto &torrent : torrents_after) {
          std::string existing_hash = torrent["hash"].get<std::string>();
          std::transform(existing_hash.begin(), existing_hash.end(),
                         existing_hash.begin(), ::tolower);

          if (existing_hash == extracted_hash) {
            OBCX_INFO("Found existing magnet torrent with hash: {}", existing_hash);
            co_return AddTorrentResult{existing_hash, true};
          }
        }

        // Hash extracted but not found in list - still return it as existing
        OBCX_WARN("Magnet returned 'Fails.' but hash {} not found in list, assuming it exists",
                  extracted_hash);
        co_return AddTorrentResult{extracted_hash, true};
      }
    } else {
      // For torrent file, we need to find which torrent in the list matches
      // Since we can't extract hash from file path, we look for a torrent that existed before
      // This is tricky - the torrent file already exists, so check if any torrent
      // in the current list was also in the before list
      OBCX_WARN("Torrent file returned 'Fails.' - already exists, searching for matching hash");

      // If there are any torrents that existed before and still exist now,
      // we can't determine which one is ours. Return the first one found.
      // A better approach would be to compare torrent file content hash, but that's complex.
      for (const auto &torrent : torrents_after) {
        std::string hash = torrent["hash"].get<std::string>();
        if (existing_hashes.find(hash) != existing_hashes.end()) {
          // This torrent existed before - could be the one
          // Check the name to see if it matches
          std::string name = torrent.value("name", "");
          OBCX_INFO("Found potential existing torrent: {} (hash: {})", name, hash);
          co_return AddTorrentResult{hash, true};
        }
      }

      // Fallback: return any hash if we can't determine
      if (!torrents_after.empty()) {
        std::string hash = torrents_after[0]["hash"].get<std::string>();
        OBCX_WARN("Cannot determine exact existing torrent, returning first: {}", hash);
        co_return AddTorrentResult{hash, true};
      }
    }
  }

  // No new torrent found after retries - try to extract hash from magnet link as fallback
  if (is_magnet) {
    size_t hash_pos = source.find("btih:");
    if (hash_pos != std::string::npos) {
      hash_pos += 5; // Skip "btih:"
      size_t hash_end = source.find_first_of("&", hash_pos);
      std::string extracted_hash = source.substr(hash_pos,
          hash_end == std::string::npos ? std::string::npos : hash_end - hash_pos);

      // Convert to lowercase for comparison
      std::transform(extracted_hash.begin(), extracted_hash.end(),
                     extracted_hash.begin(), ::tolower);

      // Check if this hash exists in existing torrents
      for (const auto &torrent : torrents_after) {
        std::string existing_hash = torrent["hash"].get<std::string>();
        std::transform(existing_hash.begin(), existing_hash.end(),
                       existing_hash.begin(), ::tolower);

        if (existing_hash == extracted_hash) {
          OBCX_WARN("Torrent already exists with hash: {}", existing_hash);
          co_return AddTorrentResult{existing_hash, true};
        }
      }

      // Hash extracted but not found - maybe it's still being added
      OBCX_INFO("Extracted hash from magnet but not found in torrents yet: {}", extracted_hash);
      co_return AddTorrentResult{extracted_hash, false};
    }
  }

  throw std::runtime_error("Could not determine hash of added torrent");
}

boost::asio::awaitable<nlohmann::json>
QBittorrentClient::get_torrent_info(const std::string &cookie,
                                     const std::string &hash) {
  std::string path = fmt::format("/api/v2/torrents/info?hashes={}", hash);

  // Retry logic for unstable DDNS resolution
  const int max_retries = 3;
  std::string last_error;

  for (int attempt = 1; attempt <= max_retries; attempt++) {
    // Delay before retry (except first attempt)
    if (attempt > 1) {
      int delay_s = attempt - 1;  // 指数退避: 1s, 2s
      OBCX_WARN("等待{}s后重试获取种子状态 (尝试 {}/{})", delay_s, attempt, max_retries);
      boost::asio::steady_timer timer(ioc_, std::chrono::seconds(delay_s));
      co_await timer.async_wait(boost::asio::use_awaitable);
    }

    try {
      std::string response = co_await http_get(path, cookie);

      auto json_array = nlohmann::json::parse(response);
      if (json_array.empty()) {
        co_return nlohmann::json::object();
      }

      if (attempt > 1) {
        OBCX_INFO("成功获取种子状态 (尝试 {})", attempt);
      }
      co_return json_array[0];

    } catch (const std::exception &e) {
      last_error = e.what();
      std::string error_str(last_error);

      // Check if it's a connection error (DNS/network issue)
      bool is_connection_error =
          error_str.find("Connection timed out") != std::string::npos ||
          error_str.find("No route to host") != std::string::npos ||
          error_str.find("Connection refused") != std::string::npos ||
          error_str.find("HTTP GET failed") != std::string::npos;

      if (!is_connection_error || attempt >= max_retries) {
        throw std::runtime_error(
            fmt::format("获取种子状态失败，已重试{}次: {}", max_retries, last_error));
      }

      OBCX_WARN("获取种子状态失败 (尝试 {}/{}): {}", attempt, max_retries, last_error);
    }
  }

  // Should never reach here
  throw std::runtime_error("获取种子状态失败: " + last_error);
}

boost::asio::awaitable<void>
QBittorrentClient::delete_torrent(const std::string &cookie,
                                   const std::string &hash, bool delete_files) {
  std::string path = "/api/v2/torrents/delete";
  std::string body = fmt::format("hashes={}&deleteFiles={}",
                                 hash, delete_files ? "true" : "false");

  co_await http_post(path, body, cookie);
  OBCX_INFO("Torrent {} deleted", hash);
}

boost::asio::awaitable<nlohmann::json>
QBittorrentClient::get_torrent_properties(const std::string &cookie,
                                           const std::string &hash) {
  std::string path = fmt::format("/api/v2/torrents/properties?hash={}", hash);

  // Retry logic for unstable DDNS resolution
  const int max_retries = 3;
  std::string last_error;

  for (int attempt = 1; attempt <= max_retries; attempt++) {
    // Delay before retry (except first attempt)
    if (attempt > 1) {
      int delay_s = attempt - 1;  // 指数退避: 1s, 2s
      OBCX_WARN("等待{}s后重试获取种子属性 (尝试 {}/{})", delay_s, attempt, max_retries);
      boost::asio::steady_timer timer(ioc_, std::chrono::seconds(delay_s));
      co_await timer.async_wait(boost::asio::use_awaitable);
    }

    try {
      std::string response = co_await http_get(path, cookie);
      if (attempt > 1) {
        OBCX_INFO("成功获取种子属性 (尝试 {})", attempt);
      }
      co_return nlohmann::json::parse(response);

    } catch (const std::exception &e) {
      last_error = e.what();
      std::string error_str(last_error);

      bool is_connection_error =
          error_str.find("Connection timed out") != std::string::npos ||
          error_str.find("No route to host") != std::string::npos ||
          error_str.find("Connection refused") != std::string::npos ||
          error_str.find("HTTP GET failed") != std::string::npos;

      if (!is_connection_error || attempt >= max_retries) {
        throw std::runtime_error(
            fmt::format("获取种子属性失败，已重试{}次: {}", max_retries, last_error));
      }

      OBCX_WARN("获取种子属性失败 (尝试 {}/{}): {}", attempt, max_retries, last_error);
    }
  }

  throw std::runtime_error("获取种子属性失败: " + last_error);
}

boost::asio::awaitable<nlohmann::json>
QBittorrentClient::get_all_torrents(const std::string &cookie) {
  std::string path = "/api/v2/torrents/info";
  std::string response = co_await http_get(path, cookie);

  OBCX_DEBUG("get_all_torrents response length: {}", response.length());
  if (!response.empty()) {
    OBCX_DEBUG("First bytes: {:02x} {:02x} {:02x} {:02x}",
               static_cast<unsigned char>(response[0]),
               response.length() > 1 ? static_cast<unsigned char>(response[1]) : 0,
               response.length() > 2 ? static_cast<unsigned char>(response[2]) : 0,
               response.length() > 3 ? static_cast<unsigned char>(response[3]) : 0);
  }

  co_return nlohmann::json::parse(response);
}

boost::asio::awaitable<nlohmann::json>
QBittorrentClient::get_torrent_files(const std::string &cookie,
                                      const std::string &hash) {
  std::string path = fmt::format("/api/v2/torrents/files?hash={}", hash);
  std::string response = co_await http_get(path, cookie);
  co_return nlohmann::json::parse(response);
}

} // namespace plugins
