#include "common/json_utils.hpp"
#include "common/logger.hpp"
#include "onebot11/adapter/message_converter.hpp"
#include "onebot11/adapter/native_fields.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"

namespace obcx::adapter::onebot11 {

auto ProtocolAdapter::parse_event(std::string_view json_str)
    -> std::optional<common::Event> {
  // Avoid the shared parser helper: its diagnostics contain native snippets.
  const auto j = common::json::parse(json_str, nullptr, false);
  if (j.is_discarded()) {
    OBCX_WARN("OneBot ProtocolAdapter: Failed to parse JSON");
    return std::nullopt;
  }

  auto post_type = common::JsonUtils::get_value<std::string>(j, "post_type");
  if (post_type.empty()) {
    return std::nullopt;
  }

  try {
    validate_event_identities(j);
    if (post_type == "message") {
      common::MessageEvent event;
      auto normalized = j;
      if (normalized.contains("anonymous") &&
          normalized["anonymous"].is_object())
        normalized["anonymous"] = "native-anonymous";
      if (j.contains("message") && j.at("message").is_string()) {
        normalized["message"] = MessageConverter::from_v11_string(
            j.at("message").get<std::string>());
      }
      event.from_json(normalized);

      // The sibling standard carrier owns authentic anonymous context. Legacy
      // snapshots retain only the existing observation marker, never its flag.
      event.data = j;
      event.data.erase("flag");
      event.data.erase("anonymous_flag");
      if (event.data.contains("anonymous") &&
          event.data["anonymous"].is_object())
        event.data["anonymous"] = "native-anonymous";

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
      auto legacy = j;
      // Preserve the legacy layout, but never publish live request secrets.
      legacy["flag"] = "";
      legacy.erase("anonymous_flag");
      event.from_json(legacy);
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
  } catch (const std::invalid_argument &) {
    OBCX_WARN("OneBot event rejected: invalid raw identity");
    return std::nullopt;
  } catch (const nlohmann::json::exception &) {
    OBCX_ERROR("OneBot ProtocolAdapter: JSON exception creating event object");
    return std::nullopt;
  }

  OBCX_TRACE("OneBot ProtocolAdapter: Unknown post_type");
  return std::nullopt;
}

} // namespace obcx::adapter::onebot11
