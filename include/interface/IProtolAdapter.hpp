#pragma once

#include "common/MessageType.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace obcx::adapter {

/**
 * @brief 抽象基类，定义了所有协议适配器必须实现的通用接口。
 *
 * 本类旨在提供一个统一的接口，使得上层应用可以与不同的即时通讯平台（如QQ、Telegram等）
 * 进行交互，而无需关心底层协议的具体实现细节。
 *
 * 接口设计遵循以下原则：
 * 1. 语义统一：尽管不同平台的API可能不同，但抽象接口应具有统一的语义。
 * 2. 参数抽象：接口参数应尽量抽象，避免直接暴露平台特定的数据结构。
 * 3. 功能覆盖：涵盖消息发送、用户管理、群组管理等核心功能。
 * 4. 可扩展性：为平台特有功能预留扩展点（通过dynamic_cast或特定子类接口）。
 */
class BaseProtocolAdapter {
public:
  BaseProtocolAdapter() = default;
  virtual ~BaseProtocolAdapter() = default;

  // 禁止拷贝和赋值
  BaseProtocolAdapter(const BaseProtocolAdapter &) = delete;
  BaseProtocolAdapter &operator=(const BaseProtocolAdapter &) = delete;
  BaseProtocolAdapter(BaseProtocolAdapter &&) = default;
  BaseProtocolAdapter &operator=(BaseProtocolAdapter &&) = default;

  // --- 事件解析 ---

  /**
   * @brief 解析从平台传入的原始事件数据。
   * @param json_str 原始JSON字符串。
   * @return 如果是有效事件，则返回转换后的内部 Event 对象；否则返回
   * std::nullopt。
   */
  virtual auto parse_event(std::string_view json_str)
      -> std::optional<common::Event> = 0;

  // --- 消息发送与管理 ---

