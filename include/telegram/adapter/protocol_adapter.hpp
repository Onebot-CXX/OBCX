#pragma once

#include "common/message_type.hpp"
#include "interfaces/protocol_adapter.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace obcx::adapter::telegram {

/**
 * @brief Telegram协议适配器
 *
 * 协调Telegram Bot API和内部模型之间转换的顶层类。
 * 它继承自 BaseProtocolAdapter，实现了统一的接口，并提供了 Telegram
 * 特有的功能。
 */
class ProtocolAdapter : public BaseProtocolAdapter {
public:
  ProtocolAdapter() = default;

  /**
   * @brief 解析从Telegram Bot API传入的原始JSON字符串。
   * @param json_str 原始JSON字符串。
   * @return 如果是有效事件，则返回转换后的内部 Event 对象；否则返回
   * std::nullopt。
   */
  auto parse_event(std::string_view json_str)
      -> std::optional<common::Event> override;

private:
  /**
   * @brief 解析消息事件
   * @param update_json 更新JSON对象
   * @return 解析后的事件对象
   */
  auto parse_message_event(const nlohmann::json &update_json)
      -> std::optional<common::Event>;

  /**
   * @brief 解析编辑消息事件
   * @param update_json 更新JSON对象
   * @return 解析后的事件对象
   */
  auto parse_edited_message_event(const nlohmann::json &update_json)
      -> std::optional<common::Event>;

  /**
   * @brief 解析频道消息事件
   * @param update_json 更新JSON对象
   * @return 解析后的事件对象
   */
  auto parse_channel_post_event(const nlohmann::json &update_json)
      -> std::optional<common::Event>;

  /**
   * @brief 解析编辑频道消息事件
   * @param update_json 更新JSON对象
   * @return 解析后的事件对象
   */
  auto parse_edited_channel_post_event(const nlohmann::json &update_json)
      -> std::optional<common::Event>;

  /**
   * @brief 解析回调查询事件
   * @param update_json 更新JSON对象
   * @return 解析后的事件对象
   */
  auto parse_callback_query_event(const nlohmann::json &update_json)
      -> std::optional<common::Event>;

public:
  /**
   * @brief 将"发送消息"动作序列化为Telegram API兼容的JSON字符串。
   * @param target_id 目标聊天ID。
   * @param message 要发送的消息对象。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_send_message_request(
      std::string_view target_id, const common::Message &message,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"发送消息到指定topic"动作序列化为Telegram API兼容的JSON字符串。
   * @param target_id 目标聊天ID。
   * @param message 要发送的消息对象。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @param topic_id 可选的topic ID，用于forum群组。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_send_topic_message_request(
      std::string_view target_id, const common::Message &message,
      const std::optional<uint64_t> &echo = std::nullopt,
      const std::optional<int64_t> &topic_id = std::nullopt) -> std::string;

  /**
   * @brief 将"删除消息"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 聊天ID。
   * @param message_id 要删除的消息ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_delete_message_request(
      std::string_view chat_id, std::string_view message_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"获取机器人信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_self_info_request(
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"获取聊天成员信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 目标用户ID。
   * @param no_cache 保留参数，Telegram API 不需要。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_user_info_request(
      std::string_view chat_id, std::string_view user_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"获取聊天信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param no_cache 保留参数，Telegram API 不需要。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_chat_info_request(
      std::string_view chat_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"获取聊天成员信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 目标用户ID。
   * @param no_cache 保留参数，Telegram API 不需要。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_chat_member_info_request(
      std::string_view chat_id, std::string_view user_id, bool no_cache = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"获取聊天成员列表"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_chat_admins_request(
      std::string_view chat_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"踢出聊天成员"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 要踢出的用户ID。
   * @param reject_add_request 保留参数，Telegram API 不需要。
   * @param revoke_messages 是否删除该用户最近的消息。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_kick_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      bool reject_add_request = false, bool revoke_messages = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"限制聊天成员"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 要限制的用户ID。
   * @param duration 限制时长（秒）。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_ban_chat_member_request(
      std::string_view chat_id, std::string_view user_id, int32_t duration,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"限制聊天成员"动作序列化为Telegram
   * API兼容的JSON字符串，用于解除限制。
   * @param chat_id 目标聊天ID。
   * @param user_id 要解除限制的用户ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_unban_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"设置聊天权限"动作序列化为Telegram
   * API兼容的JSON字符串，用于全员禁言。
   * @param chat_id 目标聊天ID。
   * @param enable 是否开启全员禁言。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_ban_all_members_request(
      std::string_view chat_id, bool enable = true,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"设置聊天标题"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param title 新的聊天标题。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_set_chat_title_request(
      std::string_view chat_id, std::string_view title,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"设置聊天头像"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param file 文件路径或ID。
   * @param cache 保留参数，Telegram API 不需要。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_set_chat_photo_request(
      std::string_view chat_id, std::string_view file, bool cache,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"提升聊天成员"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 目标用户ID。
   * @param is_admin 是否设为管理员。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_set_chat_admin_request(
      std::string_view chat_id, std::string_view user_id, bool is_admin = true,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"离开聊天"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param is_dismiss 保留参数，Telegram API 不需要。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_leave_chat_request(
      std::string_view chat_id, bool is_dismiss = false,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"处理聊天加入请求"动作序列化为Telegram API兼容的JSON字符串。
   * @param request_event 请求事件对象 (应包含 chat_join_request 类型的数据)。
   * @param approve 是否同意请求。
   * @param reason 拒绝理由（仅拒绝时有效）。
   * @param remark 保留参数，Telegram API 不需要。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_handle_join_request(
      const common::RequestEvent &request_event, bool approve = true,
      std::string_view reason = "", std::string_view remark = "",
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  /**
   * @brief 将"获取文件"动作序列化为Telegram API兼容的JSON字符串。
   * @param file_id 文件ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_download_file_request(
      std::string_view file_id,
      const std::optional<uint64_t> &echo = std::nullopt)
      -> std::string override;

  // --- Telegram 特有接口 ---

  /**
   * @brief 将"获取用户信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param user_id 目标用户ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_user_info_by_id_request(
      std::string_view user_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"获取聊天信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_chat_request(
      std::string_view chat_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"获取聊天成员列表"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_chat_administrators_request(
      std::string_view chat_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"获取聊天成员信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 目标用户ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_chat_member_request(
      std::string_view chat_id, std::string_view user_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"踢出聊天成员"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 要踢出的用户ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_kick_chat_member_by_id_request(
      std::string_view chat_id, std::string_view user_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"限制聊天成员"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param user_id 要限制的用户ID。
   * @param until_date 限制结束时间戳。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_restrict_chat_member_request(
      std::string_view chat_id, std::string_view user_id, int64_t until_date,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"离开聊天"动作序列化为Telegram API兼容的JSON字符串。
   * @param chat_id 目标聊天ID。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_leave_chat_by_id_request(
      std::string_view chat_id,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"获取机器人信息"动作序列化为Telegram API兼容的JSON字符串。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_me_request(
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;

  /**
   * @brief 将"获取更新"动作序列化为Telegram API兼容的JSON字符串。
   * @param offset 更新的偏移量。
   * @param limit 返回的更新数量限制。
   * @param echo 可选的echo字符串，用于匹配响应。
   * @return 用于动作请求的JSON字符串负载。
   */
  auto serialize_get_updates_request(
      int offset = 0, int limit = 100,
      const std::optional<uint64_t> &echo = std::nullopt) -> std::string;
};

} // namespace obcx::adapter::telegram