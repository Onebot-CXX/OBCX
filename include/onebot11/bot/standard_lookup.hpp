#pragma once

#include "onebot11/bot/standard_queries.hpp"
#include <memory>
#include <variant>

namespace obcx::onebot11::bot::standard {
namespace detail {
class LookupContentAccess;
}
// Detached observations only. Neither reported IDs nor restricted locators
// grant conversation/reply provenance, ingestion, or media-read authority.
struct LookupAbsent {};
struct LookupNull {};
template <typename T>
using LookupField = std::variant<LookupAbsent, LookupNull, T>;
class DetachedContent;
using DetachedNodeUserId = std::variant<std::string, std::int64_t>;
// Each segment retains a null data object separately from an empty object.
struct DetachedTextFields {
  LookupField<std::string> text;
};
struct DetachedText {
  std::optional<DetachedTextFields> data;
};
struct DetachedFaceFields {
  LookupField<std::string> id;
};
struct DetachedFace {
  std::optional<DetachedFaceFields> data;
};
struct DetachedImageFields {
  LookupField<SensitiveText> file;
  LookupField<std::string> type;
  LookupField<SensitiveText> url;
  LookupField<bool> cache;
  LookupField<bool> proxy;
  LookupField<std::string> timeout;
};
struct DetachedImage {
  std::optional<DetachedImageFields> data;
};
struct DetachedRecordFields {
  LookupField<SensitiveText> file;
  LookupField<bool> magic;
  LookupField<SensitiveText> url;
  LookupField<bool> cache;
  LookupField<bool> proxy;
  LookupField<std::string> timeout;
};
struct DetachedRecord {
  std::optional<DetachedRecordFields> data;
};
struct DetachedVideoFields {
  LookupField<SensitiveText> file;
  LookupField<SensitiveText> url;
  LookupField<bool> cache;
  LookupField<bool> proxy;
  LookupField<std::string> timeout;
};
struct DetachedVideo {
  std::optional<DetachedVideoFields> data;
};
struct DetachedAtFields {
  LookupField<std::string> qq;
};
struct DetachedAt {
  std::optional<DetachedAtFields> data;
};
struct DetachedRpsFields {};
struct DetachedRps {
  std::optional<DetachedRpsFields> data;
};
struct DetachedDiceFields {};
struct DetachedDice {
  std::optional<DetachedDiceFields> data;
};
struct DetachedShakeFields {};
struct DetachedShake {
  std::optional<DetachedShakeFields> data;
};
struct DetachedPokeFields {
  LookupField<std::string> type;
  LookupField<std::string> id;
  LookupField<std::string> name;
};
struct DetachedPoke {
  std::optional<DetachedPokeFields> data;
};
struct DetachedAnonymousFields {
  LookupField<bool> ignore;
};
struct DetachedAnonymous {
  std::optional<DetachedAnonymousFields> data;
};
struct DetachedShareFields {
  LookupField<SensitiveText> url;
  LookupField<std::string> title;
  LookupField<std::string> content;
  LookupField<SensitiveText> image;
};
struct DetachedShare {
  std::optional<DetachedShareFields> data;
};
struct DetachedContactFields {
  LookupField<std::string> type;
  LookupField<std::string> id;
};
struct DetachedContact {
  std::optional<DetachedContactFields> data;
};
struct DetachedLocationFields {
  LookupField<std::string> lat;
  LookupField<std::string> lon;
  LookupField<std::string> title;
  LookupField<std::string> content;
};
struct DetachedLocation {
  std::optional<DetachedLocationFields> data;
};
struct DetachedMusicFields {
  LookupField<std::string> type;
  LookupField<std::string> id;
  LookupField<SensitiveText> url;
  LookupField<SensitiveText> audio;
  LookupField<std::string> title;
  LookupField<std::string> content;
  LookupField<SensitiveText> image;
};
struct DetachedMusic {
  std::optional<DetachedMusicFields> data;
};
struct DetachedReplyFields {
  LookupField<std::string> id;
};
struct DetachedReply {
  std::optional<DetachedReplyFields> data;
};
struct DetachedForwardFields {
  LookupField<SensitiveText> id;
};
struct DetachedForward {
  std::optional<DetachedForwardFields> data;
};
struct DetachedNodeFields {
  LookupField<std::string> id;
  LookupField<DetachedNodeUserId> user_id;
  LookupField<std::string> nickname;
  LookupField<std::shared_ptr<const DetachedContent>> content;
};
struct DetachedNode {
  std::optional<DetachedNodeFields> data;
};
struct DetachedXmlFields {
  LookupField<SensitiveText> data;
};
struct DetachedXml {
  std::optional<DetachedXmlFields> data;
};
struct DetachedJsonFields {
  LookupField<SensitiveText> data;
};
struct DetachedJson {
  std::optional<DetachedJsonFields> data;
};
using DetachedSegment =
    std::variant<DetachedText, DetachedFace, DetachedImage, DetachedRecord,
                 DetachedVideo, DetachedAt, DetachedRps, DetachedDice,
                 DetachedShake, DetachedPoke, DetachedAnonymous, DetachedShare,
                 DetachedContact, DetachedLocation, DetachedMusic,
                 DetachedReply, DetachedForward, DetachedNode, DetachedXml,
                 DetachedJson>;
enum class DetachedContentEncoding : std::uint8_t {
  CqString,
  SegmentArray,
  SingleSegmentObject
};
// Immutable, codec-created content; only const, typed observations escape.
// No raw native JSON or promotion into the common inbound message family.
class DetachedContent final {
public:
  DetachedContent(DetachedContent &&) noexcept = default;
  auto operator=(DetachedContent &&) noexcept -> DetachedContent & = default;
  DetachedContent(const DetachedContent &) = delete;
  auto operator=(const DetachedContent &) -> DetachedContent & = delete;
  auto encoding() const noexcept -> DetachedContentEncoding {
    return encoding_;
  }
  auto segments() const noexcept -> const std::vector<DetachedSegment> & {
    return segments_;
  }

private:
  DetachedContent(DetachedContentEncoding, std::vector<DetachedSegment>);
  DetachedContentEncoding encoding_;
  std::vector<DetachedSegment> segments_;
  friend class detail::LookupContentAccess;
};
struct DetachedSender {
  LookupField<std::int64_t> user_id;
  LookupField<std::string> nickname, card;
  LookupField<Sex> sex;
  LookupField<std::int32_t> age;
  LookupField<std::string> area, level;
  LookupField<GroupRole> role;
  LookupField<std::string> title;
};
struct DetachedMessage {
  obcx::bot::BotInstallationRef installation;
  std::int32_t time; // Reported Unix seconds, never received_at.
  MessageKind message_type;
  std::int32_t message_id;
  std::int32_t real_id;
  DetachedSender sender;
  DetachedContent message;
};
struct DetachedForwardMessage {
  obcx::bot::BotInstallationRef installation;
  DetachedContent message; // Standard node array, in provider order.
};
} // namespace obcx::onebot11::bot::standard
