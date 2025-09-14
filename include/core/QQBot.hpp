#pragma once

#include "../interface/IBot.hpp"

#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>

namespace obcx::core {

/**
 * @brief QQBot 类，继承自 Bot 基类，实现 QQ 机器人功能
 */
class QQBot : public IBot {
public:
  QQBot(adapter::onebot11::ProtocolAdapter adapter);
  ~QQBot() override;

  /**
   * @brief 通过指定的连接类型连接到 OneBot v11 实现
   * @param type 连接类型（WebSocket或HTTP）
   * @param config 连接配置
   */
  void connect(network::ConnectionManagerFactory::ConnectionType type,
               const common::ConnectionConfig &config) override;

  /**
   * @brief 通过正向 WebSocket 连接到 OneBot v11 实现（兼容方法）
   */
  void connect_ws(std::string_view host, uint16_t port,
                  std::string_view access_token = "") override;

  /**
   * @brief 通过 HTTP 连接到 OneBot v11 实现
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

  // 标准版本 - 等待响应，符合OneBot 11规范
  asio::awaitable<std::string> send_private_message(
      std::string_view user_id, const common::Message &message) override;

  asio::awaitable<std::string> send_group_message(
      std::string_view group_id, const common::Message &message) override;

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

  /**
   * @brief 获取合并转发内容
   * @param forward_id 转发消息ID
   * @return 合并转发内容的JSON响应
   */
  asio::awaitable<std::string> get_forward_msg(std::string_view forward_id);

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
   * @param type 要获取的群荣誉类型（talkative, performer, legend,
   * strong_newbie, emotion）
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

  /**
   * @brief 获取群文件下载URL
   * @param group_id 群组ID
   * @param file_id 文件ID
   * @return 包含下载URL的JSON响应
   */
  asio::awaitable<std::string> get_group_file_url(std::string_view group_id,
                                                  std::string_view file_id);

  /**
   * @brief 获取私聊文件下载URL
   * @param user_id 用户ID
   * @param file_id 文件ID
   * @return 包含下载URL的JSON响应
   */
  asio::awaitable<std::string> get_private_file_url(std::string_view user_id,
                                                    std::string_view file_id);

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

  // --- QQ相关接口凭证 API ---

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
   * @brief 获取QQ相关接口凭证
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
   * @brief 检查是否已连接到OneBot实现
   * @return 连接状态
   */
  auto is_connected() const -> bool override;

  /**
   * @brief 获取任务调度器的引用，用于执行重负载任务
   * @return TaskScheduler& 任务调度器引用
   */
  auto get_task_scheduler() -> TaskScheduler & override {
    return *task_scheduler_;
  }

private:
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

  auto get_onebot_adapter() const -> adapter::onebot11::ProtocolAdapter &;
};

} // namespace obcx::core
