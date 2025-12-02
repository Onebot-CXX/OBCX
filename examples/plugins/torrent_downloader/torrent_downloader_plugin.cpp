#include "torrent_downloader_plugin.hpp"
#include "qbittorrent_client.hpp"
#include "common/logger.hpp"
#include "common/message_type.hpp"
#include "common/config_loader.hpp"
#include "core/tg_bot.hpp"
#include "telegram/network/connection_manager.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace plugins {

TorrentDownloaderPlugin::TorrentDownloaderPlugin() {
  OBCX_DEBUG("TorrentDownloaderPlugin constructor called");
}

TorrentDownloaderPlugin::~TorrentDownloaderPlugin() {
  shutdown();
  OBCX_DEBUG("TorrentDownloaderPlugin destructor called");
}

std::string TorrentDownloaderPlugin::get_name() const {
  return "torrent_downloader";
}

std::string TorrentDownloaderPlugin::get_version() const { return "2.0.0"; }

std::string TorrentDownloaderPlugin::get_description() const {
  return "Torrent downloader plugin with qBittorrent WebAPI and Google Drive upload";
}

bool TorrentDownloaderPlugin::initialize() {
  try {
    OBCX_INFO("Initializing Torrent Downloader Plugin (qBittorrent)...");

    if (!load_configuration()) {
      OBCX_ERROR("Failed to load plugin configuration");
      return false;
    }

    // Load saved tasks from file
    load_tasks_from_file();

    // Register event callbacks
    try {
      auto [lock, bots] = get_bots();

      for (auto &bot_ptr : bots) {
        if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
          tg_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_tg_message(bot, event);
              });
          OBCX_INFO(
              "Registered Telegram message callback for Torrent Downloader");
          break;
        }
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("Failed to register callbacks: {}", e.what());
      return false;
    }

    OBCX_INFO("Torrent Downloader Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during Torrent Downloader Plugin initialization: {}",
               e.what());
    return false;
  }
}

void TorrentDownloaderPlugin::deinitialize() {
  try {
    OBCX_INFO("Deinitializing Torrent Downloader Plugin...");

    // Save tasks before shutdown
    save_tasks_to_file();

    OBCX_INFO("Torrent Downloader Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during Torrent Downloader Plugin deinitialization: {}",
               e.what());
  }
}

void TorrentDownloaderPlugin::shutdown() {
  try {
    OBCX_INFO("Shutting down Torrent Downloader Plugin...");
    OBCX_INFO("Torrent Downloader Plugin shutdown complete");
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during Torrent Downloader Plugin shutdown: {}",
               e.what());
  }
}

