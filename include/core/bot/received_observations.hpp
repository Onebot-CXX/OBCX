#pragma once
#include "core/bot/bot_messaging.hpp"

namespace obcx::bot {
// Named receive-only content facts. None is an event archive, a send intent,
// a media locator or permission to query the source platform.
inline void validate_observation_text(std::string_view text,
                                      std::size_t maximum) {
  if (text.size() > maximum)
    throw std::invalid_argument("receive observation exceeds bound");
  detail::validate_utf8(text);
}
struct ReceivedFace {
  std::string id;
  std::optional<std::string> description;
  void validate() const {
    detail::validate_identifier(id, "face identifier", 20);
    if (!std::ranges::all_of(
            id, [](unsigned char c) { return c >= '0' && c <= '9'; }))
      throw std::invalid_argument("invalid face identifier");
    if (description)
      validate_observation_text(*description, 2048);
  }
};
struct ReceivedMarketFace {
  std::optional<std::string> description;
  void validate() const {
    if (description)
      validate_observation_text(*description, 2048);
  }
};
enum class ReceivedCardKind : std::uint8_t { Json, App, Ark, MiniApp, Share };
struct ReceivedCard {
  ReceivedCardKind kind;
  Json metadata;
  void validate() const {
    if (kind > ReceivedCardKind::Share || !metadata.is_object())
      throw std::invalid_argument("invalid received card");
    detail::validate_bot_wire(metadata);
  }
};
struct ReceivedForward {
  std::string token;
  void validate() const {
    detail::validate_identifier(token, "forward resource identifier", 512);
    detail::validate_utf8(token);
  }
};
struct ReceivedMusic {
  std::optional<std::string> title;
  void validate() const {
    if (title)
      validate_observation_text(*title, 65536);
  }
};
struct ReceivedNodeKind {
  std::string native_kind;
  void validate() const {
    detail::validate_identifier(native_kind, "node content kind", 80);
  }
};
struct ReceivedNodeText {
  std::string content;
  void validate() const { validate_observation_text(content, 65536); }
};
using ReceivedNodePart =
    std::variant<ReceivedNodeText, ReceivedFace, ReceivedNodeKind>;
enum class ReceivedNodeForm : std::uint8_t {
  Missing,
  Literal,
  Segments,
  Unsupported
};
struct ReceivedInlineNode {
  std::optional<std::string> display_name;
  ReceivedNodeForm form;
  std::vector<ReceivedNodePart> parts;
  void validate() const {
    if (display_name)
      validate_observation_text(*display_name, 1024);
    if (form > ReceivedNodeForm::Unsupported || parts.size() > 1024 ||
        ((form == ReceivedNodeForm::Missing ||
          form == ReceivedNodeForm::Unsupported) &&
         !parts.empty()) ||
        (form == ReceivedNodeForm::Literal &&
         (parts.size() != 1 ||
          !std::holds_alternative<ReceivedNodeText>(parts.front()))))
      throw std::invalid_argument("invalid received node content");
    for (const auto &part : parts)
      std::visit([](const auto &v) { v.validate(); }, part);
  }
};
struct ReceivedObservation {
  std::size_t block_index;
  std::variant<ReceivedFace, ReceivedMarketFace, ReceivedCard, ReceivedForward,
               ReceivedMusic, ReceivedInlineNode>
      value;
  void validate() const {
    std::visit([](const auto &v) { v.validate(); }, value);
  }
};
OBCX_BOT_FIELDS(ReceivedFace, OBCX_BOT_FIELD(id), OBCX_BOT_FIELD(description));
OBCX_BOT_FIELDS(ReceivedMarketFace, OBCX_BOT_FIELD(description));
OBCX_BOT_FIELDS(ReceivedCard, OBCX_BOT_FIELD(kind), OBCX_BOT_FIELD(metadata));
OBCX_BOT_FIELDS(ReceivedForward, OBCX_BOT_FIELD(token));
OBCX_BOT_FIELDS(ReceivedMusic, OBCX_BOT_FIELD(title));
OBCX_BOT_FIELDS(ReceivedNodeKind, OBCX_BOT_FIELD(native_kind));
OBCX_BOT_FIELDS(ReceivedNodeText, OBCX_BOT_FIELD(content));
OBCX_BOT_FIELDS(ReceivedInlineNode, OBCX_BOT_FIELD(display_name),
                OBCX_BOT_FIELD(form), OBCX_BOT_FIELD(parts));
OBCX_BOT_FIELDS(ReceivedObservation, OBCX_BOT_FIELD(block_index),
                OBCX_BOT_FIELD(value));
} // namespace obcx::bot
