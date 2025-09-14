#include "RetryQueueManager.hpp"

#include <cmath>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace bridge {

RetryQueueManager::RetryQueueManager(
    std::shared_ptr<obcx::storage::DatabaseManager> db_manager,
    boost::asio::io_context &io_context)
    : db_manager_(db_manager), io_context_(io_context),
      retry_timer_(std::make_unique<boost::asio::steady_timer>(io_context)),
      running_(false) {
  OBCX_INFO("RetryQueueManager initialized");
}

RetryQueueManager::~RetryQueueManager() { stop(); }

void RetryQueueManager::start() {
  if (running_) {
    OBCX_WARN("RetryQueueManager already running");
    return;
  }

  running_ = true;
  OBCX_INFO("Starting RetryQueueManager");

  // 启动重试队列处理
  boost::asio::co_spawn(io_context_, process_retry_queues(),
                        boost::asio::detached);
}

void RetryQueueManager::stop() {
  if (!running_) {
    return;
  }

  OBCX_INFO("Stopping RetryQueueManager");
  running_ = false;

  if (retry_timer_) {
    retry_timer_->cancel();
  }
}

void RetryQueueManager::add_message_retry(
    const std::string &source_platform, const std::string &target_platform,
    const std::string &source_message_id, const obcx::common::Message &message,
    const std::string &group_id, const std::string &source_group_id,
    int64_t target_topic_id, int max_retries,
    const std::string &failure_reason) {
  obcx::storage::MessageRetryInfo retry_info;
  retry_info.source_platform = source_platform;
  retry_info.target_platform = target_platform;
  retry_info.source_message_id = source_message_id;
  retry_info.message_content = serialize_message(message);
  retry_info.group_id = group_id;
  retry_info.source_group_id = source_group_id;
  retry_info.target_topic_id = target_topic_id;
  retry_info.retry_count = 0;
  retry_info.max_retry_count = max_retries;
  retry_info.failure_reason = failure_reason;
  retry_info.retry_type = "message_send";
  retry_info.created_at = std::chrono::system_clock::now();
  retry_info.last_attempt_at = std::chrono::system_clock::now();
  retry_info.next_retry_at =
      calculate_next_retry_time(0, DEFAULT_MESSAGE_RETRY_INTERVAL_SECONDS);

  if (db_manager_->add_message_retry(retry_info)) {
    OBCX_INFO("Added message retry: {} -> {} (msg_id: {})", source_platform,
              target_platform, source_message_id);
  } else {
    OBCX_ERROR("Failed to add message retry: {} -> {} (msg_id: {})",
               source_platform, target_platform, source_message_id);
  }
}

void RetryQueueManager::add_media_download_retry(
    const std::string &platform, const std::string &file_id,
    const std::string &file_type, const std::string &download_url,
    const std::string &local_path, bool use_proxy, int max_retries,
    const std::string &failure_reason) {
  obcx::storage::MediaDownloadRetryInfo retry_info;
  retry_info.platform = platform;
  retry_info.file_id = file_id;
  retry_info.file_type = file_type;
  retry_info.download_url = download_url;
  retry_info.local_path = local_path;
  retry_info.retry_count = 0;
  retry_info.max_retry_count = max_retries;
  retry_info.failure_reason = failure_reason;
  retry_info.use_proxy = use_proxy;
  retry_info.created_at = std::chrono::system_clock::now();
  retry_info.last_attempt_at = std::chrono::system_clock::now();
  retry_info.next_retry_at =
      calculate_next_retry_time(0, DEFAULT_MEDIA_RETRY_INTERVAL_SECONDS);

  if (db_manager_->add_media_download_retry(retry_info)) {
    OBCX_INFO("Added media download retry: {} (file_id: {}, use_proxy: {})",
              platform, file_id, use_proxy);
  } else {
    OBCX_ERROR("Failed to add media download retry: {} (file_id: {})", platform,
               file_id);
  }
}

void RetryQueueManager::register_message_send_callback(
    const std::string &target_platform, MessageSendCallback callback) {
  message_send_callbacks_[target_platform] = callback;
  OBCX_DEBUG("Registered message send callback for platform: {}",
             target_platform);
}

void RetryQueueManager::register_media_download_callback(
    const std::string &platform, MediaDownloadCallback callback) {
  media_download_callbacks_[platform] = callback;
  OBCX_DEBUG("Registered media download callback for platform: {}", platform);
}