bool TorrentDownloaderPlugin::load_configuration() {
  try {
    OBCX_INFO("Loading torrent_downloader plugin configuration...");
    OBCX_INFO("Plugin name from get_name(): '{}'", get_name());

    // Debug: Try to get config section directly
    auto config_section = get_config_section("config");
    if (config_section) {
      OBCX_INFO("Found config section via get_config_section()");
    } else {
      OBCX_WARN("config section not found via get_config_section()");
    }

    // Debug: Check if plugin config exists
    auto& config_loader = obcx::common::ConfigLoader::instance();
    auto plugin_config = config_loader.get_plugin_config(get_name());
    if (!plugin_config) {
      OBCX_ERROR("Plugin config section not found for plugin: {}", get_name());
      OBCX_ERROR("Make sure [plugins.{}] section exists in config file", get_name());
      return false;
    }
    OBCX_INFO("Plugin config section found, enabled={}", plugin_config->enabled);

    // qBittorrent settings
    auto qbt_host_opt = get_config_value<std::string>("qbt_host");
    config_.qbt_host = qbt_host_opt.value_or("127.0.0.1");
    OBCX_INFO("  qbt_host: {} ({})", config_.qbt_host,
              qbt_host_opt.has_value() ? "from config" : "default");

    auto qbt_port_opt = get_config_value<int64_t>("qbt_port");
    config_.qbt_port = static_cast<int>(qbt_port_opt.value_or(8080));
    OBCX_INFO("  qbt_port: {} ({})", config_.qbt_port,
              qbt_port_opt.has_value() ? "from config" : "default");

    auto qbt_use_ssl_opt = get_config_value<bool>("qbt_use_ssl");
    config_.qbt_use_ssl = qbt_use_ssl_opt.value_or(false);
    OBCX_INFO("  qbt_use_ssl: {} ({})", config_.qbt_use_ssl,
              qbt_use_ssl_opt.has_value() ? "from config" : "default");

    auto qbt_username_opt = get_config_value<std::string>("qbt_username");
    config_.qbt_username = qbt_username_opt.value_or("admin");
    OBCX_INFO("  qbt_username: {} ({})", config_.qbt_username,
              qbt_username_opt.has_value() ? "from config" : "default");

    auto qbt_password_opt = get_config_value<std::string>("qbt_password");
    config_.qbt_password = qbt_password_opt.value_or("");
    OBCX_INFO("  qbt_password: {} ({})",
              config_.qbt_password.empty() ? "<empty>" : "***",
              qbt_password_opt.has_value() ? "from config" : "default");

    auto qbt_download_path_opt = get_config_value<std::string>("qbt_download_path");
    config_.qbt_download_path = qbt_download_path_opt.value_or("");
    OBCX_INFO("  qbt_download_path: {} ({})",
              config_.qbt_download_path.empty() ? "<default>" : config_.qbt_download_path,
              qbt_download_path_opt.has_value() ? "from config" : "default");

    // rclone settings
    auto rclone_remote_opt = get_config_value<std::string>("rclone_remote");
    config_.rclone_remote = rclone_remote_opt.value_or("gdrive:");
    OBCX_INFO("  rclone_remote: {} ({})", config_.rclone_remote,
              rclone_remote_opt.has_value() ? "from config" : "default");

    auto rclone_path_opt = get_config_value<std::string>("rclone_path");
    config_.rclone_path = rclone_path_opt.value_or("Torrents");
    OBCX_INFO("  rclone_path: {} ({})", config_.rclone_path,
              rclone_path_opt.has_value() ? "from config" : "default");

    auto rclone_proxy_opt = get_config_value<std::string>("rclone_proxy");
    config_.rclone_proxy = rclone_proxy_opt.value_or("");
    OBCX_INFO("  rclone_proxy: {} ({})",
              config_.rclone_proxy.empty() ? "<none>" : config_.rclone_proxy,
              rclone_proxy_opt.has_value() ? "from config" : "default");

    // General settings
    auto max_downloads_opt = get_config_value<int64_t>("max_concurrent_downloads");
    config_.max_concurrent_downloads = static_cast<int>(max_downloads_opt.value_or(3));
    OBCX_INFO("  max_concurrent_downloads: {} ({})",
              config_.max_concurrent_downloads,
              max_downloads_opt.has_value() ? "from config" : "default");

    auto check_interval_opt = get_config_value<int64_t>("progress_check_interval");
    config_.progress_check_interval = static_cast<int>(check_interval_opt.value_or(5));
    OBCX_INFO("  progress_check_interval: {} ({})",
              config_.progress_check_interval,
              check_interval_opt.has_value() ? "from config" : "default");

    // Initialize RcloneClient
    rclone_client_ = std::make_unique<RcloneClient>(
        config_.rclone_remote, config_.rclone_path, config_.rclone_proxy);

    OBCX_INFO("Configuration loaded successfully");
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to load configuration: {}", e.what());
    return false;
  }
}

