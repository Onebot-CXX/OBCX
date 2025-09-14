#pragma once

#include "../interface/IBot.hpp"

#include "telegram/adapter/ProtocolAdapter.hpp"
#include <boost/asio/awaitable.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

namespace obcx::core {

/**
 * @brief Telegram媒体文件信息结构体
 */
struct MediaFileInfo {
  std::string file_id;        // Telegram文件ID（用于下载）
  std::string file_unique_id; // Telegram文件唯一ID（用于去重）
  std::string file_type;      // 文件类型: photo, video, audio, voice, document,
                              // sticker, animation, video_note
  std::optional<int64_t> file_size;     // 文件大小（字节）
  std::optional<std::string> mime_type; // MIME类型
  std::optional<std::string> file_name; // 文件名（仅document类型）
};

/**
 * @brief TelegramBot 类，继承自 Bot 基类，实现 Telegram 机器人功能
 */
class TGBot : public IBot {
public:
  TGBot(adapter::telegram::ProtocolAdapter adapter);
  ~TGBot() override;

  /**
   * @brief 通过指定的连接类型连接到 Telegram Bot API
   * @param type 连接类型
   * @param config 连接配置
   */
  void connect(network::ConnectionManagerFactory::ConnectionType type,
               const common::ConnectionConfig &config) override;

  /**
   * @brief 通过正向 WebSocket 连接到 Telegram Bot API（兼容方法）
   */
  void connect_ws(std::string_view host, uint16_t port,
                  std::string_view access_token = "") override;

  /**
   * @brief 通过 HTTP 连接到 Telegram Bot API
   * @param host 主机地址
   * @param port 端口
   * @param access_token 访问令牌
   */
  void connect_http(std::string_view host, uint16_t port,
                    std::string_view access_token = "") override;

  /**
   * @brief 启动 Bot 并运行事件循环 (此函数会阻塞)
   */
  void run() override;

  /**
   * @brief 停止 Bot 的事件循环
   */
  void stop() override;

  /**
   * @brief 发送错误消息（如果启用了错误处理器）
   * @param target_id 目标ID（用户ID或群ID）
   * @param message 错误消息
   * @param is_group 是否为群消息
   */
  void error_notify(std::string_view target_id, std::string_view message,
                    bool is_group = false) override;

  // --- 用户 API ---
  // 提供两个版本：异步(fire-and-forget)和同步(等待响应)

  // 标准版本 - 等待响应
  asio::awaitable<std::string> send_private_message(
      std::string_view user_id, const common::Message &message) override;

  asio::awaitable<std::string> send_group_message(
      std::string_view group_id, const common::Message &message) override;

  /**
   * @brief 发送消息到特定的forum topic
   * @param group_id 群组ID
   * @param topic_id 话题ID
   * @param message 消息内容
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> send_topic_message(
      std::string_view group_id, int64_t topic_id,
      const common::Message &message);

  /**
   * @brief 发送照片到群组
   * @param group_id 群组ID
   * @param photo_data 照片数据（file_id或URL）
   * @param caption 照片描述（可选）
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> send_group_photo(std::string_view group_id,
                                                std::string_view photo_data,
                                                std::string_view caption = "");

  // --- 消息管理 API ---

  /**
   * @brief 撤回消息
   * @param message_id 要撤回的消息ID
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> delete_message(
      std::string_view message_id) override;

  /**
   * @brief 获取消息详情
   * @param message_id 要获取的消息ID
   * @return 消息详情的JSON响应
   */
  asio::awaitable<std::string> get_message(
      std::string_view message_id) override;

  // --- 好友管理 API ---

  /**
   * @brief 获取好友列表
   * @return 好友列表的JSON响应
   */
  asio::awaitable<std::string> get_friend_list() override;

  /**
   * @brief 获取陌生人信息
   * @param user_id 目标用户ID
   * @param no_cache 是否不使用缓存
   * @return 用户信息的JSON响应
   */
  asio::awaitable<std::string> get_stranger_info(
      std::string_view user_id, bool no_cache = false) override;

  // --- 群组管理 API ---

  /**
   * @brief 获取群列表
   * @return 群列表的JSON响应
   */
  asio::awaitable<std::string> get_group_list() override;

