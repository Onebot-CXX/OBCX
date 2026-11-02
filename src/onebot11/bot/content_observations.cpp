#include "onebot11/bot/domain_binding.hpp"

namespace obcx::onebot11::bot {
namespace {
namespace d = obcx::bot;
auto text(const d::Json &object, const char *key, std::size_t maximum)
    -> std::optional<std::string> {
  const auto field = object.find(key);
  if (field == object.end())
    return std::nullopt;
  const auto &value = field->get_ref<const std::string &>();
  d::validate_observation_text(value, maximum);
  return value;
}
auto face(const d::Json &data) -> d::ReceivedFace {
  const auto &id = data.at("id");
  if (!id.is_string() && !id.is_number_integer())
    throw std::invalid_argument("invalid face identifier");
  d::ReceivedFace result{id.is_string() ? id.get<std::string>() : id.dump(),
                         text(data, "text", 2048)};
  result.validate();
  return result;
}
} // namespace
void QqDomainBinding::capture_observations(
    const common::MessageEvent &event, obcx::bot::MessageReceived &message,
    const std::vector<std::size_t> &positions) {
  for (std::size_t index = 0; index < event.message.size(); ++index) {
    const auto &segment = event.message[index];
    try {
      const auto emit = [&](auto value) {
        value.validate();
        message.observations.push_back({positions.at(index), std::move(value)});
      };
      if (segment.type == "face")
        emit(face(segment.data));
      else if (segment.type == "mface")
        emit(d::ReceivedMarketFace{text(segment.data, "summary", 2048)});
      else if (segment.type == "forward") {
        const auto &token = d::detail::require_identifier(
            segment.data, "id", "forward resource", 512);
        d::detail::validate_utf8(token);
        emit(d::ReceivedForward{token});
      } else if (segment.type == "json" || segment.type == "app" ||
                 segment.type == "ark" || segment.type == "miniapp" ||
                 segment.type == "share") {
        const auto kind = segment.type == "json"  ? d::ReceivedCardKind::Json
                          : segment.type == "app" ? d::ReceivedCardKind::App
                          : segment.type == "ark" ? d::ReceivedCardKind::Ark
                          : segment.type == "miniapp"
                              ? d::ReceivedCardKind::MiniApp
                              : d::ReceivedCardKind::Share;
        d::detail::validate_domain_wire(segment.data);
        emit(d::ReceivedCard{kind, segment.data});
      } else if (segment.type == "music")
        emit(d::ReceivedMusic{text(segment.data, "title", 65536)});
      else if (segment.type == "node") {
        d::ReceivedInlineNode node{text(segment.data, "nickname", 1024),
                                   d::ReceivedNodeForm::Missing,
                                   {}};
        if (const auto content = segment.data.find("content");
            content != segment.data.end()) {
          if (content->is_string()) {
            const auto &literal = content->get_ref<const std::string &>();
            d::validate_observation_text(literal, 65536);
            node.form = d::ReceivedNodeForm::Literal;
            node.parts.emplace_back(d::ReceivedNodeText{literal});
          } else if (content->is_array()) {
            if (content->size() > 1024)
              throw std::invalid_argument("inline node exceeds bound");
            node.form = d::ReceivedNodeForm::Segments;
            for (const auto &part : *content) {
              const auto &kind = part.at("type").get_ref<const std::string &>();
              if (kind == "text") {
                const auto value = text(part.at("data"), "text", 65536);
                if (!value)
                  throw std::invalid_argument("inline node text unavailable");
                node.parts.emplace_back(d::ReceivedNodeText{*value});
              } else if (kind == "face")
                node.parts.emplace_back(face(part.at("data")));
              else
                node.parts.emplace_back(d::ReceivedNodeKind{kind});
            }
          } else
            node.form = d::ReceivedNodeForm::Unsupported;
        }
        emit(std::move(node));
      }
    } catch (const std::invalid_argument &) {
      // Leave the corresponding UnsupportedContent in place; no fake values.
    } catch (const d::Json::exception &) {
    }
  }
}
} // namespace obcx::onebot11::bot