boost::asio::awaitable<void> RetryQueueManager::process_retry_queues() {
  while (running_) {
    try {
      // 处理消息发送重试
      co_await process_message_retries();

      // 处理媒体下载重试
      co_await process_media_download_retries();

      // 等待下一次检查
      retry_timer_->expires_after(
          std::chrono::seconds(RETRY_QUEUE_CHECK_INTERVAL_SECONDS));
      co_await retry_timer_->async_wait(boost::asio::use_awaitable);

    } catch (const boost::system::system_error &e) {
      if (e.code() == boost::asio::error::operation_aborted) {
        OBCX_INFO("Retry queue processing cancelled");
        break;
      }
      OBCX_ERROR("Error in retry queue processing: {}", e.what());
    } catch (const std::exception &e) {
      OBCX_ERROR("Exception in retry queue processing: {}", e.what());
    }

    // 短暂等待后继续
    if (running_) {
      try {
        retry_timer_->expires_after(std::chrono::seconds(5));
        co_await retry_timer_->async_wait(boost::asio::use_awaitable);
      } catch (const boost::system::system_error &) {
        // Timer was cancelled, exit gracefully
        break;
      }
    }
  }

  OBCX_INFO("Retry queue processing stopped");
}

boost::asio::awaitable<void> RetryQueueManager::process_message_retries() {
  auto pending_retries = db_manager_->get_pending_message_retries(50);

  if (pending_retries.empty()) {
    co_return;
  }

  OBCX_DEBUG("Processing {} message retries", pending_retries.size());

  for (const auto &retry_info : pending_retries) {
    try {
      // 查找对应的回调函数
      auto callback_it =
          message_send_callbacks_.find(retry_info.target_platform);
      if (callback_it == message_send_callbacks_.end()) {
        OBCX_WARN("No callback registered for target platform: {}",
                  retry_info.target_platform);
        continue;
      }

      // 反序列化消息内容
      auto message_opt = deserialize_message(retry_info.message_content);
      if (!message_opt.has_value()) {
        OBCX_ERROR("Failed to deserialize message for retry: {} -> {}",
                   retry_info.source_platform, retry_info.target_platform);
        // 删除无效的重试记录
        db_manager_->remove_message_retry(retry_info.source_platform,
                                          retry_info.source_message_id,
                                          retry_info.target_platform);
        continue;
      }

      // 尝试发送消息
      OBCX_INFO("Retrying message send: {} -> {} (attempt {})",
                retry_info.source_platform, retry_info.target_platform,
                retry_info.retry_count + 1);

      auto result =
          co_await callback_it->second(retry_info, message_opt.value());

      if (result.has_value()) {
        // 发送成功，记录消息映射并删除重试记录
        obcx::storage::MessageMapping mapping;
        mapping.source_platform = retry_info.source_platform;
        mapping.source_message_id = retry_info.source_message_id;
        mapping.target_platform = retry_info.target_platform;
        mapping.target_message_id = result.value();
        mapping.created_at = std::chrono::system_clock::now();

        db_manager_->add_message_mapping(mapping);
        db_manager_->remove_message_retry(retry_info.source_platform,
                                          retry_info.source_message_id,
                                          retry_info.target_platform);

        OBCX_INFO("Message retry successful: {} -> {} (msg_id: {})",
                  retry_info.source_platform, retry_info.target_platform,
                  result.value());
      } else {
        // 发送失败，更新重试信息
        int new_retry_count = retry_info.retry_count + 1;

        if (new_retry_count >= retry_info.max_retry_count) {
          // 达到最大重试次数，删除记录
          db_manager_->remove_message_retry(retry_info.source_platform,
                                            retry_info.source_message_id,
                                            retry_info.target_platform);
          OBCX_WARN("Message retry failed after {} attempts: {} -> {}",
                    retry_info.max_retry_count, retry_info.source_platform,
                    retry_info.target_platform);
        } else {
          // 更新重试信息
          auto next_retry_time = calculate_next_retry_time(
              new_retry_count, DEFAULT_MESSAGE_RETRY_INTERVAL_SECONDS);
          db_manager_->update_message_retry(
              retry_info.source_platform, retry_info.source_message_id,
              retry_info.target_platform, new_retry_count, next_retry_time,
              "Send failed");

          OBCX_DEBUG("Updated message retry count to {}, next retry at: {}",
                     new_retry_count,
                     std::chrono::system_clock::to_time_t(next_retry_time));
        }
      }

    } catch (const std::exception &e) {
      OBCX_ERROR("Error processing message retry: {}", e.what());
    }
  }
}