  /**
   * @brief 获取群信息
   * @param group_id 目标群ID
   * @param no_cache 是否不使用缓存
   * @return 群信息的JSON响应
   */
  asio::awaitable<std::string> get_group_info(std::string_view group_id,
                                              bool no_cache = false) override;

  /**
   * @brief 获取群成员列表
   * @param group_id 目标群ID
   * @return 群成员列表的JSON响应
   */
  asio::awaitable<std::string> get_group_member_list(
      std::string_view group_id) override;

  /**
   * @brief 获取群成员信息
   * @param group_id 目标群ID
   * @param user_id 目标用户ID
   * @param no_cache 是否不使用缓存
   * @return 群成员信息的JSON响应
   */
  asio::awaitable<std::string> get_group_member_info(
      std::string_view group_id, std::string_view user_id,
      bool no_cache = false) override;

  /**
   * @brief 群组踢人
   * @param group_id 目标群ID
   * @param user_id 要踢的用户ID
   * @param reject_add_request 是否拒绝此人的加群请求
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_kick(
      std::string_view group_id, std::string_view user_id,
      bool reject_add_request = false) override;

  /**
   * @brief 群组单人禁言
   * @param group_id 目标群ID
   * @param user_id 要禁言的用户ID
   * @param duration 禁言时长，单位秒，0表示取消禁言
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_ban(std::string_view group_id,
                                             std::string_view user_id,
                                             int32_t duration = 1800) override;

  /**
   * @brief 群组全员禁言
   * @param group_id 目标群ID
   * @param enable 是否开启全员禁言
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_whole_ban(std::string_view group_id,
                                                   bool enable = true) override;

  /**
   * @brief 设置群名片（群备注）
   * @param group_id 目标群ID
   * @param user_id 目标用户ID
   * @param card 新的群名片
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_card(std::string_view group_id,
                                              std::string_view user_id,
                                              std::string_view card) override;

  /**
   * @brief 退出群组
   * @param group_id 目标群ID
   * @param is_dismiss 是否解散群组（仅群主可用）
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_leave(
      std::string_view group_id, bool is_dismiss = false) override;

  /**
   * @brief 设置群名
   * @param group_id 目标群ID
   * @param group_name 新群名
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_name(
      std::string_view group_id, std::string_view group_name) override;

  /**
   * @brief 设置群管理员
   * @param group_id 目标群ID
   * @param user_id 目标用户ID
   * @param enable 是否设置为管理员
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_admin(std::string_view group_id,
                                               std::string_view user_id,
                                               bool enable = true) override;

  /**
   * @brief 群组匿名用户禁言
   * @param group_id 目标群ID
   * @param anonymous 匿名用户对象
   * @param duration 禁言时长，单位秒，0表示取消禁言
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_anonymous_ban(
      std::string_view group_id, const std::string &anonymous,
      int32_t duration = 1800) override;

  /**
   * @brief 群组设置匿名
   * @param group_id 目标群ID
   * @param enable 是否允许匿名聊天
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_anonymous(std::string_view group_id,
                                                   bool enable = true) override;

  /**
   * @brief 设置群头像
   * @param group_id 目标群ID
   * @param file 图片文件路径或URL
   * @param cache 是否使用缓存
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_portrait(std::string_view group_id,
                                                  std::string_view file,
                                                  bool cache = true) override;

  /**
   * @brief 获取群荣誉信息
   * @param group_id 目标群ID
   * @param type 要获取的群荣誉类型
   * @return 群荣誉信息的JSON响应
   */
  asio::awaitable<std::string> get_group_honor_info(
      std::string_view group_id, std::string_view type) override;

  // --- 状态获取 API ---

  /**
   * @brief 获取登录号信息
   * @return 登录信息的JSON响应
   */
  asio::awaitable<std::string> get_login_info() override;

  /**
   * @brief 获取插件运行状态
   * @return 状态信息的JSON响应
   */
  asio::awaitable<std::string> get_status() override;

  /**
   * @brief 获取版本信息
   * @return 版本信息的JSON响应
   */
  asio::awaitable<std::string> get_version_info() override;

  // --- 资源管理 API ---

  /**
   * @brief 获取图片信息
   * @param file 图片文件名
   * @return 图片信息的JSON响应
   */
  asio::awaitable<std::string> get_image(std::string_view file) override;