boost::asio::awaitable<void> TorrentDownloaderPlugin::handle_tg_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event) {

  auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&bot);
  if (!tg_bot) {
    co_return;
  }

  std::string chat_id =
      event.group_id.has_value() ? event.group_id.value() : event.user_id;

  // Check for /status command
  if (event.raw_message.starts_with("/status")) {
    co_await handle_status_command(bot, chat_id);
    co_return;
  }

  // Check for /reupload command
  if (event.raw_message.starts_with("/reupload")) {
    // Extract task_id from command: /reupload task_id
    std::istringstream iss(event.raw_message);
    std::string cmd, task_id;
    iss >> cmd >> task_id;

    if (task_id.empty()) {
      obcx::common::Message reply = {
          {{.type = {"text"},
            .data = {{"text", "用法: /reupload <task_id>\n使用 /status 查看失败的任务"}}}}};
      co_await bot.send_group_message(chat_id, reply);
    } else {
      co_await handle_reupload_command(bot, chat_id, task_id);
    }
    co_return;
  }

  std::string error_msg;
  bool has_error = false;

  try {
    // Check for magnet link
    std::string magnet_link = extract_magnet_link(event.raw_message);
    if (!magnet_link.empty()) {
      OBCX_INFO("Detected magnet link in message from chat {}", chat_id);

      obcx::common::Message reply = {
          {{.type = {"text"}, .data = {{"text", "开始下载磁力链接..."}}}}};

      if (event.group_id.has_value()) {
        co_await bot.send_group_message(event.group_id.value(), reply);
      } else {
        co_await bot.send_private_message(event.user_id, reply);
      }

      co_await start_download(*tg_bot, chat_id, magnet_link, true);
      co_return;
    }

    // Check for torrent file
    for (const auto &segment : event.message) {
      if (segment.type == "file" || segment.type == "document") {
        auto it = segment.data.find("file_name");
        if (it != segment.data.end()) {
          std::string filename = it->get<std::string>();
          if (filename.ends_with(".torrent")) {
            OBCX_INFO("Detected torrent file in message from chat {}", chat_id);

            obcx::common::Message reply = {
                {{.type = {"text"}, .data = {{"text", "开始下载torrent文件..."}}}}};

            if (event.group_id.has_value()) {
              co_await bot.send_group_message(event.group_id.value(), reply);
            } else {
              co_await bot.send_private_message(event.user_id, reply);
            }

            auto file_id_it = segment.data.find("file_id");
            if (file_id_it != segment.data.end()) {
              std::string file_id = file_id_it->get<std::string>();
              std::string torrent_path =
                  co_await download_torrent_file(*tg_bot, file_id);

              co_await start_download(*tg_bot, chat_id, torrent_path, false);
            }
            co_return;
          }
        }
      }
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("Error handling torrent message: {}", e.what());
    error_msg = e.what();
    has_error = true;
  }

  if (has_error) {
    obcx::common::Message error_reply = {
        {{.type = {"text"}, .data = {{"text", fmt::format("错误: {}", error_msg)}}}}};

    if (event.group_id.has_value()) {
      co_await bot.send_group_message(event.group_id.value(), error_reply);
    } else {
      co_await bot.send_private_message(event.user_id, error_reply);
    }
  }

  co_return;
}

std::string TorrentDownloaderPlugin::extract_magnet_link(const std::string &text) {
  // Match magnet:?xt=urn:btih: pattern
  std::regex magnet_regex(R"(magnet:\?xt=urn:btih:[a-zA-Z0-9]+[^\s]*)");
  std::smatch match;

  if (std::regex_search(text, match, magnet_regex)) {
    return match.str();
  }

  return "";
}

boost::asio::awaitable<std::string>
TorrentDownloaderPlugin::download_torrent_file(obcx::core::TGBot &bot,
                                                 const std::string &file_id) {
  try {
    // Create MediaFileInfo for the torrent file
    obcx::core::MediaFileInfo media_info;
    media_info.file_id = file_id;
    media_info.file_type = "document";

    // Get download URL
    auto download_url_opt = co_await bot.get_media_download_url(media_info);
    if (!download_url_opt.has_value()) {
      throw std::runtime_error("无法获取种子文件下载链接");
    }

    std::string download_url = download_url_opt.value();

    // Get connection manager to download file content
    auto *conn_manager = bot.get_connection_manager();
    if (!conn_manager) {
      throw std::runtime_error("连接管理器未初始化");
    }

    // Cast to TelegramConnectionManager to access download_file_content
    auto *tg_conn_manager = dynamic_cast<obcx::network::TelegramConnectionManager*>(conn_manager);
    if (!tg_conn_manager) {
      throw std::runtime_error("连接管理器类型不正确");
    }

    // Download file content
    std::string file_content = co_await tg_conn_manager->download_file_content(download_url);
    if (file_content.empty()) {
      throw std::runtime_error("下载的种子文件内容为空");
    }

    // Create temporary file path
    std::string temp_path = fmt::format("/tmp/torrent_{}.torrent",
                                        std::chrono::system_clock::now().time_since_epoch().count());

    // Save to file
    std::ofstream file(temp_path, std::ios::binary);
    if (!file) {
      throw std::runtime_error("无法创建临时文件: " + temp_path);
    }
    file.write(file_content.data(), file_content.size());
    file.close();

    OBCX_INFO("Downloaded torrent file to {}", temp_path);
    co_return temp_path;

  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to download torrent file: {}", e.what());
    throw std::runtime_error(fmt::format("下载种子文件失败: {}", e.what()));
  }
}

