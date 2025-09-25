#pragma once

#include "DatabaseManager.hpp"
#include "common/Logger.hpp"
#include "common/MessageType.hpp"
#include "interface/IBot.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bridge {

/**
 * @brief 重试队列管理器
 *
 * 负责管理消息发送重试和媒体下载重试的队列处理
 * 实现指数退避算法，避免频繁重试导致的系统压力
 */
class RetryQueueManager {
public:
  using MessageSendCallback =
      std::function<boost::asio::awaitable<std::optional<std::string>>(
          const obcx::storage::MessageRetryInfo &retry_info,
          const obcx::common::Message &message)>;

  using MediaDownloadCallback =
      std::function<boost::asio::awaitable<std::optional<std::string>>(
          const std::string &download_url, const std::string &local_path,
          bool use_proxy)>;

  /**
   * @brief 构造函数
   * @param db_manager 数据库管理器
   * @param io_context ASIO IO上下文
   */
  RetryQueueManager(std::shared_ptr<obcx::storage::DatabaseManager> db_manager,
                    boost::asio::io_context &io_context);

  /**
   * @brief 析构函数
   */
  ~RetryQueueManager();

  /**
   * @brief 启动重试队列处理
   */
  void start();

  /**
   * @brief 停止重试队列处理
   */
  void stop();

  /**
   * @brief 添加消息发送重试
   * @param source_platform 源平台
   * @param target_platform 目标平台
   * @param source_message_id 源消息ID
   * @param message 消息内容
   * @param group_id 目标群组ID
   * @param max_retries 最大重试次数
   * @param failure_reason 失败原因
   */
  void add_message_retry(const std::string &source_platform,
                         const std::string &target_platform,
                         const std::string &source_message_id,
                         const obcx::common::Message &message,
                         const std::string &group_id,
                         const std::string &source_group_id,
                         int64_t target_topic_id, int max_retries = 5,
                         const std::string &failure_reason = "");

  /**
   * @brief 添加媒体下载重试
   * @param platform 平台
   * @param file_id 文件ID
   * @param file_type 文件类型
   * @param download_url 下载URL
   * @param local_path 本地路径
   * @param use_proxy 是否使用代理
   * @param max_retries 最大重试次数
   * @param failure_reason 失败原因
   */
  void add_media_download_retry(const std::string &platform,
                                const std::string &file_id,
                                const std::string &file_type,
                                const std::string &download_url,
                                const std::string &local_path,
                                bool use_proxy = true, int max_retries = 3,
                                const std::string &failure_reason = "");

  /**
   * @brief 注册消息发送回调函数
   * @param target_platform 目标平台
   * @param callback 回调函数
   */
  void register_message_send_callback(const std::string &target_platform,
                                      MessageSendCallback callback);

  /**
   * @brief 注册媒体下载回调函数
   * @param platform 平台
   * @param callback 回调函数
   */
  void register_media_download_callback(const std::string &platform,
                                        MediaDownloadCallback callback);

  /**
   * @brief 获取重试统计信息
   * @return 统计信息字符串
   */
  std::string get_retry_statistics() const;

private:
  std::shared_ptr<obcx::storage::DatabaseManager> db_manager_;
  boost::asio::io_context &io_context_;
  std::unique_ptr<boost::asio::steady_timer> retry_timer_;
  bool running_;

  // 回调函数映射
  std::unordered_map<std::string, MessageSendCallback> message_send_callbacks_;
  std::unordered_map<std::string, MediaDownloadCallback>
      media_download_callbacks_;

  // 重试配置
  static constexpr int DEFAULT_MESSAGE_RETRY_INTERVAL_SECONDS = 2;
  static constexpr int DEFAULT_MEDIA_RETRY_INTERVAL_SECONDS = 5;
  static constexpr int MAX_RETRY_INTERVAL_SECONDS = 300; // 5分钟
  static constexpr int RETRY_QUEUE_CHECK_INTERVAL_SECONDS = 10;

  /**
   * @brief 定期检查重试队列
   */
  boost::asio::awaitable<void> process_retry_queues();

  /**
   * @brief 处理消息发送重试
   */
  boost::asio::awaitable<void> process_message_retries();

  /**
   * @brief 处理媒体下载重试
   */
  boost::asio::awaitable<void> process_media_download_retries();

  /**
   * @brief 计算下次重试时间（指数退避）
   * @param retry_count 当前重试次数
   * @param base_interval_seconds 基础间隔秒数
   * @return 下次重试时间点
   */
  std::chrono::system_clock::time_point calculate_next_retry_time(
      int retry_count, int base_interval_seconds) const;

  /**
   * @brief 序列化消息为JSON字符串
   * @param message 消息对象
   * @return JSON字符串
   */
  std::string serialize_message(const obcx::common::Message &message) const;

  /**
   * @brief 反序列化JSON字符串为消息对象
   * @param json_string JSON字符串
   * @return 消息对象
   */
  std::optional<obcx::common::Message> deserialize_message(
      const std::string &json_string) const;

  /**
   * @brief 启动重试队列处理定时器
   */
  void schedule_retry_check();
};

} // namespace bridge