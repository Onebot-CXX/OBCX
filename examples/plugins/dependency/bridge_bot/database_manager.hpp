#pragma once

#include "common/message_type.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace obcx::storage {

/**
 * @brief 用户信息结构
 */
struct UserInfo {
  std::string platform;   // 'qq' 或 'telegram'
  std::string user_id;    // 平台用户ID
  std::string group_id;   // 群组ID（用于群组特定的昵称，私聊时为空）
  std::string username;   // 用户名
  std::string nickname;   // 昵称/显示名（群组特定）
  std::string title;      // 群头衔/特殊称号（主要用于QQ）
  std::string first_name; // 名字（主要用于Telegram）
  std::string last_name;  // 姓氏（主要用于Telegram）
  std::chrono::system_clock::time_point last_updated;
};

/**
 * @brief 消息信息结构
 */
struct MessageInfo {
  std::string platform;     // 'qq' 或 'telegram'
  std::string message_id;   // 平台原生消息ID
  std::string group_id;     // 群组ID
  std::string user_id;      // 用户ID
  std::string content;      // 消息内容
  std::string raw_message;  // 原始消息格式（JSON）
  std::string message_type; // 消息类型
  std::chrono::system_clock::time_point timestamp;
  std::optional<std::string> reply_to_message_id;   // 回复的消息ID
  std::optional<std::string> forwarded_to_platform; // 转发到的平台
  std::optional<std::string> forwarded_message_id;  // 转发后的消息ID
  std::chrono::system_clock::time_point created_at;
};

/**
 * @brief 消息ID映射结构
 */
struct MessageMapping {
  std::string source_platform;
  std::string source_message_id;
  std::string target_platform;
  std::string target_message_id;
  std::chrono::system_clock::time_point created_at;
};

/**
 * @brief 表情包缓存信息结构
 */
struct StickerCacheInfo {
  std::string platform;                     // 平台名称 ('telegram', 'qq')
  std::string sticker_id;                   // 原始sticker ID
  std::string sticker_hash;                 // sticker ID的哈希值
  std::optional<std::string> original_name; // 原始文件名（如果有）
  std::string file_type; // 文件类型: 'static', 'animated', 'video'
  std::optional<std::string>
      mime_type; // MIME类型: 'image/webp', 'video/webm', 'application/tgs'
  std::string original_file_path; // 原始文件本地路径
  std::optional<std::string>
      converted_file_path;          // 转换后文件路径（如果需要转换）
  std::string container_path;       // 容器内路径（给LLOneBot使用）
  std::optional<int64_t> file_size; // 文件大小
  std::string conversion_status;    // 转换状态: 'none', 'success', 'failed'
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_used_at;
};

/**
 * @brief QQ表情包到Telegram file_id的映射缓存
 */
struct QQStickerMapping {
  std::string qq_sticker_hash;  // QQ表情包的唯一hash
  std::string telegram_file_id; // 对应的Telegram file_id
  std::string file_type;        // 文件类型: 'photo', 'animation'
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_used_at;
  std::optional<bool> is_gif;              // 是否为GIF格式，null表示未检测
  std::optional<std::string> content_type; // 真实的MIME类型
  std::optional<std::chrono::system_clock::time_point>
      last_checked_at; // 最后检查时间
};

/**
 * @brief 消息重试队列信息结构
 */
struct MessageRetryInfo {
  std::string source_platform;   // 源平台
  std::string target_platform;   // 目标平台
  std::string source_message_id; // 源消息ID
  std::string message_content;   // 消息内容 (JSON格式)
  std::string group_id;          // 目标群组ID
  std::string source_group_id;   // 源群组ID (新增)
  int64_t target_topic_id;       // 目标Topic ID，-1表示普通群组模式 (新增)
  int retry_count;               // 当前重试次数
  int max_retry_count;           // 最大重试次数
  std::string failure_reason;    // 失败原因
  std::string retry_type;        // 重试类型: 'message_send'
  std::chrono::system_clock::time_point next_retry_at;   // 下次重试时间
  std::chrono::system_clock::time_point created_at;      // 创建时间
  std::chrono::system_clock::time_point last_attempt_at; // 上次尝试时间
};

/**
 * @brief 媒体下载重试队列信息结构
 */