boost::asio::awaitable<void> TorrentDownloaderPlugin::start_download(
    obcx::core::TGBot &bot, const std::string &chat_id,
    const std::string &source, bool is_magnet) {

  // Check concurrent download limit
  if (active_downloads_.size() >= config_.max_concurrent_downloads) {
    obcx::common::Message reply = {
        {{.type = {"text"}, .data = {{"text", "下载队列已满，请稍后再试"}}}}};

    co_await bot.send_group_message(chat_id, reply);
    co_return;
  }

  // Create qBittorrent client
    QBittorrentClient qbt_client(config_.qbt_host, config_.qbt_port, config_.qbt_use_ssl,
                                config_.qbt_username, config_.qbt_password);

  std::string task_id = fmt::format("dl_{}", ++download_counter_);

  std::string error_text;
  bool has_error = false;

  try {
    // Login to qBittorrent
    std::string cookie = co_await qbt_client.login();

    // Add torrent with save path
    auto add_result = co_await qbt_client.add_torrent(cookie, source, is_magnet,
                                                        config_.qbt_download_path);

    // Check if torrent already existed - if so, reject the task
    if (add_result.already_existed) {
      OBCX_WARN("Torrent already exists in qBittorrent (hash: {}), rejecting task",
                add_result.hash);
      error_text = fmt::format("❌ 该种子任务已存在于qBittorrent中\n种子哈希: {}",
                               add_result.hash);
      has_error = true;

      // Don't decrement counter since we already incremented it
      download_counter_--;
    } else {
      // Create task only if torrent is new
      DownloadTask task;
      task.task_id = task_id;
      task.chat_id = chat_id;
      task.qbt_hash = add_result.hash;
      task.source = source;
      task.is_magnet = is_magnet;
      task.start_time = std::chrono::steady_clock::now();
      task.filename = is_magnet ? "磁力链接" : fs::path(source).filename().string();
      task.save_path = config_.qbt_download_path;  // Will be updated from qBittorrent later
      task.torrent_already_existed = false;  // We know it's new

      active_downloads_[task_id] = task;
      save_tasks_to_file();  // Save new task

      OBCX_INFO("Started download task {} with hash {}", task_id, add_result.hash);

      // Monitor download in background
      boost::asio::co_spawn(
          bot.get_task_scheduler().get_io_context(),
          [this, &bot, task_id, cookie]() -> boost::asio::awaitable<void> {
            co_await monitor_download(bot, task_id, cookie);
          },
          boost::asio::detached);
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("Error starting download: {}", e.what());
    error_text = fmt::format("启动下载失败: {}", e.what());
    has_error = true;
  }

  if (has_error) {
    obcx::common::Message error_reply = {
        {{.type = {"text"}, .data = {{"text", error_text}}}}};

    co_await bot.send_group_message(chat_id, error_reply);
  }

  co_return;
}

boost::asio::awaitable<void>
TorrentDownloaderPlugin::monitor_download(obcx::core::IBot &bot,
                                           const std::string &task_id,
                                           const std::string &cookie) {

  auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&bot);
  if (!tg_bot) {
    co_return;
  }

    QBittorrentClient qbt_client(config_.qbt_host, config_.qbt_port, config_.qbt_use_ssl,
                                config_.qbt_username, config_.qbt_password);

  auto it = active_downloads_.find(task_id);
  if (it == active_downloads_.end()) {
    co_return;
  }

  DownloadTask &task = it->second;
  std::string hash = task.qbt_hash;
  std::string chat_id = task.chat_id;

  std::string error_text;
  bool has_error = false;

  try {
    // Monitor progress
    while (true) {
      auto info = co_await qbt_client.get_torrent_info(cookie, hash);

      if (info.empty()) {
        throw std::runtime_error("Torrent not found");
      }

      // Update task info
      task.filename = info["name"].get<std::string>();
      task.downloaded_bytes = info["downloaded"].get<int64_t>();
      task.total_bytes = info["size"].get<int64_t>();
      task.download_speed = info["dlspeed"].get<double>();
      task.progress_percent =
          static_cast<int>(info["progress"].get<double>() * 100);
      task.eta_seconds = info["eta"].get<int>();
      task.state = info["state"].get<std::string>();

      // Get save path
      if (task.save_path.empty()) {
        auto props = co_await qbt_client.get_torrent_properties(cookie, hash);
        task.save_path = props["save_path"].get<std::string>();
      }

      // Check if completed
      if (task.state == "uploading" || task.state == "pausedUP" ||
          task.progress_percent >= 100) {
        OBCX_INFO("Download task {} completed", task_id);
        break;
      }

      // Wait before next check
      boost::asio::steady_timer timer(
          co_await boost::asio::this_coro::executor,
          std::chrono::seconds(config_.progress_check_interval));
      co_await timer.async_wait(boost::asio::use_awaitable);
    }

    // Mark download as completed
    task.download_completed = true;
    save_tasks_to_file();  // Save state

    // Download completed, start upload
    obcx::common::Message upload_msg = {
        {{.type = {"text"}, .data = {{"text", "下载完成，开始上传到Google Drive..."}}}}};

    co_await bot.send_group_message(chat_id, upload_msg);

    // Get actual download path from torrent properties (save_path)
    auto props = co_await qbt_client.get_torrent_properties(cookie, hash);
    task.save_path = props["save_path"].get<std::string>();
    save_tasks_to_file();  // Save save_path

    OBCX_INFO("Using save_path for upload: {}", task.save_path);

    std::string download_path = task.save_path;

    std::string gdrive_link;
    bool upload_success = false;

    try {
      // Upload to Google Drive
      std::string remote_path = co_await rclone_client_->upload(download_path);
      gdrive_link = co_await rclone_client_->get_share_link(remote_path);
      upload_success = true;
    } catch (const std::exception &upload_error) {
      // Upload failed - save to failed_downloads for retry
      OBCX_ERROR("Upload failed for task {}: {}", task_id, upload_error.what());
      error_text = fmt::format("上传失败: {}\n使用 /reupload {} 重试",
                               upload_error.what(), task_id);
      task.error_message = upload_error.what();
      failed_downloads_[task_id] = task;
      save_tasks_to_file();  // Save failed task
      has_error = true;
    }

    if (upload_success) {
      // Send success message
      obcx::common::Message success_msg = {
          {{.type = {"text"},
            .data = {{"text", fmt::format("上传完成！\n链接: {}", gdrive_link)}}}}};

      co_await bot.send_group_message(chat_id, success_msg);

      // Only delete if torrent was added by us (not pre-existing)
      if (task.torrent_already_existed) {
        OBCX_INFO("Torrent {} already existed before we added it, NOT deleting files or task",
                  hash);
      } else {
        // Validate path before deletion
        if (is_path_safe_to_delete(download_path)) {
          // Delete torrent and files from qBittorrent
          co_await qbt_client.delete_torrent(cookie, hash, true);
          OBCX_INFO("Deleted torrent {} and its files", hash);
        } else {
          OBCX_ERROR("Path {} is not safe to delete, skipping deletion", download_path);
        }
      }
    }

    // Remove from active downloads
    active_downloads_.erase(task_id);
    save_tasks_to_file();  // Save state

  } catch (const std::exception &e) {
    // Download error (before completion)
    OBCX_ERROR("Error during download {}: {}", task_id, e.what());
    error_text = fmt::format("下载错误: {}", e.what());
    has_error = true;
    active_downloads_.erase(task_id);
    save_tasks_to_file();  // Save state
  }

  // Handle error outside of catch block
  if (has_error) {
    obcx::common::Message error_msg = {
        {{.type = {"text"}, .data = {{"text", error_text}}}}};

    co_await bot.send_group_message(chat_id, error_msg);

    // Clean up torrent only if download didn't complete
    // If upload failed, keep files for retry
    if (!task.download_completed) {
      try {
        co_await qbt_client.delete_torrent(cookie, hash, true);
        OBCX_INFO("Cleaned up failed download torrent {}", hash);
      } catch (const std::exception &cleanup_error) {
        OBCX_WARN("Failed to cleanup torrent {}: {}", hash, cleanup_error.what());
      }
    } else {
      OBCX_INFO("Keeping files for task {} for potential reupload", task_id);
    }
  }

  co_return;
}


