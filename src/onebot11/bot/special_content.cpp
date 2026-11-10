#include "onebot11/bot/platform_adapter.hpp"
#include "onebot11/bot/presentation_observation.hpp"

namespace obcx::onebot11::bot {
namespace {
namespace d = obcx::bot;
void validate_source(const d::MessageReceived &message) {
  message.validate();
  if (message.message.conversation.installation.surface != surface)
    throw std::invalid_argument("QQ observation source mismatch");
}
auto face(const d::ReceivedFace &value) -> FaceView {
  return {value.id, value.description};
}
} // namespace
void MusicObservation::validate() const {
  if (title)
    d::validate_observation_text(*title, 65536);
}
void InlineNodeKind::validate() const {
  d::detail::validate_identifier(native_kind, "QQ node kind", 80);
  if (!std::ranges::all_of(native_kind, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '-';
      }))
    throw std::invalid_argument("invalid QQ node kind");
}
void InlineNodeText::validate() const {
  d::validate_observation_text(content, 65536);
}
void InlineNodeObservation::validate() const {
  if (display_name)
    d::validate_observation_text(*display_name, 1024);
  if (form > InlineNodeContentForm::Unsupported || parts.size() > 1024 ||
      ((form == InlineNodeContentForm::Missing ||
        form == InlineNodeContentForm::Unsupported) &&
       !parts.empty()) ||
      (form == InlineNodeContentForm::Literal &&
       (parts.size() != 1 ||
        !std::holds_alternative<InlineNodeText>(parts.front()))))
    throw std::invalid_argument("invalid QQ inline node observation");
  for (const auto &part : parts)
    std::visit([](const auto &v) { v.validate(); }, part);
}
void PresentationObservation::validate() const {
  source.validate();
  if (source.conversation.installation.surface != surface ||
      normalized_block_index >= d::detail::bot_maximum_items)
    throw std::invalid_argument("invalid QQ presentation observation");
  std::visit([](const auto &v) { v.validate(); }, value);
}
auto presentation_observations(const d::MessageReceived &message)
    -> std::vector<PresentationObservation> {
  validate_source(message);
  std::vector<PresentationObservation> result;
  for (const auto &item : message.observations) {
    if (const auto *music = std::get_if<d::ReceivedMusic>(&item.value))
      result.push_back({message.message, message.received_at, item.block_index,
                        item.block_index, MusicObservation{music->title}});
    else if (const auto *node =
                 std::get_if<d::ReceivedInlineNode>(&item.value)) {
      InlineNodeObservation view{node->display_name,
                                 static_cast<InlineNodeContentForm>(node->form),
                                 {}};
      for (const auto &part : node->parts)
        std::visit(
            [&](const auto &v) {
              using V = std::decay_t<decltype(v)>;
              if constexpr (std::same_as<V, d::ReceivedNodeText>)
                view.parts.emplace_back(InlineNodeText{v.content});
              else if constexpr (std::same_as<V, d::ReceivedFace>)
                view.parts.emplace_back(face(v));
              else
                view.parts.emplace_back(InlineNodeKind{v.native_kind});
            },
            part);
      view.validate();
      result.push_back({message.message, message.received_at, item.block_index,
                        item.block_index, std::move(view)});
    }
  }
  return result;
}
auto special_content(const d::MessageReceived &message)
    -> std::vector<SpecialContentView> {
  validate_source(message);
  std::vector<SpecialContentView> result;
  for (const auto &item : message.observations) {
    const auto emit = [&](auto value) {
      value.validate();
      result.push_back({message.message, item.block_index, item.block_index,
                        std::move(value)});
    };
    if (const auto *value = std::get_if<d::ReceivedFace>(&item.value))
      emit(face(*value));
    else if (const auto *value =
                 std::get_if<d::ReceivedMarketFace>(&item.value))
      emit(MarketFaceView{value->description, std::nullopt, std::nullopt});
    else if (const auto *value = std::get_if<d::ReceivedForward>(&item.value))
      emit(QqForwardRef{message.message, value->token});
    else if (const auto *value = std::get_if<d::ReceivedCard>(&item.value))
      emit(RawCardView{static_cast<CardKind>(value->kind), value->metadata});
  }
  return result;
}
auto market_face_view(const d::ReceivedMedia &media)
    -> std::optional<MarketFaceView> {
  if (media.media.installation.surface != surface)
    return std::nullopt;
  media.validate();
  QqNativeReferenceCodec codec;
  codec.validate(media.media);
  if (codec.unpack(media.media.id).kind != "received-mface")
    return std::nullopt;
  if (media.kind != d::MediaKind::Image)
    throw std::invalid_argument("market face kind mismatch");
  std::optional<std::string> summary;
  if (!media.caption.empty()) {
    const auto *label = std::get_if<d::Text>(&media.caption.front());
    if (media.caption.size() != 1 || !label || !label->styles.empty())
      throw std::invalid_argument("market face summary mismatch");
    summary = label->content;
  }
  return MarketFaceView{std::move(summary), media.media, std::nullopt};
}
} // namespace obcx::onebot11::bot
