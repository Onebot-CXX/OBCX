#pragma once

#include "interfaces/plugin.hpp"
#include "rclone_client.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace obcx::core {
class TGBot;
}

namespace plugins {

/**
 * @brief Torrent下载插件
 *
 * 功能：
 * 1. 接收torrent文件或磁力链接
 * 2. 使用qBittorrent WebAPI下载
 * 3. 使用rclone（带代理）上传到Google Drive
 * 4. 返回分享链接
 */
class TorrentDownloaderPlugin : public obcx::interface::IPlugin {
public:
  TorrentDownloaderPlugin();
  ~TorrentDownloaderPlugin() override;

  // IPlugin interface
  std::string get_name() const override;
  std::string get_version() const override;
  std::string get_description() const override;
  bool initialize() override;
  void deinitialize() override;
  void shutdown() override;

private:
  struct Config {
    // qBittorrent WebAPI settings
    std::string qbt_host = "127.0.0.1";
    int qbt_port = 8080;
    bool qbt_use_ssl = false;
    std::string qbt_username = "admin";
    std::string qbt_password = "";
    std::string qbt_download_path = ""; // Download path for torrents

    // rclone settings
    std::string rclone_remote = "gdrive:";
    std::string rclone_path = "Torrents";
    std::string rclone_proxy = ""; // e.g., "http://127.0.0.1:7890"

    // General settings
    int max_concurrent_downloads = 3;
    int progress_check_interval = 5; // seconds
  };

  struct DownloadTask {
    std::string task_id;
    std::string chat_id;
    std::string qbt_hash; // qBittorrent torrent hash
    std::string source;   // torrent file path or magnet link
    bool is_magnet;
    std::chrono::steady_clock::time_point start_time;

    // Progress information
    std::string filename;
    std::string save_path;    // qBittorrent save path
    std::string content_path; // Actual file/folder path from qBittorrent
    int64_t downloaded_bytes = 0;
    int64_t total_bytes = 0;
    double download_speed = 0.0; // bytes/sec
    int progress_percent = 0;
    int eta_seconds = 0;
    std::string state; // qBittorrent state

    // Status tracking
    bool download_completed = false;      // Download finished, ready for upload
    bool torrent_already_existed = false; // Torrent existed before we added it
    std::string error_message;            // Error message if failed
  };

  bool load_configuration();

  // Message handlers
  boost::asio::awaitable<void> handle_tg_message(
      obcx::core::IBot &bot, const obcx::common::MessageEvent &event);

  // Download handlers
  boost::asio::awaitable<void> start_download(obcx::core::TGBot &bot,
                                              const std::string &chat_id,
                                              const std::string &source,
                                              bool is_magnet);

  boost::asio::awaitable<void> monitor_download(obcx::core::IBot &bot,
                                                const std::string &task_id,
                                                const std::string &cookie);

  // Utility functions
  std::string extract_magnet_link(const std::string &text);
  boost::asio::awaitable<std::string> download_torrent_file(
      obcx::core::TGBot &bot, const std::string &file_id);

  // qBittorrent WebAPI functions
  boost::asio::awaitable<std::string> qbt_login();
  boost::asio::awaitable<std::string> qbt_add_torrent(const std::string &cookie,
                                                      const std::string &source,
                                                      bool is_magnet);
  boost::asio::awaitable<nlohmann::json> qbt_get_torrent_info(
      const std::string &cookie, const std::string &hash);
  boost::asio::awaitable<void> qbt_delete_torrent(const std::string &cookie,
                                                  const std::string &hash,
                                                  bool delete_files);
  std::string qbt_api_url(const std::string &endpoint);

  // Progress tracking
  boost::asio::awaitable<void> update_task_progress(const std::string &cookie,
                                                    DownloadTask &task);
  std::string format_bytes(int64_t bytes);
  std::string format_eta(int seconds);
  std::string create_progress_bar(int percent, int width = 10);

  // Command handlers
  boost::asio::awaitable<void> handle_status_command(
      obcx::core::IBot &bot, const std::string &chat_id);
  boost::asio::awaitable<void> handle_reupload_command(
      obcx::core::IBot &bot, const std::string &chat_id,
      const std::string &task_id);

  // Persistence
  void save_tasks_to_file();
  void load_tasks_from_file();
  std::string get_tasks_file_path() const;

  // Path validation
  bool is_path_safe_to_delete(const std::string &path) const;

  // Shell escaping
  std::string shell_escape(const std::string &arg) const;

  // Configuration
  Config config_;

  // Rclone client
  std::unique_ptr<RcloneClient> rclone_client_;

  // Active downloads tracking
  std::unordered_map<std::string, DownloadTask> active_downloads_;
  std::unordered_map<std::string, DownloadTask>
      failed_downloads_; // Tasks that failed upload
  int download_counter_ = 0;
};

} // namespace plugins
