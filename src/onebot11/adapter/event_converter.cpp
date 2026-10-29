#include "common/json_utils.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/message_converter.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

namespace obcx::adapter::onebot11 {

auto ProtocolAdapter::parse_event(std::string_view json_str)
    -> std::optional<common::Event> {
  auto j_opt = common::JsonUtils::parse(std::string(json_str));
  if (!j_opt) {
    OBCX_WARN("OneBot ProtocolAdapter: Failed to parse JSON: {}", json_str);
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
      if (j.contains("message") && j.at("message").is_string()) {
        auto normalized = j;
        normalized["message"] = MessageConverter::from_v11_string(
            j.at("message").get<std::string>());
        event.from_json(normalized);
      } else {
        event.from_json(j);
      }

      // Structured segments are already decoded. For string wire messages,
      // the converter recognizes native CQ syntax before unescaping literals.
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
        OBCX_TRACE("OneBot ProtocolAdapter: Received heartbeat, interval: {}ms",
                   event.interval);
        return event;
      } else {
        common::MetaEvent event;
        event.from_json(j);
        return event;
      }
    }
  } catch (const nlohmann::json::exception &e) {
    OBCX_ERROR("OneBot ProtocolAdapter: JSON exception creating event object: "
               "{}. JSON: {}",
               e.what(), json_str);
    return std::nullopt;
  }

  OBCX_TRACE("OneBot ProtocolAdapter: Unknown post_type '{}'", post_type);
  return std::nullopt;
}

} // namespace obcx::adapter::onebot11
