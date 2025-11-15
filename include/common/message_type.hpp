#pragma once

#include "json_utils.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace obcx::common {

/**
 * \if CHINESE
 * @brief 消息状态枚举
 * \endif
 * \if ENGLISH
 * @brief Message status enum.
 * \endif
 */
enum class MessageStatus { ok, failed, async_ };

/**
 * \if CHINESE
 * @brief 基础响应结构
 * \endif
 * \if ENGLISH
 * @brief Base response structure.
 * \endif
 */
struct BaseResponse {
  MessageStatus status;
  int retcode;
  std::optional<std::string> message;
  std::optional<std::string> wording;
  json data;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 基础请求结构
 * \endif
 * \if ENGLISH
 * @brief Base request structure
 * \endif
 */
struct BaseRequest {
  std::string action;
  json params;
  std::optional<std::string> echo;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief OneBot事件类型
 * \endif
 * \if ENGLISH
 * @brief OneBot event type
 * \endif
 */
enum class EventType { message, notice, request, meta_event };

/**
 * \if CHINESE
 * @brief 基础事件结构
 * \endif
 * \if ENGLISH
 * @brief Base event structure
 * \endif
 */
struct BaseEvent {
  EventType type;
  std::chrono::system_clock::time_point time;
  std::string self_id;
  std::string post_type;
  json data;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 消息段类型
 * \endif
 * \if ENGLISH
 * @brief Message segment type
 * \endif
 */
struct MessageSegment {
  std::string type;
  json data;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 消息类型
 * \endif
 * \if ENGLISH
 * @brief Message type
 * \endif
 */
using Message = std::vector<MessageSegment>;

/**
 * \if CHINESE
 * @brief 消息事件
 * \endif
 * \if ENGLISH
 * @brief Message event
 * \endif
 */
struct MessageEvent : public BaseEvent {
  std::string message_type; // private, group, channel
  std::string sub_type;
  std::string message_id;
  std::string user_id;
  Message message;
  std::string raw_message;
  int32_t font;

  /*
   * \if CHINESE
   * 群消息特有字段
   * \endif
   * \if ENGLISH
   * Group message specific fields
   * \endif
   */
  std::optional<std::string> group_id;
  std::optional<std::string> anonymous;

  /*
   * \if CHINESE
   * 频道消息特有字段
   * \endif
   * \if ENGLISH
   * Channel message specific fields
   * \endif
   */
  std::optional<std::string> guild_id;
  std::optional<std::string> channel_id;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 通知事件
 * \endif
 * \if ENGLISH
 * @brief Notice event
 * \endif
 */
struct NoticeEvent : public BaseEvent {
  std::string notice_type;
  std::string user_id;
  std::optional<std::string> group_id;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 请求事件
 * \endif
 * \if ENGLISH
 * @brief Request event
 * \endif
 */
struct RequestEvent : public BaseEvent {
  std::string request_type;
  std::string user_id;
  std::string comment;
  std::string flag;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 元事件
 * \endif
 * \if ENGLISH
 * @brief Meta event
 * \endif
 */
struct MetaEvent : public BaseEvent {
  std::string meta_event_type;
  std::string sub_type;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 心跳事件
 * \endif
 * \if ENGLISH
 * @brief Heartbeat event
 * \endif
 */
struct HeartbeatEvent : public MetaEvent {
  /*
   * \if CHINESE
   * 状态信息
   * \endif
   * \if ENGLISH
   * Status information
   * \endif
   */
  json status;

  /*
   * \if CHINESE
   * 距离上次心跳的间隔时间（毫秒）
   * \endif
   * \if ENGLISH
   * Interval since last heartbeat (milliseconds)
   * \endif
   */
  int64_t interval;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 异常事件结构
 * \endif
 * \if ENGLISH
 * @brief Error event structure
 * \endif
 */
struct ErrorEvent {
  std::string error_type;                     // 异常类型
  std::string error_message;                  // 异常消息
  std::string target_id;                      // 目标ID（用户ID或群组ID）
  bool is_group;                              // 是否为群组
  std::chrono::system_clock::time_point time; // 异常发生时间
  json context;                               // 异常上下文信息

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 事件变体类型
 * \endif
 * \if ENGLISH
 * @brief Event variant type
 * \endif
 */
using Event = std::variant<MessageEvent, NoticeEvent, RequestEvent, MetaEvent,
                           HeartbeatEvent, ErrorEvent>;

/**
 * \if CHINESE
 * @brief 连接配置
 * \endif
 * \if ENGLISH
 * @brief Connection configuration
 * \endif
 */
struct ConnectionConfig {
  std::string host = "localhost";
  uint16_t port = 8080;
  std::string access_token;
  std::string secret;
  std::chrono::milliseconds timeout{30000};
  std::chrono::milliseconds heartbeat_interval{5000};
  bool use_ssl = false;

  // Proxy configuration
  std::string proxy_host;
  uint16_t proxy_port = 0;
  std::string proxy_type = "http"; // "http", "https", "socks5"
  std::string proxy_username;
  std::string proxy_password;

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

/**
 * \if CHINESE
 * @brief 适配器配置
 * \endif
 * \if ENGLISH
 * @brief Adapter configuration
 * \endif
 */
struct AdapterConfig {
  ConnectionConfig v11_config;
  std::string v12_impl_name = "obcx";
  std::string v12_platform = "qq";
  std::string v12_version = "1.0.0";
  bool enable_heartbeat = true;
  std::chrono::milliseconds event_timeout{5000};

  /*
   * \if CHINESE
   * 序列化支持
   * \endif
   * \if ENGLISH
   * Serialization support
   * \endif
   */
  void to_json(json &j) const;
  void from_json(const json &j);
};

} // namespace obcx::common