struct MediaDownloadRetryInfo {
  std::string platform;       // 平台 ('telegram', 'qq')
  std::string file_id;        // 文件ID
  std::string file_type;      // 文件类型
  std::string download_url;   // 下载URL
  std::string local_path;     // 预期本地路径
  int retry_count;            // 当前重试次数
  int max_retry_count;        // 最大重试次数
  std::string failure_reason; // 失败原因
  bool use_proxy;             // 是否使用代理
  std::chrono::system_clock::time_point next_retry_at;   // 下次重试时间
  std::chrono::system_clock::time_point created_at;      // 创建时间
  std::chrono::system_clock::time_point last_attempt_at; // 上次尝试时间
};

/**
 * @brief 平台心跳信息结构
 */
struct PlatformHeartbeatInfo {
  std::string platform; // 平台名称 ('qq', 'telegram')
  std::chrono::system_clock::time_point last_heartbeat_at; // 最后心跳时间
  std::chrono::system_clock::time_point updated_at;        // 更新时间
};

/**
 * @brief 数据库管理器类
 *
 * 提供消息持久化、用户信息管理、消息ID映射等功能
 */
class DatabaseManager {
public:
  /**
   * @brief 构造函数
   * @param db_path 数据库文件路径
   */
  explicit DatabaseManager(const std::string &db_path);

  /**
   * @brief 析构函数
   */
  ~DatabaseManager();

  // 禁用拷贝构造和赋值
  DatabaseManager(const DatabaseManager &) = delete;
  DatabaseManager &operator=(const DatabaseManager &) = delete;

  /**
   * @brief 初始化数据库（创建表结构）
   * @return 成功返回true，失败返回false
   */
  bool initialize();

  // === 消息相关操作 ===

  /**
   * @brief 保存消息到数据库
   * @param message_info 消息信息
   * @return 成功返回true，失败返回false
   */
  bool save_message(const MessageInfo &message_info);

  /**
   * @brief 根据平台和消息ID查询消息
   * @param platform 平台名称
   * @param message_id 消息ID
   * @return 消息信息，如果未找到返回nullopt
   */
  std::optional<MessageInfo> get_message(const std::string &platform,
                                         const std::string &message_id);

  /**
   * @brief 更新消息的转发信息
   * @param platform 原平台
   * @param message_id 原消息ID
   * @param forwarded_to_platform 转发到的平台
   * @param forwarded_message_id 转发后的消息ID
   * @return 成功返回true，失败返回false
   */
  bool update_message_forwarding(const std::string &platform,
                                 const std::string &message_id,
                                 const std::string &forwarded_to_platform,
                                 const std::string &forwarded_message_id);

  // === 用户相关操作 ===

  /**
   * @brief 保存或更新用户信息
   * @param user_info 用户信息
   * @return 成功返回true，失败返回false
   */
  bool save_or_update_user(const UserInfo &user_info);

  /**
   * @brief 根据平台和用户ID查询用户信息
   * @param platform 平台名称
   * @param user_id 用户ID
   * @param group_id 群组ID（可选，用于查询群组特定的昵称）
   * @return 用户信息，如果未找到返回nullopt
   */
  std::optional<UserInfo> get_user(const std::string &platform,
                                   const std::string &user_id,
                                   const std::string &group_id = "");

  /**
   * @brief 获取用户的显示名称（优先显示昵称，其次用户名，最后用户ID）
   * @param platform 平台名称
   * @param user_id 用户ID
   * @param group_id 群组ID（可选，用于获取群组特定的昵称）
   * @return 用户显示名称
   */
  std::string get_user_display_name(const std::string &platform,
                                    const std::string &user_id,
                                    const std::string &group_id = "");

  /**
   * @brief 检查用户是否需要更新信息（用于主动获取QQ用户信息）
   * @param platform 平台名称
   * @param user_id 用户ID
   * @param group_id 群组ID（可选，用于检查群组特定的用户信息）
   * @return 如果用户不存在或信息不完整返回true
   */
  bool should_fetch_user_info(const std::string &platform,
                              const std::string &user_id,
                              const std::string &group_id = "");

  // === 消息ID映射相关操作 ===

  /**
   * @brief 添加消息ID映射
   * @param mapping 映射信息
   * @return 成功返回true，失败返回false
   */
  bool add_message_mapping(const MessageMapping &mapping);

  /**
   * @brief 根据源消息查找目标消息ID
   * @param source_platform 源平台
   * @param source_message_id 源消息ID
   * @param target_platform 目标平台
   * @return 目标消息ID，如果未找到返回nullopt
   */
  std::optional<std::string> get_target_message_id(
      const std::string &source_platform, const std::string &source_message_id,
      const std::string &target_platform);

