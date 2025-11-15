#pragma once
#include "common/message_type.hpp"
#include "core/event_dispatcher.hpp"
#include "core/task_scheduler.hpp"
#include "interfaces/connection_manager.hpp"

#include "interfaces/protocol_adapter.hpp"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>
#include <string>

namespace obcx::core {

/**
 * @brief Bot 基类，定义所有 Bot 实现的公共接口
 */
class IBot {
public:
  explicit IBot(std::unique_ptr<adapter::BaseProtocolAdapter> adapter);
  virtual ~IBot();

  // 禁止拷贝和移动
  IBot(const IBot &) = delete;
  IBot &operator=(const IBot &) = delete;
  IBot(IBot &&) = delete;
  IBot &operator=(IBot &&) = delete;

  /**
   * @brief 注册事件处理器的语法糖 (新版本，支持Bot引用)
   * @tparam EventType 事件类型
   * @param handler 协程事件处理器，接受Bot引用和事件参数
   */
  template <typename EventType>
  void on_event(
      std::function<asio::awaitable<void>(IBot &, EventType)> handler) {
    dispatcher_->on<EventType>(std::move(handler));
  }

  /**
   * @brief 通过指定的连接类型连接到实现
   * @param type 连接类型
   * @param config 连接配置
   */
  virtual void connect(network::ConnectionManagerFactory::ConnectionType type,
                       const common::ConnectionConfig &config) = 0;

  /**
   * @brief 通过正向 WebSocket 连接到实现（兼容方法）
   */
  virtual void connect_ws(std::string_view host, uint16_t port,
                          std::string_view access_token = "") = 0;

  /**
   * @brief 通过 HTTP 连接到实现
   * @param host 主机地址
   * @param port 端口
   * @param access_token 访问令牌
   */
  virtual void connect_http(std::string_view host, uint16_t port,
                            std::string_view access_token = "") = 0;

  /**
   * @brief 启动 Bot 并运行事件循环 (此函数会阻塞)
   */
  virtual void run() = 0;

  /**
   * @brief 停止 Bot 的事件循环
   */
  virtual void stop() = 0;

  /**
   * @brief 发送错误消息（如果启用了错误处理器）
   * @param target_id 目标ID（用户ID或群ID）
   * @param message 错误消息
   * @param is_group 是否为群消息
   */
  virtual void error_notify(std::string_view target_id,
                            std::string_view message,
                            bool is_group = false) = 0;

  // --- 用户 API ---
  // 提供两个版本：异步(fire-and-forget)和同步(等待响应)

  // 标准版本 - 等待响应，符合OneBot 11规范
  virtual asio::awaitable<std::string> send_private_message(
      std::string_view user_id, const common::Message &message) = 0;

  virtual asio::awaitable<std::string> send_group_message(
      std::string_view group_id, const common::Message &message) = 0;

  // --- 消息管理 API ---

  /**
   * @brief 撤回消息
   * @param message_id 要撤回的消息ID
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> delete_message(
      std::string_view message_id) = 0;

  /**
   * @brief 获取消息详情
   * @param message_id 要获取的消息ID
   * @return 消息详情的JSON响应
   */
  virtual asio::awaitable<std::string> get_message(
      std::string_view message_id) = 0;

  // --- 好友管理 API ---

  /**
   * @brief 获取好友列表
   * @return 好友列表的JSON响应
   */
  virtual asio::awaitable<std::string> get_friend_list() = 0;

  /**
   * @brief 获取陌生人信息
   * @param user_id 目标用户ID
   * @param no_cache 是否不使用缓存
   * @return 用户信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_stranger_info(
      std::string_view user_id, bool no_cache = false) = 0;

  // --- 群组管理 API ---

  /**
   * @brief 获取群列表
   * @return 群列表的JSON响应
   */
  virtual asio::awaitable<std::string> get_group_list() = 0;

  /**
   * @brief 获取群信息
   * @param group_id 目标群ID
   * @param no_cache 是否不使用缓存
   * @return 群信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_group_info(
      std::string_view group_id, bool no_cache = false) = 0;

  /**
   * @brief 获取群成员列表
   * @param group_id 目标群ID
   * @return 群成员列表的JSON响应
   */
  virtual asio::awaitable<std::string> get_group_member_list(
      std::string_view group_id) = 0;

