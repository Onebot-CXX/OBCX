#pragma once

#include "common/message_type.hpp"
#include "interfaces/protocol_adapter.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace obcx::adapter::onebot11 {

/**
 * \~chinese
 * @brief OneBot v11 协议适配器
 *
 * 协调 OneBot v11 协议和内部模型之间转换的顶层类。
 * 它继承自 BaseProtocolAdapter，实现了统一的接口，并提供了 OneBot v11
 * 特有的功能。
 *
 * \~english
 * @brief OneBot v11 Protocol Adapter
 *
 * The top-level class that coordinates the conversion between the OneBot v11
 * protocol and the internal model. It inherits from BaseProtocolAdapter,
 * implements the unified interface, and provides OneBot v11 specific
 * functionalities.
 */
class ProtocolAdapter : public BaseProtocolAdapter {
public:
  ProtocolAdapter() = default;

  /**
   * \~chinese
   * @brief 解析从 v11 实现传入的原始JSON字符串。
   * @param json_str 原始JSON字符串。
   * @return 如果是有效事件，则返回转换后的内部 Event 对象；否则返回
   * std::nullopt。
   *
   * \~english
   * @brief Parses a raw JSON string from a v11 implementation.
   * @param json_str The raw JSON string.
   * @return The converted internal Event object if it's a valid event;
   * otherwise returns std::nullopt.
   */
  auto parse_event(std::string_view json_str)
      -> std::optional<common::Event> override;