boost::asio::awaitable<void>
TorrentDownloaderPlugin::handle_reupload_command(obcx::core::IBot &bot,
                                                  const std::string &chat_id,
                                                  const std::string &task_id) {
  auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&bot);
  if (!tg_bot) {
    co_return;
  }

  // Check if task exists in failed_downloads
  auto it = failed_downloads_.find(task_id);
  if (it == failed_downloads_.end()) {
    obcx::common::Message reply = {
        {{.type = {"text"},
          .data = {{"text", fmt::format("任务 {} 不存在或未失败\n使用 /status 查看失败的任务",
                                        task_id)}}}}};
    co_await bot.send_group_message(chat_id, reply);
    co_return;
  }

  DownloadTask &task = it->second;

  obcx::common::Message start_msg = {
      {{.type = {"text"},
        .data = {{"text", fmt::format("开始重新上传任务 {}...", task_id)}}}}};
  co_await bot.send_group_message(chat_id, start_msg);

  // Use save_path if available, otherwise fall back to save_path + filename
  std::string download_path = task.save_path.empty()
                               ? (task.save_path + "/" + task.filename)
                               : task.save_path;

  OBCX_INFO("Reupload using path: {}", download_path);

  std::string error_text;
  bool success = false;

  try {
    // Attempt upload
    std::string remote_path = co_await rclone_client_->upload(download_path);
    std::string gdrive_link = co_await rclone_client_->get_share_link(remote_path);

    // Send success message
    obcx::common::Message success_msg = {
        {{.type = {"text"},
          .data = {{"text", fmt::format("重新上传完成！\n链接: {}", gdrive_link)}}}}};
    co_await bot.send_group_message(chat_id, success_msg);

    // Only delete if torrent was added by us (not pre-existing)
    if (task.torrent_already_existed) {
      OBCX_INFO("Torrent {} already existed, NOT deleting files or task after reupload",
                task.qbt_hash);
    } else {
      // Validate path before deletion
      if (is_path_safe_to_delete(download_path)) {
        // Delete torrent and files after successful upload
        QBittorrentClient qbt_client(config_.qbt_host, config_.qbt_port,
                                      config_.qbt_use_ssl, config_.qbt_username,
                                      config_.qbt_password);
        std::string cookie = co_await qbt_client.login();
        co_await qbt_client.delete_torrent(cookie, task.qbt_hash, true);
        OBCX_INFO("Deleted torrent {} and files after reupload", task.qbt_hash);
      } else {
        OBCX_ERROR("Path {} is not safe to delete, skipping deletion", download_path);
      }
    }

    // Remove from failed downloads
    failed_downloads_.erase(task_id);
    save_tasks_to_file();  // Save state
    success = true;

  } catch (const std::exception &e) {
    OBCX_ERROR("Reupload failed for task {}: {}", task_id, e.what());
    error_text = fmt::format("重新上传失败: {}\n稍后可再次尝试 /reupload {}",
                             e.what(), task_id);
    task.error_message = e.what();  // Update error message
    save_tasks_to_file();  // Save updated error
  }

  if (!success) {
    obcx::common::Message error_msg = {
        {{.type = {"text"}, .data = {{"text", error_text}}}}};
    co_await bot.send_group_message(chat_id, error_msg);
  }

  co_return;
}

