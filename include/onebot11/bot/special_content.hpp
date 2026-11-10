#pragma once
#include "core/bot/bot_events.hpp"
#include "onebot11/bot/group_forward.hpp"
namespace obcx::onebot11::bot {
struct FaceView {
  std::string id;
  std::optional<std::string> description;
  void validate() const {
    detail::validate_identifier(id, "QQ face ID", 20);
    obcx::bot::detail::validate_utf8(id);
    if (description) {
      detail::validate_identifier(*description, "QQ face description", 2048);
      obcx::bot::detail::validate_utf8(*description);
    }
  }
};
struct MarketFaceView {
  std::optional<std::string> description;
  std::optional<MediaRef> media;
  std::optional<bool> animated;
  void validate() const {
    if (description) {
      detail::validate_identifier(*description, "QQ market face description",
                                  2048);
      obcx::bot::detail::validate_utf8(*description);
    }
    if (media)
      media->validate();
  }
};
// Pure recognition of an original live market-face projection. Its opaque
// receipt keeps the original ingress/account authority; the view grants none.
// Image is the received image family, not proof of a static encoding. The
// caller chooses Image/Animation presentation after bounded byte inspection.
// Pure content views below never mint or renew this media receipt.
auto market_face_view(const obcx::bot::ReceivedMedia &)
    -> std::optional<MarketFaceView>;

enum class CardKind : std::uint8_t { Json, App, Ark, MiniApp, Share };
struct RawCardView {
  CardKind kind;
  Json metadata;
  void validate() const {
    switch (kind) {
    case CardKind::Json:
    case CardKind::App:
    case CardKind::Ark:
    case CardKind::MiniApp:
    case CardKind::Share:
      break;
    default:
      throw std::invalid_argument("invalid QQ card kind");
    }
    // This bounds the original data without parsing embedded JSON strings,
    // extracting fields, rewriting URLs, or granting a send/read capability.
    obcx::bot::detail::validate_bot_wire(metadata);
  }
};
struct SpecialContentView {
  MessageRef source;
  std::size_t native_segment_index;
  std::size_t normalized_block_index;
  std::variant<FaceView, MarketFaceView, QqForwardRef, RawCardView> value;
};
// Views of observations already captured in the current typed message.
// No native JSON input, event reconstruction, I/O or admission.
auto special_content(const obcx::bot::MessageReceived &)
    -> std::vector<SpecialContentView>;
} // namespace obcx::onebot11::bot
namespace obcx::bot {
template <> struct detail::BotRecordPreflight<onebot11::bot::FaceView> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "id", 20, false);
    detail::preflight_optional_identifier(d, "description", "QQ face", 2048);
  }
};
template <> struct detail::BotRecordPreflight<onebot11::bot::MarketFaceView> {
  static void validate(const Json &d) {
    detail::preflight_optional_identifier(d, "description", "QQ market face",
                                          2048);
  }
};
OBCX_BOT_FIELDS(onebot11::bot::FaceView, OBCX_BOT_FIELD(id),
                OBCX_BOT_FIELD(description));
OBCX_BOT_FIELDS(onebot11::bot::MarketFaceView, OBCX_BOT_FIELD(description),
                OBCX_BOT_FIELD(media), OBCX_BOT_FIELD(animated));
template <> struct detail::BotRecordPreflight<onebot11::bot::RawCardView> {
  static void validate(const Json &d) {
    detail::validate_bot_wire(d);
    (void)detail::bounded_unsigned(
        d.at("kind"), static_cast<unsigned>(onebot11::bot::CardKind::Share));
  }
};
OBCX_BOT_FIELDS(onebot11::bot::RawCardView, OBCX_BOT_FIELD(kind),
                OBCX_BOT_FIELD(metadata));
OBCX_BOT_FIELDS(onebot11::bot::SpecialContentView, OBCX_BOT_FIELD(source),
                OBCX_BOT_FIELD(native_segment_index),
                OBCX_BOT_FIELD(normalized_block_index), OBCX_BOT_FIELD(value));
} // namespace obcx::bot