  /**
   * \~chinese
   * @brief 将“发送私聊消息”或“发送群消息”动作序列化为 v11 兼容的JSON字符串。
   * @param target_id 目标用户ID或群ID。
   * @param message 要发送的消息对象。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "send message" action into a v11-compatible
   * JSON string. It determines whether to send a private or group message
   * based on the target_id.
   * @param target_id The target user ID or group ID.
   * @param message The message object to send.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_send_message_request(
      std::string_view target_id, const common::Message &message,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"撤回消息"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 保留参数，OneBot v11 不需要。
   * @param message_id 要撤回的消息ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "delete message" action into a v11-compatible JSON
   * string.
   * @param chat_id Reserved parameter, not needed for OneBot v11.
   * @param message_id The message ID to delete.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_delete_message_request(
      std::string_view chat_id, std::string_view message_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"获取登录号信息"动作序列化为 v11 兼容的JSON字符串。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get self info" action into a v11-compatible JSON
   * string.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_self_info_request(
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"获取陌生人信息"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 保留参数，OneBot v11 不需要。
   * @param user_id 目标用户ID。
   * @param no_cache 是否不使用缓存。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get user info" action into a v11-compatible JSON
   * string.
   * @param chat_id Reserved parameter, not needed for OneBot v11.
   * @param user_id The target user ID.
   * @param no_cache Whether to not use cache.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_user_info_request(
      std::string_view chat_id, std::string_view user_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"获取群信息"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param no_cache 是否不使用缓存。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get chat info" action into a v11-compatible JSON
   * string.
   * @param chat_id The target group ID.
   * @param no_cache Whether to not use cache.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_chat_info_request(
      std::string_view chat_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"获取群成员信息"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param user_id 目标用户ID。
   * @param no_cache 是否不使用缓存。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get chat member info" action into a v11-compatible
   * JSON string.
   * @param chat_id The target group ID.
   * @param user_id The target user ID.
   * @param no_cache Whether to not use cache.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_chat_member_info_request(
      std::string_view chat_id, std::string_view user_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"获取群成员列表"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get chat admins" action into a v11-compatible
   * JSON string. In OneBot v11, we get the full member list and filter admins
   * in the application layer if needed.
   * @param chat_id The target group ID.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_chat_admins_request(
      std::string_view chat_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"群组踢人"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param user_id 要踢的用户ID。
   * @param reject_add_request 是否拒绝此人的加群请求。
   * @param revoke_messages 保留参数，OneBot v11 不支持。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "kick chat member" action into a v11-compatible JSON
   * string.
   * @param chat_id The target group ID.
   * @param user_id The user ID to kick.
   * @param reject_add_request Whether to reject this person's join request.
   * @param revoke_messages Reserved parameter, not supported by OneBot v11.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_kick_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      bool reject_add_request = false, bool revoke_messages = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"群组单人禁言"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param user_id 要禁言的用户ID。
   * @param duration 禁言时长，单位秒，0表示取消禁言。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "ban chat member" action into a v11-compatible JSON
   * string.
   * @param chat_id The target group ID.
   * @param user_id The user ID to ban.
   * @param duration Ban duration in seconds, 0 means unban.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_ban_chat_member_request(
      std::string_view chat_id, std::string_view user_id, int32_t duration,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"取消群组单人禁言"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param user_id 要解除禁言的用户ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes an "unban chat member" action into a v11-compatible JSON
   * string. This is achieved by calling set_group_ban with duration 0.
   * @param chat_id The target group ID.
   * @param user_id The user ID to unban.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_unban_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"群组全员禁言"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param enable 是否开启全员禁言。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "ban all members" action into a v11-compatible
   * JSON string.
   * @param chat_id The target group ID.
   * @param enable Whether to enable ban all.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_ban_all_members_request(
      std::string_view chat_id, bool enable = true,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"设置群名片"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param title 新的群名片。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "set chat title" action into a v11-compatible JSON
   * string.
   * @param chat_id The target group ID.
   * @param title The new group card.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_set_chat_title_request(
      std::string_view chat_id, std::string_view title,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"设置群头像"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param file 文件路径或ID。
   * @param cache 是否使用缓存。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "set chat photo" action into a v11-compatible JSON
   * string.
   * @param chat_id The target group ID.
   * @param file The file path or ID.
   * @param cache Whether to use cache.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_set_chat_photo_request(
      std::string_view chat_id, std::string_view file, bool cache,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"设置群管理员"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param user_id 目标用户ID。
   * @param is_admin 是否设为管理员。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "set chat admin" action into a v11-compatible JSON
   * string.
   * @param chat_id The target group ID.
   * @param user_id The target user ID.
   * @param is_admin Whether to set as admin.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_set_chat_admin_request(
      std::string_view chat_id, std::string_view user_id, bool is_admin = true,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"退出群组"动作序列化为 v11 兼容的JSON字符串。
   * @param chat_id 目标群ID。
   * @param is_dismiss 是否解散群组（仅群主可用）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "leave chat" action into a v11-compatible JSON string.
   * @param chat_id The target group ID.
   * @param is_dismiss Whether to dismiss the group (group owner only).
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_leave_chat_request(
      std::string_view chat_id, bool is_dismiss = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"处理加好友请求"或"处理加群请求/邀请"动作序列化为 v11
   * 兼容的JSON字符串。
   * @param request_event 请求事件对象。
   * @param approve 是否同意请求。
   * @param reason 拒绝理由（仅拒绝时有效）。
   * @param remark 添加后的好友备注（仅QQ同意好友请求时有效）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "handle join request" action into a v11-compatible
   * JSON string. It determines whether it's a friend request or a group request
   * based on the event data.
   * @param request_event The request event object.
   * @param approve Whether to approve the request.
   * @param reason Reason for rejection (only valid when rejecting).
   * @param remark Friend remark after adding (only valid when approving a
   * friend request).
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_handle_join_request(
      const common::RequestEvent &request_event, bool approve = true,
      std::string_view reason = "", std::string_view remark = "",
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * \~chinese
   * @brief 将"获取图片"或"获取语音"动作序列化为 v11 兼容的JSON字符串。
   * @param file_id 文件ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "download file" action into a v11-compatible JSON
   * string. It determines whether it's an image or a record based on the file
   * ID or context, which is handled internally.
   * @param file_id The file ID.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_download_file_request(
      std::string_view file_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  // --- OneBot v11 特有接口 ---

  /**
   * \~chinese
   * @brief 将“发送私聊消息”动作序列化为 v11 兼容的JSON字符串。
   * @param user_id 目标用户ID。
   * @param message 要发送的消息对象。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "send private message" action into a v11-compatible
   * JSON string.
   * @param user_id The target user ID.
   * @param message The message object to send.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_send_private_message_request(
      std::string_view user_id, const common::Message &message,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将“发送群消息”动作序列化为 v11 兼容的JSON字符串。
   * @param group_id 目标群ID。
   * @param message 要发送的消息对象。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "send group message" action into a v11-compatible JSON
   * string.
   * @param group_id The target group ID.
   * @param message The message object to send.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_send_group_message_request(
      std::string_view group_id, const common::Message &message,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将"获取消息"动作序列化为 v11 兼容的JSON字符串。
   * @param message_id 要获取的消息ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get message" action into a v11-compatible JSON string.
   * @param message_id The message ID to get.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_message_request(
      std::string_view message_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将"获取合并转发内容"动作序列化为 v11 兼容的JSON字符串。
   * @param forward_id 合并转发ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get forward msg" action into a v11-compatible JSON
   * string.
   * @param forward_id The forward message ID.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_forward_msg_request(
      std::string_view forward_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将"获取好友列表"动作序列化为 v11 兼容的JSON字符串。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get friend list" action into a v11-compatible JSON
   * string.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_friend_list_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将"获取群列表"动作序列化为 v11 兼容的JSON字符串。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "get group list" action into a v11-compatible JSON
   * string.
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_get_group_list_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  // --- 状态获取扩展 API ---

  auto serialize_get_status_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_version_info_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  // --- 群组管理扩展 API ---

  auto serialize_set_group_name_request(
      std::string_view group_id, std::string_view group_name,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_set_group_admin_request(
      std::string_view group_id, std::string_view user_id, bool enable,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_set_group_anonymous_ban_request(
      std::string_view group_id, const std::string &anonymous, int32_t duration,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_set_group_anonymous_request(
      std::string_view group_id, bool enable,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_set_group_portrait_request(
      std::string_view group_id, std::string_view file, bool cache,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_group_honor_info_request(
      std::string_view group_id, std::string_view type,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_image_request(
      std::string_view file, const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string;

  auto serialize_get_record_request(
      std::string_view file, std::string_view out_format,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_group_file_url_request(
      std::string_view group_id, std::string_view file_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_private_file_url_request(
      std::string_view user_id, std::string_view file_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_can_send_image_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_can_send_record_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  // --- QQ相关接口凭证 API ---

  auto serialize_get_cookies_request(
      std::string_view domain,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_csrf_token_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  auto serialize_get_credentials_request(
      std::string_view domain,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将"处理加好友请求"动作序列化为 v11 兼容的JSON字符串。
   * @param flag 请求flag。
   * @param approve 是否同意请求。
   * @param remark 添加后的好友备注（仅同意时有效）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "set friend add request" action into a v11-compatible
   * JSON string.
   * @param flag Request flag.
   * @param approve Whether to approve the request.
   * @param remark Friend remark after adding (only valid when approving).
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_set_friend_add_request(
      std::string_view flag, bool approve = true, std::string_view remark = "",
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * \~chinese
   * @brief 将"处理加群请求/邀请"动作序列化为 v11 兼容的JSON字符串。
   * @param flag 请求flag。
   * @param sub_type 请求类型（add/invite）。
   * @param approve 是否同意请求。
   * @param reason 拒绝理由（仅拒绝时有效）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   *
   * \~english
   * @brief Serializes a "set group add request" action into a v11-compatible
   * JSON string.
   * @param flag Request flag.
   * @param sub_type Request type (add/invite).
   * @param approve Whether to approve the request.
   * @param reason Reason for rejection (only valid when rejecting).
   * @param echo Optional echo string to match the response.
   * @return The JSON string payload for the action request.
   */
  auto serialize_set_group_add_request(
      std::string_view flag, std::string_view sub_type, bool approve = true,
      std::string_view reason = "",
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  // \~chinese 其他动作的序列化方法可以按需在此处添加...
  // \~english Serialization methods for other actions can be added here as
  // needed...
};

} // namespace obcx::adapter::onebot11