// Progress tracking functions
std::string TorrentDownloaderPlugin::format_bytes(int64_t bytes) {
  if (bytes < 1024) {
    return fmt::format("{}B", bytes);
  } else if (bytes < 1024 * 1024) {
    return fmt::format("{:.1f}KB", bytes / 1024.0);
  } else if (bytes < 1024 * 1024 * 1024) {
    return fmt::format("{:.1f}MB", bytes / (1024.0 * 1024.0));
  } else {
    return fmt::format("{:.2f}GB", bytes / (1024.0 * 1024.0 * 1024.0));
  }
}

std::string TorrentDownloaderPlugin::format_eta(int seconds) {
  if (seconds < 0) {
    return "未知";
  } else if (seconds < 60) {
    return fmt::format("{}秒", seconds);
  } else if (seconds < 3600) {
    return fmt::format("{}分{}秒", seconds / 60, seconds % 60);
  } else {
    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;
    return fmt::format("{}小时{}分", hours, mins);
  }
}

std::string TorrentDownloaderPlugin::create_progress_bar(int percent, int width) {
  int filled = (percent * width) / 100;
  std::string bar;

  for (int i = 0; i < width; i++) {
    if (i < filled) {
      bar += "█";
    } else {
      bar += "░";
    }
  }

  return bar;
}

