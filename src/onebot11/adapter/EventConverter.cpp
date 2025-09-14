#include "onebot11/adapter/EventConverter.hpp"
#include "common/JsonUtils.hpp"
#include "common/Logger.hpp"
#include "onebot11/adapter/MessageConverter.hpp"

using json = nlohmann::json;

namespace obcx::adapter::onebot11 {

auto EventConverter::from_v11_json(std::string_view json_str)
    -> std::optional<common::Event> {
  auto j_opt = common::JsonUtils::parse(std::string(json_str));
  if (!j_opt) {
    OBCX_WARN("EventConverter: 无法解析JSON: {}", json_str);
    return std::nullopt;
  }
  const auto &j = j_opt.value();

  auto post_type = common::JsonUtils::get_value<std::string>(j, "post_type");
  if (post_type.empty()) {
    return std::nullopt;
  }

  try {
    if (post_type == "message") {
      common::MessageEvent event;
      event.from_json(j); // 这里会自动从JSON中解析 message 数组和其他字段

      // 只需要对 raw_message 进行CQ码反转义处理
      auto raw_message_escaped =
          common::JsonUtils::get_value<std::string>(j, "raw_message");
      event.raw_message = MessageConverter::cq_unescape(raw_message_escaped);

      return event;
    }
    if (post_type == "notice") {
      common::NoticeEvent event;
      event.from_json(j);
      return event;
    }
    if (post_type == "request") {
      common::RequestEvent event;
      event.from_json(j);
      return event;
    }
    if (post_type == "meta_event") {
      auto meta_event_type =
          common::JsonUtils::get_value<std::string>(j, "meta_event_type");

      if (meta_event_type == "heartbeat") {
        common::HeartbeatEvent event;
        event.from_json(j);
        OBCX_DEBUG("EventConverter: 接收到心跳事件，间隔: {}ms",
                   event.interval);
        return event;
      } else {
        common::MetaEvent event;
        event.from_json(j);
        return event;
      }
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_ERROR("EventConverter: 创建事件对象时发生JSON异常: {}. JSON: {}",
               e.what(), json_str);
    return std::nullopt;
  }

  OBCX_DEBUG("EventConverter: 未知的 post_type '{}'", post_type);
  return std::nullopt;
}

} // namespace obcx::adapter::onebot11