  /**
   * @brief 反向查找：根据目标消息ID查找源消息ID
   * @param target_platform 目标平台
   * @param target_message_id 目标消息ID
   * @param source_platform 源平台
   * @return 源消息ID，如果未找到返回nullopt
   */
  std::optional<std::string> get_source_message_id(
      const std::string &target_platform, const std::string &target_message_id,
      const std::string &source_platform);

  /**
   * @brief 删除消息映射
   * @param source_platform 源平台
   * @param source_message_id 源消息ID
   * @param target_platform 目标平台
   * @return 成功返回true，失败返回false
   */
  bool delete_message_mapping(const std::string &source_platform,
                              const std::string &source_message_id,
                              const std::string &target_platform);

  /**
   * @brief 更新消息映射的目标消息ID
   * @param source_platform 源平台
   * @param source_message_id 源消息ID
   * @param target_platform 目标平台
   * @param new_target_message_id 新的目标消息ID
   * @return 成功返回true，失败返回false
   */
  bool update_message_mapping(const std::string &source_platform,
                              const std::string &source_message_id,
                              const std::string &target_platform,
                              const std::string &new_target_message_id);

  // === 辅助功能 ===

  /**
   * @brief 从MessageEvent提取并保存消息信息
   * @param event 消息事件
   * @param platform 平台名称
   * @return 成功返回true，失败返回false
   */
  bool save_message_from_event(const common::MessageEvent &event,
                               const std::string &platform);

  /**
   * @brief 从消息事件中提取用户信息并保存
   * @param event 消息事件
   * @param platform 平台名称
   * @return 成功返回true，失败返回false
   */
  bool save_user_from_event(const common::MessageEvent &event,
                            const std::string &platform);

  // === 表情包缓存相关操作 ===

  /**
   * @brief 保存表情包缓存信息
   * @param cache_info 缓存信息
   * @return 成功返回true，失败返回false
   */
  bool save_sticker_cache(const StickerCacheInfo &cache_info);

  /**
   * @brief 根据平台和哈希查询表情包缓存
   * @param platform 平台名称
   * @param sticker_hash 表情包哈希值
   * @return 缓存信息，如果未找到返回nullopt
   */
  std::optional<StickerCacheInfo> get_sticker_cache(
      const std::string &platform, const std::string &sticker_hash);

  /**
   * @brief 更新表情包缓存的使用时间
   * @param platform 平台名称
   * @param sticker_hash 表情包哈希值
   * @return 成功返回true，失败返回false
   */
  bool update_sticker_last_used(const std::string &platform,
                                const std::string &sticker_hash);

  /**
   * @brief 更新表情包缓存的转换状态和路径
   * @param platform 平台名称
   * @param sticker_hash 表情包哈希值
   * @param conversion_status 转换状态
   * @param converted_file_path 转换后文件路径（可选）
   * @return 成功返回true，失败返回false
   */
  bool update_sticker_conversion(
      const std::string &platform, const std::string &sticker_hash,
      const std::string &conversion_status,
      const std::optional<std::string> &converted_file_path = std::nullopt);

  // === QQ表情包映射相关操作 ===

  /**
   * @brief 保存QQ表情包到Telegram file_id的映射
   * @param mapping 映射信息
   * @return 成功返回true，失败返回false
   */
  bool save_qq_sticker_mapping(const QQStickerMapping &mapping);

  /**
   * @brief 根据QQ表情包hash查询Telegram file_id
   * @param qq_sticker_hash QQ表情包哈希值
   * @return 映射信息，如果未找到返回nullopt
   */
  std::optional<QQStickerMapping> get_qq_sticker_mapping(
      const std::string &qq_sticker_hash);

  /**
   * @brief 更新QQ表情包映射的使用时间
   * @param qq_sticker_hash QQ表情包哈希值
   * @return 成功返回true，失败返回false
   */
  bool update_qq_sticker_last_used(const std::string &qq_sticker_hash);

  /**
   * @brief 计算字符串的哈希值（用于QQ表情包ID）
   * @param input 输入字符串
   * @return 哈希值的十六进制字符串
   */
  static std::string calculate_hash(const std::string &input);