boost::asio::awaitable<void>
RetryQueueManager::process_media_download_retries() {
  auto pending_retries = db_manager_->get_pending_media_download_retries(30);

  if (pending_retries.empty()) {
    co_return;
  }

  OBCX_DEBUG("Processing {} media download retries", pending_retries.size());

  for (const auto &retry_info : pending_retries) {
    try {
      // 查找对应的回调函数
      auto callback_it = media_download_callbacks_.find(retry_info.platform);
      if (callback_it == media_download_callbacks_.end()) {
        OBCX_WARN("No callback registered for platform: {}",
                  retry_info.platform);
        continue;
      }

      // 尝试下载媒体文件
      OBCX_INFO("Retrying media download: {} (attempt {}, use_proxy: {})",
                retry_info.file_id, retry_info.retry_count + 1,
                retry_info.use_proxy);

      auto result = co_await callback_it->second(
          retry_info.download_url, retry_info.local_path, retry_info.use_proxy);

      if (result.has_value()) {
        // 下载成功，删除重试记录
        db_manager_->remove_media_download_retry(retry_info.platform,
                                                 retry_info.file_id);
        OBCX_INFO("Media download retry successful: {} -> {}",
                  retry_info.file_id, result.value());
      } else {
        // 下载失败，更新重试信息
        int new_retry_count = retry_info.retry_count + 1;

        if (new_retry_count >= retry_info.max_retry_count) {
          // 达到最大重试次数
          // 如果之前使用代理失败，尝试直连一次
          if (retry_info.use_proxy &&
              new_retry_count == retry_info.max_retry_count) {
            OBCX_INFO("Proxy download failed, trying direct connection: {}",
                      retry_info.file_id);
            auto next_retry_time = calculate_next_retry_time(
                new_retry_count, DEFAULT_MEDIA_RETRY_INTERVAL_SECONDS);
            db_manager_->update_media_download_retry(
                retry_info.platform, retry_info.file_id, new_retry_count,
                next_retry_time, "Trying direct connection",
                false); // 不使用代理
          } else {
            // 彻底失败，删除记录
            db_manager_->remove_media_download_retry(retry_info.platform,
                                                     retry_info.file_id);
            OBCX_WARN("Media download retry failed after {} attempts: {}",
                      retry_info.max_retry_count, retry_info.file_id);
          }
        } else {
          // 更新重试信息
          auto next_retry_time = calculate_next_retry_time(
              new_retry_count, DEFAULT_MEDIA_RETRY_INTERVAL_SECONDS);
          db_manager_->update_media_download_retry(
              retry_info.platform, retry_info.file_id, new_retry_count,
              next_retry_time, "Download failed", retry_info.use_proxy);

          OBCX_DEBUG(
              "Updated media download retry count to {}, next retry at: {}",
              new_retry_count,
              std::chrono::system_clock::to_time_t(next_retry_time));
        }
      }

    } catch (const std::exception &e) {
      OBCX_ERROR("Error processing media download retry: {}", e.what());
    }
  }
}

std::chrono::system_clock::time_point
RetryQueueManager::calculate_next_retry_time(int retry_count,
                                             int base_interval_seconds) const {
  // 指数退避：2^retry_count * base_interval，但有最大限制
  int delay_seconds =
      static_cast<int>(std::pow(2, retry_count)) * base_interval_seconds;
  delay_seconds = std::min(delay_seconds, MAX_RETRY_INTERVAL_SECONDS);

  return std::chrono::system_clock::now() + std::chrono::seconds(delay_seconds);
}

std::string RetryQueueManager::serialize_message(
    const obcx::common::Message &message) const {
  nlohmann::json message_json = nlohmann::json::array();

  for (const auto &segment : message) {
    nlohmann::json segment_json;
    segment_json["type"] = segment.type;
    segment_json["data"] = segment.data;
    message_json.push_back(segment_json);
  }

  return message_json.dump();
}

std::optional<obcx::common::Message> RetryQueueManager::deserialize_message(
    const std::string &json_string) const {
  try {
    nlohmann::json message_json = nlohmann::json::parse(json_string);

    if (!message_json.is_array()) {
      return std::nullopt;
    }

    obcx::common::Message message;
    for (const auto &segment_json : message_json) {
      if (!segment_json.contains("type") || !segment_json.contains("data")) {
        return std::nullopt;
      }

      obcx::common::MessageSegment segment;
      segment.type = segment_json["type"];
      segment.data = segment_json["data"];
      message.push_back(segment);
    }

    return message;
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to deserialize message: {}", e.what());
    return std::nullopt;
  }
}

std::string RetryQueueManager::get_retry_statistics() const {
  // 获取重试队列统计信息
  std::ostringstream stats;

  try {
    auto message_retries = db_manager_->get_pending_message_retries(1000);
    auto media_retries = db_manager_->get_pending_media_download_retries(1000);

    stats << "=== 重试队列统计 ===\n";
    stats << "待重试消息数: " << message_retries.size() << "\n";
    stats << "待重试媒体下载数: " << media_retries.size() << "\n";

    // 按重试次数统计
    std::unordered_map<int, int> message_retry_counts;
    std::unordered_map<int, int> media_retry_counts;

    for (const auto &retry : message_retries) {
      message_retry_counts[retry.retry_count]++;
    }

    for (const auto &retry : media_retries) {
      media_retry_counts[retry.retry_count]++;
    }

    stats << "\n消息重试次数分布:\n";
    for (const auto &[count, num] : message_retry_counts) {
      stats << "  " << count << "次: " << num << "个\n";
    }

    stats << "\n媒体下载重试次数分布:\n";
    for (const auto &[count, num] : media_retry_counts) {
      stats << "  " << count << "次: " << num << "个\n";
    }

  } catch (const std::exception &e) {
    stats << "获取统计信息失败: " << e.what() << "\n";
  }

  return stats.str();
}

} // namespace bridge