boost::asio::awaitable<void> TorrentDownloaderPlugin::handle_status_command(
    obcx::core::IBot &bot, const std::string &chat_id) {

  if (active_downloads_.empty() && failed_downloads_.empty()) {
    obcx::common::Message reply = {
        {{.type = {"text"}, .data = {{"text", "📊 当前没有任务"}}}}};

    co_await bot.send_group_message(chat_id, reply);
    co_return;
  }

  // Format status message
  std::string status_msg = "📊 下载任务状态\n\n";

  if (!active_downloads_.empty()) {
    int task_num = 1;
    for (const auto &[task_id, task] : active_downloads_) {
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - task.start_time)
                         .count();

      status_msg += fmt::format("任务 #{}: {} (ID: {})\n", task_num++, task.filename, task_id);
      status_msg += fmt::format("├─ 状态: {}\n", task.state);

    if (task.total_bytes > 0) {
      std::string progress_bar = create_progress_bar(task.progress_percent);
      status_msg += fmt::format("├─ 进度: {} {}% ({} / {})\n", progress_bar,
                                task.progress_percent,
                                format_bytes(task.downloaded_bytes),
                                format_bytes(task.total_bytes));
    } else {
      status_msg += fmt::format("├─ 进度: 准备中... (已运行 {}秒)\n", elapsed);
    }

    if (task.download_speed > 0) {
      status_msg += fmt::format(
          "├─ 速度: {}/s\n",
          format_bytes(static_cast<int64_t>(task.download_speed)));
    }

    if (task.eta_seconds >= 0) {
      status_msg +=
          fmt::format("└─ 预计: 剩余 {}\n", format_eta(task.eta_seconds));
    } else {
      status_msg += "└─ 预计: 计算中...\n";
    }

      status_msg += "\n";
    }

    status_msg += fmt::format("总计: {}个活跃任务\n", active_downloads_.size());
  }

  // Show failed uploads if any
  if (!failed_downloads_.empty()) {
    status_msg += "\n\n❌ 上传失败的任务:\n\n";

    int failed_num = 1;
    for (const auto &[task_id, task] : failed_downloads_) {
      status_msg += fmt::format("任务 #{}: {} (ID: {})\n", failed_num++,
                                task.filename, task_id);
      status_msg += fmt::format("├─ 错误: {}\n", task.error_message);
      status_msg += fmt::format("└─ 使用 /reupload {} 重新上传\n\n", task_id);
    }

    status_msg += fmt::format("失败任务: {}个", failed_downloads_.size());
  }

  obcx::common::Message reply = {
      {{.type = {"text"}, .data = {{"text", status_msg}}}}};

  co_await bot.send_group_message(chat_id, reply);
  co_return;
}

// Persistence functions
std::string TorrentDownloaderPlugin::get_tasks_file_path() const {
  return config_.qbt_download_path + "/.torrent_tasks.json";
}

