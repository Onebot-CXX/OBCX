#include "common/message_type.hpp"
#include "common/i18n_log_messages.hpp"

namespace obcx::common {

// C++20 compatible remove_optional_t
template <typename T> struct remove_optional {
  using type = T;
};
template <typename T> struct remove_optional<std::optional<T>> {
  using type = T;
};
template <typename T>
using remove_optional_t = typename remove_optional<T>::type;

// BaseResponse 序列化
void BaseResponse::to_json(json &j) const {
  j["status"] = (status == MessageStatus::ok)       ? "ok"
                : (status == MessageStatus::failed) ? "failed"
                                                    : "async";
  j["retcode"] = ret_code;
  if (message.has_value()) {
    j["message"] = message.value();
  }
  if (wording.has_value()) {
    j["wording"] = wording.value();
  }
  j["data"] = data;
}

void BaseResponse::from_json(const json &j) {
  std::string status_str =
      JsonUtils::get_value(j, "status", std::string("failed"));
  if (status_str == "ok") {
    status = MessageStatus::ok;
  } else if (status_str == "async") {
    status = MessageStatus::async_;
  } else {
    status = MessageStatus::failed;
  }

  ret_code = JsonUtils::get_value(j, "retcode", -1);
  message = JsonUtils::get_optional<std::string>(j, "message");
  wording = JsonUtils::get_optional<std::string>(j, "wording");
  data = JsonUtils::get_value(j, "data", json::object());
}

// BaseRequest 序列化
void BaseRequest::to_json(json &j) const {
  j["action"] = action;
  j["params"] = params;
  if (echo.has_value()) {
    j["echo"] = echo.value();
  }
}

void BaseRequest::from_json(const json &j) {
  action = JsonUtils::get_value(j, "action", std::string(""));
  params = JsonUtils::get_value(j, "params", json::object());
  echo = JsonUtils::get_optional<std::string>(j, "echo");
}

// 序列化/反序列化辅助宏，减少重复代码
#define SERIALIZE_FIELD(j, obj, field)                                         \
  JsonUtils::set_value(j, #field, (obj).field)
#define DESERIALIZE_FIELD(j, obj, field)                                       \
  (obj).field = JsonUtils::get_value<decltype((obj).field)>(j, #field)

#define SERIALIZE_OPTIONAL_FIELD(j, obj, field)                                \
  JsonUtils::set_optional(j, #field, (obj).field)
#define DESERIALIZE_OPTIONAL_FIELD(j, obj, field)                              \
  (obj).field =                                                                \
      JsonUtils::get_optional<remove_optional_t<decltype((obj).field)>>(       \
          j, #field)

void to_json(json &j, const MessageSegment &seg) {
  SERIALIZE_FIELD(j, seg, type);
  SERIALIZE_FIELD(j, seg, data);
}

void from_json(const json &j, MessageSegment &seg) {
  DESERIALIZE_FIELD(j, seg, type);
  DESERIALIZE_FIELD(j, seg, data);
}

// BaseEvent
void BaseEvent::to_json(json &j) const {
  // EventType to string conversion is handled by consumers
  JsonUtils::set_value(
      j, "time",
      std::chrono::duration_cast<std::chrono::duration<double>>(
          time.time_since_epoch())
          .count());
  SERIALIZE_FIELD(j, *this, self_id);
  SERIALIZE_FIELD(j, *this, post_type);
}

void BaseEvent::from_json(const json &j) {
  // string to EventType conversion is handled by consumers
  auto time_double = JsonUtils::get_value<double>(j, "time");
  time = std::chrono::system_clock::time_point(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::duration<double>(time_double)));
  DESERIALIZE_FIELD(j, *this, self_id);
  DESERIALIZE_FIELD(j, *this, post_type);
}

// MessageEvent
void MessageEvent::to_json(json &j) const {
  BaseEvent::to_json(j);
  SERIALIZE_FIELD(j, *this, message_type);
  SERIALIZE_FIELD(j, *this, sub_type);
  SERIALIZE_FIELD(j, *this, message_id);
  SERIALIZE_FIELD(j, *this, user_id);
  JsonUtils::set_value(j, "message", message); // Uses MessageSegment's to_json
  SERIALIZE_FIELD(j, *this, raw_message);
  SERIALIZE_FIELD(j, *this, font);
  SERIALIZE_OPTIONAL_FIELD(j, *this, group_id);
  SERIALIZE_OPTIONAL_FIELD(j, *this, anonymous);
  SERIALIZE_OPTIONAL_FIELD(j, *this, guild_id);
  SERIALIZE_OPTIONAL_FIELD(j, *this, channel_id);
}

void MessageEvent::from_json(const json &j) {
  BaseEvent::from_json(j);
  DESERIALIZE_FIELD(j, *this, message_type);
  DESERIALIZE_FIELD(j, *this, sub_type);

  message_id = JsonUtils::get_id_as_string(j, "message_id");
  user_id = JsonUtils::get_id_as_string(j, "user_id");

  if (j.contains("message")) {
    message = j.at("message").get<Message>();
  }
  DESERIALIZE_FIELD(j, *this, raw_message);
  DESERIALIZE_FIELD(j, *this, font);

  group_id = JsonUtils::get_optional_id_as_string(j, "group_id");

  DESERIALIZE_OPTIONAL_FIELD(j, *this, anonymous);
  DESERIALIZE_OPTIONAL_FIELD(j, *this, guild_id);
  DESERIALIZE_OPTIONAL_FIELD(j, *this, channel_id);
}

// NoticeEvent
void NoticeEvent::to_json(json &j) const {
  BaseEvent::to_json(j);
  SERIALIZE_FIELD(j, *this, notice_type);
  SERIALIZE_FIELD(j, *this, user_id);
  SERIALIZE_OPTIONAL_FIELD(j, *this, group_id);
}

void NoticeEvent::from_json(const json &j) {
  BaseEvent::from_json(j);
  DESERIALIZE_FIELD(j, *this, notice_type);
  user_id = JsonUtils::get_id_as_string(j, "user_id");
  group_id = JsonUtils::get_optional_id_as_string(j, "group_id");
  // 保存原始JSON数据，以便访问如message_id等额外字段
  data = j;
}

// RequestEvent
void RequestEvent::to_json(json &j) const {
  BaseEvent::to_json(j);
  SERIALIZE_FIELD(j, *this, request_type);
  SERIALIZE_FIELD(j, *this, user_id);
  SERIALIZE_FIELD(j, *this, comment);
  SERIALIZE_FIELD(j, *this, flag);
}

void RequestEvent::from_json(const json &j) {
  BaseEvent::from_json(j);

  DESERIALIZE_FIELD(j, *this, request_type);
  user_id = JsonUtils::get_id_as_string(j, "user_id");
  DESERIALIZE_FIELD(j, *this, comment);
  DESERIALIZE_FIELD(j, *this, flag);
}

// MetaEvent
void MetaEvent::to_json(json &j) const {
  BaseEvent::to_json(j);
  SERIALIZE_FIELD(j, *this, meta_event_type);
  SERIALIZE_FIELD(j, *this, sub_type);
}

void MetaEvent::from_json(const json &j) {
  BaseEvent::from_json(j);
  DESERIALIZE_FIELD(j, *this, meta_event_type);
  DESERIALIZE_FIELD(j, *this, sub_type);
}

// HeartbeatEvent
void HeartbeatEvent::to_json(json &j) const {
  MetaEvent::to_json(j);
  SERIALIZE_FIELD(j, *this, status);
  SERIALIZE_FIELD(j, *this, interval);
}

void HeartbeatEvent::from_json(const json &j) {
  MetaEvent::from_json(j);
  status = JsonUtils::get_value(j, "status", json::object());
  interval = JsonUtils::get_value(j, "interval", int64_t(0));
}

// ErrorEvent
void ErrorEvent::to_json(json &j) const {
  SERIALIZE_FIELD(j, *this, error_type);
  SERIALIZE_FIELD(j, *this, error_message);
  SERIALIZE_FIELD(j, *this, target_id);
  SERIALIZE_FIELD(j, *this, is_group);
  JsonUtils::set_value(
      j, "time",
      std::chrono::duration_cast<std::chrono::duration<double>>(
          time.time_since_epoch())
          .count());
  SERIALIZE_FIELD(j, *this, context);
}

void ErrorEvent::from_json(const json &j) {
  DESERIALIZE_FIELD(j, *this, error_type);
  DESERIALIZE_FIELD(j, *this, error_message);
  DESERIALIZE_FIELD(j, *this, target_id);
  DESERIALIZE_FIELD(j, *this, is_group);
  auto time_double = JsonUtils::get_value<double>(j, "time");
  time = std::chrono::system_clock::time_point(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::duration<double>(time_double)));
  context = JsonUtils::get_value(j, "context", json::object());
}
} // namespace obcx::common