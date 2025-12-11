#pragma once

#include "common/message_type.hpp"
#include "network/http_client.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace plugins {

/**
 * @brief add_torrent的结果
 */
struct AddTorrentResult {
  std::string hash;     // torrent hash
  bool already_existed; // 是否已存在
};

/**
 * @brief qBittorrent WebAPI 客户端
 *
 * 封装所有与qBittorrent WebUI的HTTP交互
 */
class QBittorrentClient {
public:
  QBittorrentClient(const std::string &host, int port, bool use_ssl,
                    const std::string &username, const std::string &password);

  ~QBittorrentClient();

  // Authentication
  boost::asio::awaitable<std::string> login();

  // Torrent management
  boost::asio::awaitable<AddTorrentResult> add_torrent(
      const std::string &cookie, const std::string &source, bool is_magnet,
      const std::string &save_path = "");

  boost::asio::awaitable<nlohmann::json> get_torrent_info(
      const std::string &cookie, const std::string &hash);

  boost::asio::awaitable<nlohmann::json> get_all_torrents(
      const std::string &cookie);

  boost::asio::awaitable<void> delete_torrent(const std::string &cookie,
                                              const std::string &hash,
                                              bool delete_files);

  // Torrent properties
  boost::asio::awaitable<nlohmann::json> get_torrent_properties(
      const std::string &cookie, const std::string &hash);

  boost::asio::awaitable<nlohmann::json> get_torrent_files(
      const std::string &cookie, const std::string &hash);

private:
  boost::asio::io_context ioc_; // Own io_context for HttpClient
  std::thread ioc_thread_;      // Thread to run io_context
  std::unique_ptr<obcx::network::HttpClient> http_client_;
  std::string host_;
  int port_;
  bool use_ssl_;
  std::string username_;
  std::string password_;

  // Helper functions
  std::string build_url(const std::string &endpoint);
  std::string url_encode(const std::string &value);

  // HTTP methods using HttpClient
  boost::asio::awaitable<std::string> http_post(
      const std::string &path, const std::string &body,
      const std::string &cookie = "",
      const std::string &content_type = "application/x-www-form-urlencoded");

  boost::asio::awaitable<std::string> http_get(const std::string &path,
                                               const std::string &cookie = "");
};

} // namespace plugins