void TorrentDownloaderPlugin::save_tasks_to_file() {
  try {
    nlohmann::json j;

    // Save active downloads
    nlohmann::json active_arr = nlohmann::json::array();
    for (const auto &[task_id, task] : active_downloads_) {
      nlohmann::json task_obj;
      task_obj["task_id"] = task.task_id;
      task_obj["chat_id"] = task.chat_id;
      task_obj["qbt_hash"] = task.qbt_hash;
      task_obj["source"] = task.source;
      task_obj["is_magnet"] = task.is_magnet;
      task_obj["filename"] = task.filename;
      task_obj["save_path"] = task.save_path;
      task_obj["save_path"] = task.save_path;
      task_obj["download_completed"] = task.download_completed;
      task_obj["torrent_already_existed"] = task.torrent_already_existed;
      task_obj["error_message"] = task.error_message;
      active_arr.push_back(task_obj);
    }
    j["active_downloads"] = active_arr;

    // Save failed downloads
    nlohmann::json failed_arr = nlohmann::json::array();
    for (const auto &[task_id, task] : failed_downloads_) {
      nlohmann::json task_obj;
      task_obj["task_id"] = task.task_id;
      task_obj["chat_id"] = task.chat_id;
      task_obj["qbt_hash"] = task.qbt_hash;
      task_obj["source"] = task.source;
      task_obj["is_magnet"] = task.is_magnet;
      task_obj["filename"] = task.filename;
      task_obj["save_path"] = task.save_path;
      task_obj["save_path"] = task.save_path;
      task_obj["download_completed"] = task.download_completed;
      task_obj["torrent_already_existed"] = task.torrent_already_existed;
      task_obj["error_message"] = task.error_message;
      failed_arr.push_back(task_obj);
    }
    j["failed_downloads"] = failed_arr;

    j["download_counter"] = download_counter_;

    // Write to file
    std::ofstream file(get_tasks_file_path());
    if (file.is_open()) {
      file << j.dump(2);
      file.close();
      OBCX_DEBUG("Tasks saved to {}", get_tasks_file_path());
    } else {
      OBCX_ERROR("Failed to open tasks file for writing: {}", get_tasks_file_path());
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to save tasks: {}", e.what());
  }
}

void TorrentDownloaderPlugin::load_tasks_from_file() {
  try {
    std::string file_path = get_tasks_file_path();

    if (!std::filesystem::exists(file_path)) {
      OBCX_INFO("No saved tasks file found at {}", file_path);
      return;
    }

    std::ifstream file(file_path);
    if (!file.is_open()) {
      OBCX_ERROR("Failed to open tasks file for reading: {}", file_path);
      return;
    }

    nlohmann::json j;
    file >> j;
    file.close();

    // Load active downloads
    if (j.contains("active_downloads") && j["active_downloads"].is_array()) {
      for (const auto &task_obj : j["active_downloads"]) {
        DownloadTask task;
        task.task_id = task_obj.value("task_id", "");
        task.chat_id = task_obj.value("chat_id", "");
        task.qbt_hash = task_obj.value("qbt_hash", "");
        task.source = task_obj.value("source", "");
        task.is_magnet = task_obj.value("is_magnet", false);
        task.filename = task_obj.value("filename", "");
        task.save_path = task_obj.value("save_path", "");
        task.save_path = task_obj.value("save_path", "");
        task.download_completed = task_obj.value("download_completed", false);
        task.torrent_already_existed = task_obj.value("torrent_already_existed", false);
        task.error_message = task_obj.value("error_message", "");
        task.start_time = std::chrono::steady_clock::now();  // Reset start time

        active_downloads_[task.task_id] = task;
      }
      OBCX_INFO("Loaded {} active downloads", active_downloads_.size());
    }

    // Load failed downloads
    if (j.contains("failed_downloads") && j["failed_downloads"].is_array()) {
      for (const auto &task_obj : j["failed_downloads"]) {
        DownloadTask task;
        task.task_id = task_obj.value("task_id", "");
        task.chat_id = task_obj.value("chat_id", "");
        task.qbt_hash = task_obj.value("qbt_hash", "");
        task.source = task_obj.value("source", "");
        task.is_magnet = task_obj.value("is_magnet", false);
        task.filename = task_obj.value("filename", "");
        task.save_path = task_obj.value("save_path", "");
        task.save_path = task_obj.value("save_path", "");
        task.download_completed = task_obj.value("download_completed", false);
        task.torrent_already_existed = task_obj.value("torrent_already_existed", false);
        task.error_message = task_obj.value("error_message", "");
        task.start_time = std::chrono::steady_clock::now();  // Reset start time

        failed_downloads_[task.task_id] = task;
      }
      OBCX_INFO("Loaded {} failed downloads", failed_downloads_.size());
    }

    // Load counter
    if (j.contains("download_counter")) {
      download_counter_ = j["download_counter"].get<int>();
      OBCX_INFO("Loaded download counter: {}", download_counter_);
    }

  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to load tasks: {}", e.what());
  }
}

// Path validation
bool TorrentDownloaderPlugin::is_path_safe_to_delete(const std::string &path) const {
  try {
    namespace fs = std::filesystem;

    // Get absolute paths
    fs::path abs_path = fs::absolute(path);
    fs::path abs_download_path = fs::absolute(config_.qbt_download_path);

    // Check if path starts with download path
    auto path_str = abs_path.string();
    auto download_path_str = abs_download_path.string();

    // Ensure download path ends with separator for proper prefix matching
    if (!download_path_str.empty() && download_path_str.back() != '/') {
      download_path_str += '/';
    }

    bool is_safe = path_str.find(download_path_str) == 0;

    if (!is_safe) {
      OBCX_WARN("Path {} is NOT under download path {}, refusing to delete",
                path_str, download_path_str);
    }

    return is_safe;
  } catch (const std::exception &e) {
    OBCX_ERROR("Error validating path {}: {}", path, e.what());
    return false;  // Fail-safe: don't delete if we can't verify
  }
}

// Shell escaping for safe command execution
std::string TorrentDownloaderPlugin::shell_escape(const std::string &arg) const {
  // Use single quotes and escape any single quotes in the string
  // by replacing ' with '\''
  std::string result = "'";
  for (char c : arg) {
    if (c == '\'') {
      result += "'\\''";  // End quote, escaped quote, start quote
    } else {
      result += c;
    }
  }
  result += "'";
  return result;
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::TorrentDownloaderPlugin)