  /**
   * @brief 获取群成员信息
   * @param group_id 目标群ID
   * @param user_id 目标用户ID
   * @param no_cache 是否不使用缓存
   * @return 群成员信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_group_member_info(
      std::string_view group_id, std::string_view user_id,
      bool no_cache = false) = 0;

  /**
   * @brief 群组踢人
   * @param group_id 目标群ID
   * @param user_id 要踢的用户ID
   * @param reject_add_request 是否拒绝此人的加群请求
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_kick(
      std::string_view group_id, std::string_view user_id,
      bool reject_add_request = false) = 0;

  /**
   * @brief 群组单人禁言
   * @param group_id 目标群ID
   * @param user_id 要禁言的用户ID
   * @param duration 禁言时长，单位秒，0表示取消禁言
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_ban(
      std::string_view group_id, std::string_view user_id,
      int32_t duration = 1800) = 0;

  /**
   * @brief 群组全员禁言
   * @param group_id 目标群ID
   * @param enable 是否开启全员禁言
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_whole_ban(
      std::string_view group_id, bool enable = true) = 0;

  /**
   * @brief 设置群名片（群备注）
   * @param group_id 目标群ID
   * @param user_id 目标用户ID
   * @param card 新的群名片
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_card(
      std::string_view group_id, std::string_view user_id,
      std::string_view card) = 0;

  /**
   * @brief 退出群组
   * @param group_id 目标群ID
   * @param is_dismiss 是否解散群组（仅群主可用）
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_leave(
      std::string_view group_id, bool is_dismiss = false) = 0;

  /**
   * @brief 设置群名
   * @param group_id 目标群ID
   * @param group_name 新群名
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_name(
      std::string_view group_id, std::string_view group_name) = 0;

  /**
   * @brief 设置群管理员
   * @param group_id 目标群ID
   * @param user_id 目标用户ID
   * @param enable 是否设置为管理员
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_admin(
      std::string_view group_id, std::string_view user_id,
      bool enable = true) = 0;

  /**
   * @brief 群组匿名用户禁言
   * @param group_id 目标群ID
   * @param anonymous 匿名用户对象
   * @param duration 禁言时长，单位秒，0表示取消禁言
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_anonymous_ban(
      std::string_view group_id, const std::string &anonymous,
      int32_t duration = 1800) = 0;

  /**
   * @brief 群组设置匿名
   * @param group_id 目标群ID
   * @param enable 是否允许匿名聊天
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_anonymous(
      std::string_view group_id, bool enable = true) = 0;

  /**
   * @brief 设置群头像
   * @param group_id 目标群ID
   * @param file 图片文件路径或URL
   * @param cache 是否使用缓存
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_portrait(
      std::string_view group_id, std::string_view file, bool cache = true) = 0;

  /**
   * @brief 获取群荣誉信息
   * @param group_id 目标群ID
   * @param type 要获取的群荣誉类型（talkative, performer, legend,
   * strong_newbie, emotion）
   * @return 群荣誉信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_group_honor_info(
      std::string_view group_id, std::string_view type) = 0;

  // --- 状态获取 API ---

  /**
   * @brief 获取登录号信息
   * @return 登录信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_login_info() = 0;

  /**
   * @brief 获取插件运行状态
   * @return 状态信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_status() = 0;

  /**
   * @brief 获取版本信息
   * @return 版本信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_version_info() = 0;

  // --- 资源管理 API ---

  /**
   * @brief 获取图片信息
   * @param file 图片文件名
   * @return 图片信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_image(std::string_view file) = 0;

  /**
   * @brief 获取语音信息
   * @param file 语音文件名
   * @param out_format 输出格式
   * @return 语音信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_record(
      std::string_view file, std::string_view out_format = "mp3") = 0;

  // --- 能力检查 API ---

  /**
   * @brief 检查是否可以发送图片
   * @return 检查结果的JSON响应
   */
  virtual asio::awaitable<std::string> can_send_image() = 0;

  /**
   * @brief 检查是否可以发送语音
   * @return 检查结果的JSON响应
   */
  virtual asio::awaitable<std::string> can_send_record() = 0;

  // --- QQ相关接口凭证 API ---

  /**
   * @brief 获取Cookies
   * @param domain 目标域名（可选）
   * @return Cookies的JSON响应
   */
  virtual asio::awaitable<std::string> get_cookies(
      std::string_view domain = "") = 0;

  /**
   * @brief 获取CSRF Token
   * @return CSRF Token的JSON响应
   */
  virtual asio::awaitable<std::string> get_csrf_token() = 0;

  /**
   * @brief 获取相关接口凭证
   * @param domain 目标域名（可选）
   * @return 凭证信息的JSON响应
   */
  virtual asio::awaitable<std::string> get_credentials(
      std::string_view domain = "") = 0;

  // --- 请求处理 API ---

  /**
   * @brief 处理加好友请求
   * @param flag 请求flag
   * @param approve 是否同意请求
   * @param remark 添加后的好友备注（仅同意时有效）
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_friend_add_request(
      std::string_view flag, bool approve = true,
      std::string_view remark = "") = 0;

  /**
   * @brief 处理加群请求/邀请
   * @param flag 请求flag
   * @param sub_type 请求类型（add/invite）
   * @param approve 是否同意请求
   * @param reason 拒绝理由（仅拒绝时有效）
   * @return 操作结果的JSON响应
   */
  virtual asio::awaitable<std::string> set_group_add_request(
      std::string_view flag, std::string_view sub_type, bool approve = true,
      std::string_view reason = "") = 0;

  /**
   * @brief 检查是否已连接到实现
   * @return 连接状态
   */
  virtual auto is_connected() const -> bool = 0;

  /**
   * @brief 获取任务调度器的引用，用于执行重负载任务
   * @return TaskScheduler& 任务调度器引用
   */
  virtual auto get_task_scheduler() -> TaskScheduler & = 0;

  /**
   * @brief 便捷方法：在线程池中执行重负载任务
   * @tparam Func 可调用对象类型
   * @param task 要执行的任务
   * @return asio::awaitable<T> 可等待的协程对象
   */
  template <typename Func> auto run_heavy_task(Func task) {
    return get_task_scheduler().run_heavy_task(std::move(task));
  }

protected:
  std::shared_ptr<asio::io_context> io_context_;
  std::unique_ptr<adapter::BaseProtocolAdapter> adapter_;
  std::unique_ptr<EventDispatcher> dispatcher_;
  std::unique_ptr<TaskScheduler> task_scheduler_;
  std::unique_ptr<network::IConnectionManager> connection_manager_;
  common::ConnectionConfig conection_config_;
};

} // namespace obcx::core