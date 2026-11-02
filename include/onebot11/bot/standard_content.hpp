#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/standard_common.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace obcx::onebot11::bot::standard {

// Explicit provider expansion intent, not measured remote content/size
// evidence.
enum class DelegatedWork { AcceptUnmeasured, RequireVerifiedBounds };
// Provider-side filename reuse is not local file/read authority.
struct ProviderFileName {
  obcx::bot::BotInstallationRef installation;
  std::string name;
  DelegatedWork work;
};
// Caller supplies the final absolute file URI; no mapping, probing or opening.
// The optional lease owner is retained by the original owning SDK invocation
// through await, never serialized or replaced by a process-global registry.
// Pending does not prove the provider finished reading: the caller must keep
// the file alive after return for any still-pending provider work.
struct CallerFileUri {
  std::string uri;
  std::shared_ptr<const void> lease;
};
struct InlineBase64 {
  std::string data;
};
struct RemoteMediaUrl {
  std::string url;
  bool cache;
  bool proxy;
  std::optional<std::uint64_t> timeout_seconds;
  DelegatedWork work;
};
using SendMediaSource =
    std::variant<ProviderFileName, CallerFileUri, InlineBase64, RemoteMediaUrl>;
struct Image {
  SendMediaSource source;
  bool flash;
};
struct Record {
  SendMediaSource source;
  bool magic;
};
struct Video {
  SendMediaSource source;
};

// Outbound content only: no received URL/evidence, action or reply authority.
struct Text {
  std::string text;
};
struct Face {
  std::string id;
};
struct AtUser {
  obcx::bot::UserRef user;
};
struct AtAll {};
struct Rps {};
struct Dice {};
struct Shake {};
struct Poke {
  std::string type;
  std::string id;
};
struct Anonymous {
  std::optional<bool> ignore;
};
struct Share {
  std::string url;
  std::string title;
  std::optional<std::string> content;
  std::optional<std::string> image_url;
};
struct UserContact {
  obcx::bot::UserRef user;
};
struct GroupContact {
  obcx::bot::ConversationRef group;
};
// Decimal spelling is preserved, without binary floating-point conversion or
// an invented geographic/provider range. NaN and infinity are not decimals.
struct DecimalCoordinate {
  std::string decimal;
};
struct Location {
  DecimalCoordinate latitude;
  DecimalCoordinate longitude;
  std::optional<std::string> title;
  std::optional<std::string> content;
};
enum class MusicProvider : std::uint8_t { Qq, NetEase163, Xiami };
struct Music {
  MusicProvider provider;
  std::string id;
};
struct CustomMusic {
  std::string url;
  std::string audio_url;
  std::string title;
  std::optional<std::string> content;
  std::optional<std::string> image_url;
};
// Opaque outbound documents. The SDK bounds and preserves UTF-8 bytes; it
// never parses, resolves entities, fetches schemas or interprets operations.
struct XmlContent {
  std::string document;
};
struct JsonContent {
  std::string document;
};

using SendLeaf =
    std::variant<Text, Face, AtUser, AtAll, Rps, Dice, Shake, Poke, Anonymous,
                 Share, UserContact, GroupContact, Location, Music, CustomMusic,
                 XmlContent, JsonContent, Image, Record, Video>;
enum class ContentEncoding : std::uint8_t {
  SegmentArray,
  SingleSegmentObject,
  CqString
};
struct ComposedContent {
  ContentEncoding encoding;
  std::vector<SendLeaf> segments;
};
// This string is never interpreted as CQ. The private encoder requires
// auto_escape=true; composed CQ requires false. There is no raw CQ input.
struct LiteralText {
  std::string text;
};
using SendContent = std::variant<LiteralText, ComposedContent>;

} // namespace obcx::onebot11::bot::standard