  /**
   * @brief 获取语音信息
   * @param file 语音文件名
   * @param out_format 输出格式
   * @return 语音信息的JSON响应
   */
  asio::awaitable<std::string> get_record(
      std::string_view file, std::string_view out_format = "mp3") override;

  // --- 能力检查 API ---

  /**
   * @brief 检查是否可以发送图片
   * @return 检查结果的JSON响应
   */
  asio::awaitable<std::string> can_send_image() override;

  /**
   * @brief 检查是否可以发送语音
   * @return 检查结果的JSON响应
   */
  asio::awaitable<std::string> can_send_record() override;

  // --- Telegram相关接口凭证 API ---

  /**
   * @brief 获取Cookies
   * @param domain 目标域名（可选）
   * @return Cookies的JSON响应
   */
  asio::awaitable<std::string> get_cookies(
      std::string_view domain = "") override;

  /**
   * @brief 获取CSRF Token
   * @return CSRF Token的JSON响应
   */
  asio::awaitable<std::string> get_csrf_token() override;

  /**
   * @brief 获取Telegram相关接口凭证
   * @param domain 目标域名（可选）
   * @return 凭证信息的JSON响应
   */
  asio::awaitable<std::string> get_credentials(
      std::string_view domain = "") override;

  // --- 请求处理 API ---

  /**
   * @brief 处理加好友请求
   * @param flag 请求flag
   * @param approve 是否同意请求
   * @param remark 添加后的好友备注（仅同意时有效）
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_friend_add_request(
      std::string_view flag, bool approve = true,
      std::string_view remark = "") override;

  /**
   * @brief 处理加群请求/邀请
   * @param flag 请求flag
   * @param sub_type 请求类型（add/invite）
   * @param approve 是否同意请求
   * @param reason 拒绝理由（仅拒绝时有效）
   * @return 操作结果的JSON响应
   */
  asio::awaitable<std::string> set_group_add_request(
      std::string_view flag, std::string_view sub_type, bool approve = true,
      std::string_view reason = "") override;

  /**
   * @brief 检查是否已连接到Telegram Bot API
   * @return 连接状态
   */
  [[nodiscard]] auto is_connected() const -> bool override;

  auto get_task_scheduler() -> TaskScheduler & override;

  // --- 媒体文件处理 API ---

  /**
   * @brief 从Telegram消息数据中提取所有媒体文件信息
   * @param message_data Telegram消息的JSON数据
   * @return 媒体文件信息列表
   */
  static auto extract_media_files(const nlohmann::json &message_data)
      -> std::vector<MediaFileInfo>;

  /**
   * @brief 获取单个媒体文件的下载链接
   * @param media_info 媒体文件信息
   * @return 下载链接的awaitable，失败时返回nullopt
   */
  auto get_media_download_url(const MediaFileInfo &media_info)
      -> asio::awaitable<std::optional<std::string>>;

  /**
   * @brief 批量获取多个媒体文件的下载链接
   * @param media_list 媒体文件信息列表
   * @return 下载链接列表的awaitable，失败的项目为nullopt
   */
  auto get_media_download_urls(const std::vector<MediaFileInfo> &media_list)
      -> asio::awaitable<std::vector<std::optional<std::string>>>;

  /**
   * @brief 获取连接管理器实例（用于媒体下载）
   * @return 连接管理器指针，可能为nullptr
   */
  auto get_connection_manager() const -> network::IConnectionManager *;

private:
  /**
   * @brief 轮询 Telegram 更新
   * @return 可等待的协程对象
   */
  asio::awaitable<void> poll_updates();

  /**
   * @brief 获取 Telegram 更新
   * @param offset 更新偏移量
   * @param limit 更新数量限制
   * @return 更新的JSON响应
   */
  asio::awaitable<std::string> get_updates(int offset = 0, int limit = 100);

  /**
   * @brief 生成唯一的echo ID
   * @return 唯一ID字符串
   */
  uint64_t generate_echo_id();

  /**
   * @brief 检查连接管理器是否已初始化
   * @throws std::runtime_error 如果未初始化
   */
  void ensure_connection_manager() const;

  auto get_telegram_adapter() const -> adapter::telegram::ProtocolAdapter &;
};

} // namespace obcx::core