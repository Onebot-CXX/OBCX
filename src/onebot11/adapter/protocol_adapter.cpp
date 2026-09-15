#include "onebot11/adapter/protocol_adapter.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/message_converter.hpp"

namespace obcx::adapter::onebot11 {

auto ProtocolAdapter::serialize_send_message_request(
    std::string_view target_id, const common::Message &message,
    const std::optional<uint64_t> &echo,
    const std::optional<uint8_t> &message_type) -> std::string {
  if (*message_type == static_cast<uint8_t>(MessageType::Group)) {
    return serialize_send_group_message_request(target_id, message, echo);
  } else if (*message_type == static_cast<uint8_t>(MessageType::Private)) {
    return serialize_send_private_message_request(target_id, message, echo);
  } else {
    OBCX_TRACE("Unsupported message type: {}", static_cast<int>(*message_type));
    return "";
  }
}

auto ProtocolAdapter::serialize_delete_message_request(
    std::string_view chat_id, std::string_view message_id,
    const std::optional<uint64_t> &echo) -> std::string {
  // For OneBot11, chat_id is not needed for delete_msg
  nlohmann::json j;
  j["action"] = "delete_msg";

  nlohmann::json params;
  params["message_id"] = message_id;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_self_info_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_login_info";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_user_info_request(
    std::string_view chat_id, std::string_view user_id, bool no_cache,
    const std::optional<uint64_t> &echo) -> std::string {
  // For OneBot11, chat_id is not needed for get_stranger_info
  nlohmann::json j;
  j["action"] = "get_stranger_info";

  nlohmann::json params;
  params["user_id"] = user_id;
  params["no_cache"] = no_cache;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_chat_info_request(
    std::string_view chat_id, bool no_cache,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_group_info";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["no_cache"] = no_cache;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_chat_member_info_request(
    std::string_view chat_id, std::string_view user_id, bool no_cache,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_group_member_info";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["user_id"] = user_id;
  params["no_cache"] = no_cache;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_chat_admins_request(
    std::string_view chat_id, const std::optional<uint64_t> &echo)
    -> std::string {
  // For OneBot11, we get the full member list and filter admins in the
  // application layer
  nlohmann::json j;
  j["action"] = "get_group_member_list";

  nlohmann::json params;
  params["group_id"] = chat_id;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_kick_chat_member_request(
    std::string_view chat_id, std::string_view user_id, bool reject_add_request,
    bool revoke_messages, const std::optional<uint64_t> &echo) -> std::string {
  // For OneBot11, revoke_messages is not supported
  nlohmann::json j;
  j["action"] = "set_group_kick";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["user_id"] = user_id;
  params["reject_add_request"] = reject_add_request;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_ban_chat_member_request(
    std::string_view chat_id, std::string_view user_id, int32_t duration,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_ban";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["user_id"] = user_id;
  params["duration"] = duration;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_unban_chat_member_request(
    std::string_view chat_id, std::string_view user_id,
    const std::optional<uint64_t> &echo) -> std::string {
  // For OneBot11, unban is done by setting duration to 0
  nlohmann::json j;
  j["action"] = "set_group_ban";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["user_id"] = user_id;
  params["duration"] = 0;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_ban_all_members_request(
    std::string_view chat_id, bool enable, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_whole_ban";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["enable"] = enable;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_chat_title_request(
    std::string_view chat_id, std::string_view title,
    const std::optional<uint64_t> &echo) -> std::string {
  return serialize_set_group_name_request(chat_id, title, echo);
}

auto ProtocolAdapter::serialize_set_chat_photo_request(
    std::string_view chat_id, std::string_view file, bool cache,
    const std::optional<uint64_t> &echo) -> std::string {
  return serialize_set_group_portrait_request(chat_id, file, cache, echo);
}

auto ProtocolAdapter::serialize_set_chat_admin_request(
    std::string_view chat_id, std::string_view user_id, bool is_admin,
    const std::optional<uint64_t> &echo) -> std::string {
  return serialize_set_group_admin_request(chat_id, user_id, is_admin, echo);
}

auto ProtocolAdapter::serialize_leave_chat_request(
    std::string_view chat_id, bool is_dismiss,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_leave";

  nlohmann::json params;
  params["group_id"] = chat_id;
  params["is_dismiss"] = is_dismiss;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_handle_join_request(
    const common::RequestEvent &request_event, bool approve,
    std::string_view reason, std::string_view remark,
    const std::optional<uint64_t> &echo) -> std::string {
  if (request_event.request_type == "friend") {
    return serialize_set_friend_add_request(request_event.flag, approve, remark,
                                            echo);
  } else if (request_event.request_type == "group") {
    // TODO: RequestEvent currently lacks a sub_type field; default to "add".
    std::string sub_type = "add";
    return serialize_set_group_add_request(request_event.flag, sub_type,
                                           approve, reason, echo);
  }

  return "";
}

auto ProtocolAdapter::serialize_download_file_request(
    std::string_view file_id, const std::optional<uint64_t> &echo)
    -> std::string {
  // OneBot11 splits image vs voice retrieval; sniff by file extension.
  if (file_id.find(".image") != std::string::npos) {
    return serialize_get_image_request(file_id, echo);
  } else {
    return serialize_get_record_request(file_id, "mp3", echo);
  }
}

auto ProtocolAdapter::serialize_send_private_message_request(
    std::string_view user_id, const common::Message &message,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "send_private_msg";

  nlohmann::json params;
  params["user_id"] = user_id;
  params["message"] = MessageConverter::to_v11_string(message);

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_send_group_message_request(
    std::string_view group_id, const common::Message &message,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "send_group_msg";

  nlohmann::json params;
  params["group_id"] = group_id;

  params["message"] = MessageConverter::to_v11_string(message);

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_message_request(
    std::string_view message_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "get_msg";

  nlohmann::json params;
  params["message_id"] = message_id;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_forward_msg_request(
    std::string_view forward_id, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "get_forward_msg";

  nlohmann::json params;
  params["id"] = forward_id;

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized get_forward_msg request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_friend_list_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_friend_list";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_group_list_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_group_list";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_status_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_status";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_version_info_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_version_info";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_group_name_request(
    std::string_view group_id, std::string_view group_name,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_name";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["group_name"] = std::string(group_name);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_group_admin_request(
    std::string_view group_id, std::string_view user_id, bool enable,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_admin";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["user_id"] = std::string(user_id);
  j["params"]["enable"] = enable;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_group_anonymous_ban_request(
    std::string_view group_id, const std::string &anonymous, int32_t duration,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_anonymous_ban";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["anonymous"] = nlohmann::json::parse(anonymous);
  j["params"]["duration"] = duration;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_group_anonymous_request(
    std::string_view group_id, bool enable, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_anonymous";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["enable"] = enable;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_group_portrait_request(
    std::string_view group_id, std::string_view file, bool cache,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_portrait";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["file"] = std::string(file);
  j["params"]["cache"] = cache;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_group_honor_info_request(
    std::string_view group_id, std::string_view type,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_group_honor_info";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["type"] = std::string(type);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_friend_add_request(
    std::string_view flag, bool approve, std::string_view remark,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "set_friend_add_request";

  nlohmann::json params;
  params["flag"] = flag;
  params["approve"] = approve;
  if (!remark.empty()) {
    params["remark"] = remark;
  }

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_set_group_add_request(
    std::string_view flag, std::string_view sub_type, bool approve,
    std::string_view reason, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "set_group_add_request";

  nlohmann::json params;
  params["flag"] = flag;
  params["sub_type"] = sub_type;
  params["approve"] = approve;
  if (!reason.empty()) {
    params["reason"] = reason;
  }

  j["params"] = params;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_image_request(
    std::string_view file, const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_image";
  j["params"]["file"] = std::string(file);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_record_request(
    std::string_view file, std::string_view out_format,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_record";
  j["params"]["file"] = std::string(file);
  j["params"]["out_format"] = std::string(out_format);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_can_send_image_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "can_send_image";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_can_send_record_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "can_send_record";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_cookies_request(
    std::string_view domain, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "get_cookies";
  if (!domain.empty()) {
    j["params"]["domain"] = std::string(domain);
  } else {
    j["params"] = nlohmann::json::object();
  }

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_csrf_token_request(
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_csrf_token";
  j["params"] = nlohmann::json::object();

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_credentials_request(
    std::string_view domain, const std::optional<uint64_t> &echo)
    -> std::string {
  nlohmann::json j;
  j["action"] = "get_credentials";
  if (!domain.empty()) {
    j["params"]["domain"] = std::string(domain);
  } else {
    j["params"] = nlohmann::json::object();
  }

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_group_file_url_request(
    std::string_view group_id, std::string_view file_id,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_group_file_url";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["file_id"] = std::string(file_id);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_get_private_file_url_request(
    std::string_view user_id, std::string_view file_id,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "get_private_file_url";
  j["params"]["user_id"] = std::string(user_id);
  j["params"]["file_id"] = std::string(file_id);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_group_poke_request(
    std::string_view group_id, std::string_view user_id,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "group_poke";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["user_id"] = std::string(user_id);

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

auto ProtocolAdapter::serialize_send_group_forward_msg_request(
    std::string_view group_id, const nlohmann::json &messages,
    const std::optional<uint64_t> &echo) -> std::string {
  nlohmann::json j;
  j["action"] = "send_group_forward_msg";
  j["params"]["group_id"] = std::string(group_id);
  j["params"]["messages"] = messages;

  if (echo) {
    j["echo"] = echo.value();
  }

  OBCX_DEBUG("Serialized action request: {}", j.dump());
  return j.dump();
}

} // namespace obcx::adapter::onebot11
