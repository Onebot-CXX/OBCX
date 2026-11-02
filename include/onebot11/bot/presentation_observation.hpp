#pragma once
#include "onebot11/bot/special_content.hpp"

#include <string_view>

namespace obcx::onebot11::bot {
// Source observations only: the actor chooses labels, fallback wording and
// layout. Missing text differs from observed empty text. These views carry no
// author identity, media locator, reply evidence or send/read authority.
struct MusicObservation {
  std::optional<std::string> title;
  void validate() const;
};
struct InlineNodeKind {
  std::string native_kind;
  void validate() const;
};
struct InlineNodeText {
  std::string content;
  void validate() const;
};
using InlineNodePart = std::variant<InlineNodeText, FaceView, InlineNodeKind>;
enum class InlineNodeContentForm : std::uint8_t {
  Missing,
  Literal,
  Segments,
  Unsupported
};
struct InlineNodeObservation {
  std::optional<std::string> display_name;
  InlineNodeContentForm form;
  std::vector<InlineNodePart> parts;
  void validate() const;
};
struct PresentationObservation {
  MessageRef source;
  obcx::bot::TimePoint received_at;
  std::size_t native_segment_index;
  std::size_t normalized_block_index;
  std::variant<MusicObservation, InlineNodeObservation> value;
  void validate() const;
};
// Current typed source observations, not native archive reconstruction.
// No nested media addresses, recursive lookup or fabricated node references.
auto presentation_observations(const obcx::bot::MessageReceived &)
    -> std::vector<PresentationObservation>;
} // namespace obcx::onebot11::bot

namespace obcx::bot {
namespace detail {
// Borrow text before allocation. Display text permits empty strings and line
// breaks; identifier validation would silently narrow the existing contract.
inline void preflight_presentation_text_v1(const Json &field) {
  if (!field.is_string())
    throw std::invalid_argument("invalid QQ presentation text");
  const auto &text = field.get_ref<const std::string &>();
  if (text.size() > domain_maximum_bytes)
    throw std::invalid_argument("QQ presentation text exceeds bound");
  validate_utf8(text);
}
template <> struct DomainRecordPreflight<onebot11::bot::MusicObservation> {
  static void validate(const Json &d) {
    if (!d.at("title").is_null())
      preflight_presentation_text_v1(d.at("title"));
  }
};
template <> struct DomainRecordPreflight<onebot11::bot::InlineNodeText> {
  static void validate(const Json &d) {
    preflight_presentation_text_v1(d.at("content"));
  }
};
template <> struct DomainRecordPreflight<onebot11::bot::InlineNodeKind> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "native_kind", 80, false);
    const auto &kind = d.at("native_kind").get_ref<const std::string &>();
    if (!std::ranges::all_of(kind, [](unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                 c == '-';
        }))
      throw std::invalid_argument("invalid QQ node kind");
  }
};
template <> struct DomainRecordPreflight<onebot11::bot::InlineNodeObservation> {
  static void validate(const Json &d) {
    if (!d.at("display_name").is_null())
      preflight_presentation_text_v1(d.at("display_name"));
    const auto form = bounded_unsigned(d.at("form"), 3);
    onebot11::bot::detail::preflight_array(d, "parts", domain_maximum_items,
                                           true);
    const auto &parts = d.at("parts");
    if ((form == 0 || form == 3) && !parts.empty())
      throw std::invalid_argument("QQ node content form mismatch");
    if (form == 1 && parts.size() != 1)
      throw std::invalid_argument("QQ node literal form mismatch");
    for (const auto &part : parts) {
      require_only_keys(part, "QQ node part", {"alternative", "value"});
      const auto kind = bounded_unsigned(part.at("alternative"), 2);
      if (form == 1 && kind != 0)
        throw std::invalid_argument("QQ node literal form mismatch");
      const auto &value = part.at("value");
      if (kind == 0)
        DomainRecordPreflight<onebot11::bot::InlineNodeText>::validate(value);
      else if (kind == 2)
        DomainRecordPreflight<onebot11::bot::InlineNodeKind>::validate(value);
      else {
        DomainRecordPreflight<onebot11::bot::FaceView>::validate(value);
        const auto &id = value.at("id").get_ref<const std::string &>();
        if (!std::ranges::all_of(id,
                                 [](char c) { return c >= '0' && c <= '9'; }))
          throw std::invalid_argument("invalid QQ node face ID");
      }
    }
  }
};
template <>
struct DomainRecordPreflight<onebot11::bot::PresentationObservation> {
  static void validate(const Json &d) {
    (void)bounded_unsigned(d.at("native_segment_index"),
                           domain_maximum_items - 1);
    (void)bounded_unsigned(d.at("normalized_block_index"),
                           domain_maximum_items - 1);
    const auto &value = d.at("value");
    require_only_keys(value, "QQ presentation value", {"alternative", "value"});
    const auto kind = bounded_unsigned(value.at("alternative"), 1);
    if (kind == 0)
      DomainRecordPreflight<onebot11::bot::MusicObservation>::validate(
          value.at("value"));
    else
      DomainRecordPreflight<onebot11::bot::InlineNodeObservation>::validate(
          value.at("value"));
  }
};
} // namespace detail
OBCX_DOMAIN_FIELDS(onebot11::bot::MusicObservation, OBCX_DOMAIN_FIELD(title));
OBCX_DOMAIN_FIELDS(onebot11::bot::InlineNodeText, OBCX_DOMAIN_FIELD(content));
OBCX_DOMAIN_FIELDS(onebot11::bot::InlineNodeKind,
                   OBCX_DOMAIN_FIELD(native_kind));
OBCX_DOMAIN_FIELDS(onebot11::bot::InlineNodeObservation,
                   OBCX_DOMAIN_FIELD(display_name), OBCX_DOMAIN_FIELD(form),
                   OBCX_DOMAIN_FIELD(parts));
OBCX_DOMAIN_FIELDS(onebot11::bot::PresentationObservation,
                   OBCX_DOMAIN_FIELD(source), OBCX_DOMAIN_FIELD(received_at),
                   OBCX_DOMAIN_FIELD(native_segment_index),
                   OBCX_DOMAIN_FIELD(normalized_block_index),
                   OBCX_DOMAIN_FIELD(value));
} // namespace obcx::bot