  /**
   * @brief 清理过期的图片类型缓存记录
   * @param max_age_days 最大保留天数，超过此天数的记录将被删除
   * @return 删除的记录数量
   */
  int cleanup_old_image_type_cache(int max_age_days = 30);

  /**
   * @brief 获取图片类型缓存统计信息
   * @return 包含缓存统计的字符串
   */
  std::string get_cache_statistics();

  // === 消息重试队列相关操作 ===

  /**
   * @brief 添加消息到重试队列
   * @param retry_info 重试信息
   * @return 成功返回true，失败返回false
   */
  bool add_message_retry(const MessageRetryInfo &retry_info);

  /**
   * @brief 获取需要重试的消息列表
   * @param limit 返回记录的最大数量
   * @return 需要重试的消息列表
   */
  std::vector<MessageRetryInfo> get_pending_message_retries(int limit = 100);

  /**
   * @brief 更新消息重试信息
   * @param source_platform 源平台
   * @param source_message_id 源消息ID
   * @param target_platform 目标平台
   * @param retry_count 新的重试次数
   * @param next_retry_at 下次重试时间
   * @param failure_reason 失败原因
   * @return 成功返回true，失败返回false
   */
  bool update_message_retry(
      const std::string &source_platform, const std::string &source_message_id,
      const std::string &target_platform, int retry_count,
      const std::chrono::system_clock::time_point &next_retry_at,
      const std::string &failure_reason);

  /**
   * @brief 删除消息重试记录（成功或达到最大重试次数）
   * @param source_platform 源平台
   * @param source_message_id 源消息ID
   * @param target_platform 目标平台
   * @return 成功返回true，失败返回false
   */
  bool remove_message_retry(const std::string &source_platform,
                            const std::string &source_message_id,
                            const std::string &target_platform);

  // === 媒体下载重试队列相关操作 ===

  /**
   * @brief 添加媒体下载到重试队列
   * @param retry_info 重试信息
   * @return 成功返回true，失败返回false
   */
  bool add_media_download_retry(const MediaDownloadRetryInfo &retry_info);

  /**
   * @brief 获取需要重试的媒体下载列表
   * @param limit 返回记录的最大数量
   * @return 需要重试的媒体下载列表
   */
  std::vector<MediaDownloadRetryInfo> get_pending_media_download_retries(
      int limit = 50);

  /**
   * @brief 更新媒体下载重试信息
   * @param platform 平台
   * @param file_id 文件ID
   * @param retry_count 新的重试次数
   * @param next_retry_at 下次重试时间
   * @param failure_reason 失败原因
   * @param use_proxy 是否使用代理
   * @return 成功返回true，失败返回false
   */
  bool update_media_download_retry(
      const std::string &platform, const std::string &file_id, int retry_count,
      const std::chrono::system_clock::time_point &next_retry_at,
      const std::string &failure_reason, bool use_proxy);

  /**
   * @brief 删除媒体下载重试记录（成功或达到最大重试次数）
   * @param platform 平台
   * @param file_id 文件ID
   * @return 成功返回true，失败返回false
   */
  bool remove_media_download_retry(const std::string &platform,
                                   const std::string &file_id);

  // 平台心跳管理
  /**
   * @brief 更新平台心跳时间
   * @param platform 平台名称 ('qq', 'telegram')
   * @param heartbeat_time 心跳时间
   * @return 成功返回true，失败返回false
   */
  bool update_platform_heartbeat(
      const std::string &platform,
      const std::chrono::system_clock::time_point &heartbeat_time);

  /**
   * @brief 获取平台最后心跳时间
   * @param platform 平台名称 ('qq', 'telegram')
   * @return 心跳信息，如果不存在返回nullopt
   */
  std::optional<PlatformHeartbeatInfo> get_platform_heartbeat(
      const std::string &platform);

private:
  std::string db_path_;
  sqlite3 *db_;
  std::mutex db_mutex_;

  /**
   * @brief 创建数据库表
   * @return 成功返回true，失败返回false
   */
  bool create_tables();

  /**
   * @brief 执行SQL语句
   * @param sql SQL语句
   * @return 成功返回true，失败返回false
   */
  bool execute_sql(const std::string &sql);

  /**
   * @brief 时间戳转换辅助函数
   */
  static int64_t time_point_to_timestamp(
      const std::chrono::system_clock::time_point &tp);
  static std::chrono::system_clock::time_point timestamp_to_time_point(
      int64_t timestamp);
};

} // namespace obcx::storage