  /**
   * @brief 发送消息。
   * @param target_id 目标ID（用户ID或群组ID）。
   * @param message 要发送的消息对象。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 如果 target_id 是用户ID，则调用 send_private_msg。
   * - 如果 target_id 是群组ID，则调用 send_group_msg。
   *
   * 对于Telegram:
   * - 调用 sendMessage 方法。
   */
  virtual auto serialize_send_message_request(
      std::string_view target_id, const common::Message &message,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 撤回消息。
   * @param chat_id 聊天ID（对于QQ，私聊消息可能不需要）。
   * @param message_id 要撤回的消息ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 delete_msg，仅需要 message_id。
   *
   * 对于Telegram:
   * - 调用 deleteMessage，需要 chat_id 和 message_id。
   */
  virtual auto serialize_delete_message_request(
      std::string_view chat_id, std::string_view message_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  // --- 用户与自身信息 ---

  /**
   * @brief 获取机器人自身的信息。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 get_login_info。
   *
   * 对于Telegram:
   * - 调用 getMe。
   */
  virtual auto serialize_get_self_info_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 获取指定用户的基本信息。
   * @param chat_id 聊天ID（对于QQ获取陌生人信息可能不需要）。
   * @param user_id 目标用户ID。
   * @param no_cache 是否不使用缓存（QQ特有）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 get_stranger_info。
   *
   * 对于Telegram:
   * - 调用 getChatMember 或 getChat (对于私聊)。
   */
  virtual auto serialize_get_user_info_request(
      std::string_view chat_id, std::string_view user_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  // --- 群组/聊天管理 ---

  /**
   * @brief 获取群组/频道的基本信息。
   * @param chat_id 目标群组/频道ID。
   * @param no_cache 是否不使用缓存（QQ特有）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 get_group_info。
   *
   * 对于Telegram:
   * - 调用 getChat。
   */
  virtual auto serialize_get_chat_info_request(
      std::string_view chat_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 获取特定群组成员的信息。
   * @param chat_id 目标群组ID。
   * @param user_id 目标用户ID。
   * @param no_cache 是否不使用缓存（QQ特有）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 get_group_member_info。
   *
   * 对于Telegram:
   * - 调用 getChatMember。
   */
  virtual auto serialize_get_chat_member_info_request(
      std::string_view chat_id, std::string_view user_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 获取群组/频道的管理员列表。
   * @param chat_id 目标群组/频道ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 get_group_member_list，然后在应用层过滤出管理员。
   *
   * 对于Telegram:
   * - 调用 getChatAdministrators。
   */
  virtual auto serialize_get_chat_admins_request(
      std::string_view chat_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 从群组中移除成员。
   * @param chat_id 目标群组ID。
   * @param user_id 要移除的用户ID。
   * @param reject_add_request 是否拒绝此人的加群请求（QQ特有）。
   * @param revoke_messages 是否删除该用户最近的消息（Telegram特有）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_kick。
   *
   * 对于Telegram:
   * - 调用 banChatMember (旧称 kickChatMember)。
   */
  virtual auto serialize_kick_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      bool reject_add_request = false, bool revoke_messages = false,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 禁言群组成员。
   * @param chat_id 目标群组ID。
   * @param user_id 要禁言的用户ID。
   * @param duration 禁言时长，单位秒，0表示取消禁言。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_ban。
   *
   * 对于Telegram:
   * - 调用 restrictChatMember。
   */
  virtual auto serialize_ban_chat_member_request(
      std::string_view chat_id, std::string_view user_id, int32_t duration,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 解除禁言。
   * @param chat_id 目标群组ID。
   * @param user_id 要解除禁言的用户ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_ban，duration 设置为 0。
   *
   * 对于Telegram:
   * - 调用 restrictChatMember，设置为默认权限。
   */
  virtual auto serialize_unban_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 开启/关闭全员禁言。
   * @param chat_id 目标群组ID。
   * @param enable 是否开启全员禁言。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_whole_ban。
   *
   * 对于Telegram:
   * - 调用 setChatPermissions。
   */
  virtual auto serialize_ban_all_members_request(
      std::string_view chat_id, bool enable = true,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 修改群组/频道的标题。
   * @param chat_id 目标群组/频道ID。
   * @param title 新的标题。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_name。
   *
   * 对于Telegram:
   * - 调用 setChatTitle。
   */
  virtual auto serialize_set_chat_title_request(
      std::string_view chat_id, std::string_view title,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 设置群头像。
   * @param chat_id 目标群组ID。
   * @param file 文件路径或ID。
   * @param cache 是否使用缓存（QQ特有）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_portrait。
   *
   * 对于Telegram:
   * - 调用 setChatPhoto。
   */
  virtual auto serialize_set_chat_photo_request(
      std::string_view chat_id, std::string_view file, bool cache,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 设置/取消群组成员的管理员权限。
   * @param chat_id 目标群组ID。
   * @param user_id 目标用户ID。
   * @param is_admin 是否设为管理员。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_admin。
   *
   * 对于Telegram:
   * - 调用 promoteChatMember。
   */
  virtual auto serialize_set_chat_admin_request(
      std::string_view chat_id, std::string_view user_id, bool is_admin = true,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  /**
   * @brief 机器人主动离开群组。
   * @param chat_id 目标群组ID。
   * @param is_dismiss 是否解散群组（仅群主可用，QQ特有）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_group_leave。
   *
   * 对于Telegram:
   * - 调用 leaveChat。
   */
  virtual auto serialize_leave_chat_request(
      std::string_view chat_id, bool is_dismiss = false,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  // --- 事件处理 ---

  /**
   * @brief 处理好友/加群请求。
   * @param request_event 请求事件对象。
   * @param approve 是否同意请求。
   * @param reason 拒绝理由（仅拒绝时有效）。
   * @param remark 添加后的好友备注（仅QQ同意好友请求时有效）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 set_friend_add_request 或 set_group_add_request。
   *
   * 对于Telegram:
   * - 调用 approveChatJoinRequest 或 declineChatJoinRequest。
   */
  virtual auto serialize_handle_join_request(
      const common::RequestEvent &request_event, bool approve = true,
      std::string_view reason = "", std::string_view remark = "",
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;

  // --- 文件处理 ---

  /**
   * @brief 从平台下载文件（如图片、语音）。
   * @param file_id 文件ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * 对于QQ:
   * - 调用 get_image 或 get_record。
   *
   * 对于Telegram:
   * - 调用 getFile。
   */
  virtual auto serialize_download_file_request(
      std::string_view file_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string = 0;
};

} // namespace obcx::adapter