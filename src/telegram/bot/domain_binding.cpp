#include "telegram/bot/domain_binding.hpp"
#include "telegram/bot/domain_operations.hpp"
#include "telegram/bot/domain_views.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <openssl/evp.h>
#include <set>
#include <utility>

namespace obcx::telegram::bot {
namespace d = obcx::bot;
namespace {
std::int64_t number(std::string_view s, std::int64_t low, std::int64_t high) {
  std::int64_t n{};
  auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
  if (ec != std::errc{} || end != s.data() + s.size() || n < low || n > high ||
      std::to_string(n) != s)
    throw std::invalid_argument("invalid canonical Telegram numeric identity");
  return n;
}
constexpr std::int64_t max_id = (std::int64_t{1} << 52) - 1;
std::string id(const d::Json &j, const char *key) {
  const auto &v = j.at(key);
  if (!v.is_number_integer())
    throw std::invalid_argument("Telegram identity must be integer");
  if (v.is_number_unsigned() &&
      v.get<std::uint64_t>() > static_cast<std::uint64_t>(max_id))
    throw std::invalid_argument("Telegram identity out of range");
  return std::to_string(v.get<std::int64_t>());
}
bool media_kind(std::string_view k) {
  return k == "photo" || k == "animation" || k == "video" ||
         k == "video-note" || k == "audio" || k == "voice" || k == "document" ||
         k == "sticker-static" || k == "sticker-webm" || k == "sticker-tgs";
}
void locator(std::string_view s) {
  if (s.empty() || s.size() > 1024)
    throw std::invalid_argument("invalid Telegram file identity");
  for (unsigned char c : s)
    if (!(std::isalnum(c) && c < 128) && c != '_' && c != '-')
      throw std::invalid_argument("invalid Telegram file identity alphabet");
}
// Map UTF-16 entity boundaries to UTF-8 byte boundaries; surrogate halves are
// invalid.
std::vector<std::optional<std::size_t>> boundaries(std::string_view s) {
  d::detail::validate_utf8(s);
  std::vector<std::optional<std::size_t>> out{0};
  for (std::size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    std::size_t n = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
    if (n == 4)
      out.push_back(std::nullopt);
    i += n;
    out.push_back(i);
  }
  return out;
}
// Apply the same field/work bounds to live input and restoration, before any
// text/entity copy or output construction. The aggregate bound includes fields
// ignored by the typed projection, including unknown archival fields.
void preflight_native_message(const d::Json &message) {
  d::detail::validate_domain_wire(message);
  d::detail::require_object(message, "Telegram message");
  for (auto key : {"text", "caption"})
    if (message.contains(key)) {
      const auto &text = message.at(key).get_ref<const std::string &>();
      if (text.size() > 65536)
        throw std::invalid_argument("Telegram text bound exceeded");
      d::detail::validate_utf8(text);
    }
  for (auto key : {"entities", "caption_entities"})
    if (message.contains(key)) {
      const auto &entities = message.at(key);
      if (!entities.is_array() || entities.size() > 1024)
        throw std::invalid_argument("Telegram entity bound exceeded");
      for (const auto &entity : entities)
        if (entity.is_object() && entity.contains("type"))
          d::detail::validate_identifier(
              entity.at("type").get_ref<const std::string &>(),
              "Telegram entity type", 128);
    }
  // Unknown native fields become one bounded UnsupportedContent each. Charge
  // worst-case output expansion before allocating even the first output block.
  d::detail::DomainWireBudget projected;
  for (auto it = message.begin(); it != message.end(); ++it) {
    d::detail::validate_identifier(it.key(), "Telegram native field", 128);
    projected.nodes(8);
    projected.bytes(it.key().size());
  }
}
d::ReceivedRichText rich(const d::BotInstallationRef &installation,
                         const d::Json &j, const char *text_key,
                         const char *entities_key, bool &styled_mention) {
  if (!j.contains(text_key))
    return {};
  const auto &text = j.at(text_key).get_ref<const std::string &>();
  if (text.size() > 65536 ||
      (j.contains(entities_key) &&
       (!j.at(entities_key).is_array() || j.at(entities_key).size() > 1024)))
    throw std::invalid_argument("Telegram text/entity bound exceeded");
  const auto map = boundaries(text);
  struct Entity {
    std::size_t begin, end;
    const d::Json *value;
  };
  std::vector<Entity> entities;
  std::vector<std::pair<std::size_t, std::size_t>> mention_ranges;
  std::set<std::size_t> cuts{0, text.size()};
  if (j.contains(entities_key))
    for (const auto &e : j.at(entities_key)) {
      if (!e.at("offset").is_number_integer() ||
          !e.at("length").is_number_integer())
        throw std::invalid_argument("Telegram entity range must be integral");
      auto offset = e.at("offset").get<std::int64_t>(),
           length = e.at("length").get<std::int64_t>();
      if (offset < 0 || length <= 0 ||
          static_cast<std::uint64_t>(offset) >= map.size() ||
          static_cast<std::uint64_t>(length) >
              map.size() - 1 - static_cast<std::size_t>(offset) ||
          !map[offset] || !map[offset + length])
        throw std::invalid_argument("invalid Telegram UTF-16 entity range");
      entities.push_back({*map[offset], *map[offset + length], &e});
      const auto &type = e.at("type").get_ref<const std::string &>();
      if (type == "mention" || type == "text_mention")
        mention_ranges.emplace_back(*map[offset], *map[offset + length]);
      cuts.insert(*map[offset]);
      cuts.insert(*map[offset + length]);
    }
  // A native mention cannot overlap another mention. Check the entire field
  // before constructing identity evidence, independently of entity order;
  // otherwise the last entity wins on each cut and can invent partial mentions.
  // Formatting entities may still surround or intersect a genuine mention;
  // the existing styled-mention marker preserves that unsupported combination.
  std::ranges::sort(mention_ranges);
  for (std::size_t n = 1; n < mention_ranges.size(); ++n)
    if (mention_ranges[n].first < mention_ranges[n - 1].second)
      throw std::invalid_argument("overlapping Telegram mention entities");
  d::ReceivedRichText result;
  for (auto it = cuts.begin(); it != cuts.end();) {
    auto begin = *it++;
    if (it == cuts.end())
      break;
    auto end = *it;
    if (begin == end)
      continue;
    std::vector<d::TextStyle> styles;
    const Entity *mention = nullptr;
    for (const auto &e : entities)
      if (e.begin <= begin && e.end >= end) {
        const auto &type = e.value->at("type").get_ref<const std::string &>();
        if (type == "bold")
          styles.push_back(d::TextStyle::Bold);
        if (type == "italic")
          styles.push_back(d::TextStyle::Italic);
        if (type == "code" || type == "pre")
          styles.push_back(d::TextStyle::Code);
        if (type == "mention" || type == "text_mention")
          mention = &e;
      }
    if (mention && !styles.empty())
      styled_mention = true;
    if (mention && begin == mention->begin) {
      if (mention->value->at("type") == "text_mention")
        result.emplace_back(d::Mention{
            native_user(installation, id(mention->value->at("user"), "id"))});
      else {
        const auto spelling = std::string_view{text}.substr(
            mention->begin, mention->end - mention->begin);
        // Profile usernames need not contain @, but a native mention span
        // must. Do not apply the profile convenience rule to event evidence.
        if (!spelling.starts_with('@'))
          throw std::invalid_argument("Telegram mention lacks @ prefix");
        result.emplace_back(d::UnresolvedMention{
            reference_codec().normalize_username(spelling)});
      }
    } else if (!mention) {
      std::ranges::sort(styles);
      auto unique = std::ranges::unique(styles);
      styles.erase(unique.begin(), unique.end());
      result.emplace_back(
          d::Text{text.substr(begin, end - begin), std::move(styles)});
    }
  }
  return result;
}
} // namespace
d::SurfaceId TelegramReferenceCodec::surface() const {
  return d::SurfaceId{"telegram.bot_api"};
}
void TelegramReferenceCodec::validate_native(
    std::string_view tag, std::string_view kind,
    std::span<const std::string> c) const {
  if (tag == "conversation") {
    if (kind == "topic" && c.size() == 2) {
      number(c[0], -max_id, -1);
      number(c[1], 2, INT32_MAX);
      return;
    }
    if (c.size() == 1 && kind == "private") {
      number(c[0], 1, max_id);
      return;
    }
    if (c.size() == 1 && (kind == "group" || kind == "channel")) {
      number(c[0], -max_id, -1);
      return;
    }
  } else if (tag == "user" && kind == "user" && c.size() == 1) {
    number(c[0], 1, max_id);
    return;
  } else if (tag == "message" && kind == "message" && c.size() == 1) {
    number(c[0], 1, INT32_MAX);
    return;
  } else if (tag == "media" && media_kind(kind) &&
             (c.size() == 1 ||
              (kind.starts_with("sticker-") && c.size() == 2))) {
    locator(c[0]);
    if (c.size() == 2)
      d::detail::validate_identifier(c[1], "sticker emoji", 128);
    return;
  } else if (tag == "media_identity" && kind == "file-unique" &&
             c.size() == 1) {
    locator(c[0]);
    return;
  } else if (tag == "media_group" && kind == "album" && c.size() == 1) {
    d::detail::validate_identifier(c[0], "Telegram album", 128);
    return;
  }
  throw std::invalid_argument(
      "invalid Telegram native kind or component count");
}
std::string TelegramReferenceCodec::normalize_username(
    std::string_view username) const {
  if (username.starts_with('@'))
    username.remove_prefix(1);
  if (username.empty() || username.size() > 32)
    throw std::invalid_argument("invalid Telegram username");
  std::string out;
  for (unsigned char c : username) {
    if (c > 127 || !(std::isalnum(c) || c == '_'))
      throw std::invalid_argument("invalid Telegram username");
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}
const TelegramReferenceCodec &reference_codec() {
  static const TelegramReferenceCodec codec;
  return codec;
}
d::UserRef native_user(const d::BotInstallationRef &i, std::string value) {
  reference_codec().validate(i);
  return {i, reference_codec().make<d::UserId>("user", {std::move(value)})};
}
d::ConversationRef native_conversation(const d::BotInstallationRef &i,
                                       const d::Json &message) {
  const auto &chat = message.at("chat");
  auto chat_id = id(chat, "id");
  const auto &type = chat.at("type").get_ref<const std::string &>();
  d::detail::validate_identifier(type, "Telegram chat type", 16);
  std::string kind = type == "supergroup" ? "group" : type;
  std::vector<std::string> c{chat_id};
  const bool forum_topic = message.value("is_topic_message", false);
  if (message.contains("message_thread_id")) {
    if (type != "supergroup")
      throw std::invalid_argument("topic on non-group chat");
    auto topic = id(message, "message_thread_id");
    number(topic, 1, INT32_MAX);
    // Ordinary supergroup replies also carry a message_thread_id. Only an
    // actual forum-topic message changes the conversation's routing scope.
    // General forum topic (1) remains the canonical main group scope.
    if (forum_topic && topic != "1") {
      kind = "topic";
      c.push_back(topic);
    }
  } else if (forum_topic)
    throw std::invalid_argument("missing topic evidence");
  reference_codec().validate(i);
  return {i, reference_codec().make<d::ConversationId>(kind, std::move(c))};
}
d::Sender native_sender(const d::BotInstallationRef &installation,
                        const d::Json &j) {
  if (j.contains("sender_chat")) {
    const auto &chat = j.at("sender_chat");
    const auto &type = chat.at("type").get_ref<const std::string &>();
    d::detail::validate_identifier(type, "Telegram sender chat type", 16);
    const auto native_id = id(chat, "id");
    d::Json m{
        {"chat", {{"id", number(native_id, -max_id, max_id)}, {"type", type}}}};
    return d::ConversationSender{native_conversation(installation, m)};
  }
  if (j.contains("from"))
    return d::KnownUser{native_user(installation, id(j.at("from"), "id"))};
  return d::UnknownSender{};
}
d::MessageRef native_message_reference(
    const d::BotInstallationRef &installation, const d::Json &message) {
  return {native_conversation(installation, message),
          reference_codec().make<d::MessageId>("message",
                                               {id(message, "message_id")})};
}
std::optional<d::ReceivedMedia> native_media(const d::BotInstallationRef &i,
                                             const d::Json &message) {
  const d::Json *file = nullptr;
  std::string kind;
  d::MediaKind mk = d::MediaKind::File;
  for (const auto key : {"photo", "animation", "video", "video_note", "audio",
                         "voice", "document", "sticker"})
    if (message.contains(key)) {
      kind = key;
      file = &message.at(key);
      break;
    }
  if (!file)
    return {};
  if (kind == "photo") {
    if (!file->is_array() || file->empty())
      throw std::invalid_argument("invalid Telegram photo size list");
    file = &file->back();
    mk = d::MediaKind::Image;
  }
  if (kind == "animation")
    mk = d::MediaKind::Animation;
  if (kind == "video" || kind == "video_note")
    mk = d::MediaKind::Video;
  if (kind == "video_note")
    kind = "video-note";
  if (kind == "audio")
    mk = d::MediaKind::Audio;
  if (kind == "voice")
    mk = d::MediaKind::Voice;
  if (kind == "sticker") {
    kind = file->value("is_video", false)      ? "sticker-webm"
           : file->value("is_animated", false) ? "sticker-tgs"
                                               : "sticker-static";
    mk = kind == "sticker-static" ? d::MediaKind::Image
                                  : d::MediaKind::Animation;
  }
  const auto &file_id = file->at("file_id").get_ref<const std::string &>();
  locator(file_id);
  std::vector<std::string> c{file_id};
  if (kind.starts_with("sticker-") && file->contains("emoji") &&
      !file->at("emoji").get_ref<const std::string &>().empty()) {
    const auto &emoji = file->at("emoji").get_ref<const std::string &>();
    d::detail::validate_identifier(emoji, "Telegram sticker emoji", 128);
    c.push_back(emoji);
  }
  d::ReceivedMedia out{{i, reference_codec().make<d::MediaId>(kind, c)},
                       mk,
                       {},
                       std::nullopt,
                       {}};
  try {
    if (file->contains("file_unique_id")) {
      const auto &unique_id =
          file->at("file_unique_id").get_ref<const std::string &>();
      locator(unique_id);
      out.identity =
          d::MediaIdentity{i, reference_codec().make<d::MediaIdentityId>(
                                  "file-unique", {unique_id})};
    }
  } catch (const std::exception &) {
  }
  auto unsigned_field =
      [&](const char *key,
          std::uint64_t maximum) -> std::optional<std::uint64_t> {
    try {
      if (file->contains(key))
        return d::detail::bounded_unsigned(file->at(key), maximum);
    } catch (const std::exception &) {
    }
    return {};
  };
  out.metadata.size_bytes = unsigned_field("file_size", UINT64_MAX);
  if (auto value = unsigned_field("width", UINT32_MAX); value && *value)
    out.metadata.width = *value;
  if (auto value = unsigned_field("height", UINT32_MAX); value && *value)
    out.metadata.height = *value;
  if (auto value = unsigned_field("length", UINT32_MAX); value && *value)
    out.metadata.width = out.metadata.height = *value;
  if (auto value = unsigned_field("duration", UINT64_MAX / 1000))
    out.metadata.duration_ms = *value * 1000;
  if (file->contains("mime_type") && file->at("mime_type").is_string()) {
    const auto &mime = file->at("mime_type").get_ref<const std::string &>();
    const auto separator = mime.find('/');
    const auto ascii_alnum = [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9');
    };
    // Native MIME metadata is a type/subtype, never a provider path. Keep
    // invalid/parameterized values unknown instead of repairing or exposing
    // them. Preserve the existing accepted token character set.
    if (mime.size() <= 256 && separator != std::string::npos && separator > 0 &&
        separator + 1 < mime.size() &&
        mime.find('/', separator + 1) == std::string::npos &&
        ascii_alnum(mime.front()) && ascii_alnum(mime[separator + 1]) &&
        std::ranges::all_of(mime, [](unsigned char c) {
          return c < 128 && (std::isalnum(c) || c == '/' || c == '.' ||
                             c == '+' || c == '-' || c == '_');
        }))
      out.metadata.mime_type = mime;
  }
  if (file->contains("file_name") && file->at("file_name").is_string()) {
    const auto &name = file->at("file_name").get_ref<const std::string &>();
    try {
      d::detail::validate_identifier(name, "Telegram filename", 1024);
      if (name.find_first_of("/\\") == std::string::npos)
        out.metadata.filename = name;
    } catch (const std::exception &) {
    }
  }
  if (kind == "sticker-static")
    out.metadata.mime_type = "image/webp";
  if (kind == "sticker-webm")
    out.metadata.mime_type = "video/webm";
  if (kind == "sticker-tgs")
    out.metadata.mime_type = "application/x-tgsticker";
  out.validate();
  return out;
}
namespace {
d::MessageReceived normalize_message_impl(const d::BotInstallationRef &i,
                                          const d::Json &j,
                                          d::TimePoint received,
                                          std::string_view observation) {
  preflight_native_message(j);
  d::MessageReceived out{.message = native_message_reference(i, j),
                         .sender = native_sender(i, j),
                         .received_at = received};
  const auto &chat = j.at("chat");
  const auto &native_kind = chat.at("type").get_ref<const std::string &>();
  out.context.kind = native_kind == "supergroup" ? "group" : native_kind;
  if (out.context.kind == "group")
    out.context.group_id = id(chat, "id");
  if (j.value("is_topic_message", false)) {
    out.context.topic_id = number(id(j, "message_thread_id"), 1, INT32_MAX);
    if (const auto reply = j.find("reply_to_message");
        reply != j.end() && reply->contains("message_id"))
      out.context.topic_root_reply = number(id(*reply, "message_id"), 1,
                                            INT32_MAX) == *out.context.topic_id;
  }
  if (const auto from = j.find("from"); from != j.end()) {
    if (const auto bot = from->find("is_bot"); bot != from->end())
      out.context.sender_is_bot = bot->get<bool>();
    d::SenderProfileObservation profile;
    const auto profile_field =
        [&](const char *key) -> std::optional<std::string> {
      const auto value = from->find(key);
      if (value == from->end())
        return std::nullopt;
      const auto &text = value->get_ref<const std::string &>();
      if (text.size() > 1024)
        throw std::invalid_argument("Telegram sender profile exceeds bound");
      d::detail::validate_utf8(text);
      return text;
    };
    profile.first_name = profile_field("first_name");
    profile.last_name = profile_field("last_name");
    profile.username = profile_field("username");
    out.context.sender_profile = std::move(profile);
  }
  if (j.contains("edit_date"))
    out.context.edited_at =
        d::TimePoint{static_cast<std::int64_t>(d::detail::bounded_unsigned(
                         j.at("edit_date"), INT64_MAX / 1000)) *
                     1000};
  if (j.contains("date"))
    out.sent_at =
        d::TimePoint{static_cast<std::int64_t>(d::detail::bounded_unsigned(
                         j.at("date"), INT64_MAX / 1000)) *
                     1000};
  const auto text_view = [&](const char *key, const char *entities) {
    try {
      bool styled_mention = false;
      auto text = rich(i, j, key, entities, styled_mention);
      if (styled_mention)
        out.body.emplace_back(
            d::UnsupportedContent{"telegram.styled-mention", std::nullopt});
      return text;
    } catch (const std::exception &) {
      out.body.emplace_back(
          d::UnsupportedContent{"telegram.invalid-entities", std::nullopt});
      const auto &literal = j.at(key).get_ref<const std::string &>();
      if (literal.size() > 65536)
        return d::ReceivedRichText{};
      return d::ReceivedRichText{d::Text{literal, {}}};
    }
  };
  if (j.contains("text"))
    out.body.emplace_back(text_view("text", "entities"));
  bool have_media = false;
  try {
    if (auto media = native_media(i, j)) {
      if (j.contains("caption"))
        media->caption = text_view("caption", "caption_entities");
      out.body.emplace_back(std::move(*media));
      have_media = true;
    }
  } catch (const std::exception &) {
    out.body.emplace_back(
        d::UnsupportedContent{"telegram.invalid-media", std::nullopt});
  }
  if (j.contains("sticker"))
    out.body.emplace_back(
        d::UnsupportedContent{"telegram.sticker", std::nullopt});
  if (j.contains("video_note"))
    out.body.emplace_back(
        d::UnsupportedContent{"telegram.video-note", std::nullopt});
  for (auto key : {"reply_markup", "show_caption_above_media",
                   "has_media_spoiler", "effect_id", "reply_to_story", "quote"})
    if (j.contains(key) && j.at(key) != false)
      out.body.emplace_back(
          d::UnsupportedContent{std::string{"telegram."} + key, std::nullopt});
  if (!have_media && j.contains("caption"))
    out.body.emplace_back(text_view("caption", "caption_entities"));
  for (auto key : {"entities", "caption_entities"})
    if (j.contains(key) && j.at(key).is_array()) {
      for (const auto &e : j.at(key))
        if (e.is_object() && e.contains("type") && e.at("type").is_string()) {
          const auto &type = e.at("type").get_ref<const std::string &>();
          if (type != "bold" && type != "italic" && type != "code" &&
              type != "pre" && type != "mention" && type != "text_mention" &&
              type != "bot_command")
            out.body.emplace_back(d::UnsupportedContent{
                "telegram.entity", type.empty()
                                       ? std::nullopt
                                       : std::optional{type.substr(0, 128)}});
        }
    }
  const std::set<std::string> known{"is_edited",
                                    "message_id",
                                    "message_thread_id",
                                    "is_topic_message",
                                    "from",
                                    "sender_chat",
                                    "sender_boost_count",
                                    "sender_business_bot",
                                    "date",
                                    "chat",
                                    "forward_origin",
                                    "is_automatic_forward",
                                    "reply_to_message",
                                    "external_reply",
                                    "quote",
                                    "reply_to_story",
                                    "via_bot",
                                    "edit_date",
                                    "has_protected_content",
                                    "is_from_offline",
                                    "media_group_id",
                                    "author_signature",
                                    "text",
                                    "entities",
                                    "link_preview_options",
                                    "effect_id",
                                    "animation",
                                    "audio",
                                    "document",
                                    "photo",
                                    "sticker",
                                    "video",
                                    "video_note",
                                    "voice",
                                    "caption",
                                    "caption_entities",
                                    "show_caption_above_media",
                                    "has_media_spoiler",
                                    "reply_markup",
                                    "business_connection_id"};
  for (auto it = j.begin(); it != j.end(); ++it)
    if (!known.contains(it.key()))
      out.body.emplace_back(d::UnsupportedContent{it.key(), std::nullopt});
  if (j.contains("edit_date") || j.value("is_edited", false))
    out.body.emplace_back(
        d::UnsupportedContent{"telegram.edited", std::nullopt});
  if (j.contains("media_group_id")) {
    const auto &album = j.at("media_group_id").get_ref<const std::string &>();
    d::detail::validate_identifier(album, "Telegram album", 128);
    out.media_group = d::MediaGroupRef{
        out.message.conversation,
        reference_codec().make<d::MediaGroupId>("album", {album})};
  }
  if (j.contains("external_reply"))
    out.reply = d::binding::EvidenceFactory::reply_info(
        out.message, {}, {}, d::ReplyRelation::External,
        d::ReplyAvailability::Unavailable);
  else if (j.contains("reply_to_message")) {
    const auto &r = j.at("reply_to_message");
    try {
      // Bot API reply_to_message explicitly denotes the same chat AND thread.
      // Missing nested thread is therefore not missing routing evidence; unlike
      // arbitrary legacy archives, the enclosing reply relationship proves it.
      if (id(r.at("chat"), "id") != id(j.at("chat"), "id"))
        throw std::invalid_argument("reply chat mismatch");
      if ((r.contains("message_thread_id") ||
           r.value("is_topic_message", false)) &&
          native_conversation(i, r) != out.message.conversation)
        throw std::invalid_argument("reply thread mismatch");
      d::MessageRef target{out.message.conversation,
                           reference_codec().make<d::MessageId>(
                               "message", {id(r, "message_id")})};
      auto author = native_sender(i, r);
      std::optional<d::Sender> known_author;
      if (!std::holds_alternative<d::UnknownSender>(author))
        known_author = author;
      out.reply = d::binding::EvidenceFactory::reply_info(
          out.message,
          d::binding::EvidenceFactory::reply_ref(out.message, target,
                                                 std::string{observation}),
          known_author,
          target.conversation == out.message.conversation
              ? d::ReplyRelation::SameConversation
              : d::ReplyRelation::CrossConversation,
          known_author ? d::ReplyAvailability::AuthorKnown
                       : d::ReplyAvailability::ReferenceOnly);
    } catch (const std::exception &) {
      out.reply = d::binding::EvidenceFactory::reply_info(
          out.message, {}, {}, d::ReplyRelation::Unavailable,
          d::ReplyAvailability::Unavailable);
    }
  }
  d::binding::validate_native_event(reference_codec(), out);
  return out;
}
} // namespace

d::MessageReceived normalize_native_message(
    const d::BotInstallationRef &installation, const d::Json &message,
    d::TimePoint received, std::string_view observation) {
  return normalize_message_impl(installation, message, received, observation);
}
namespace {
class Binding final : public core::PlatformDomainBinding {
public:
  const d::binding::NativeReferenceCodec &codec() const override {
    return reference_codec();
  }
  d::MessageReceived normalize_message(
      const d::BotInstallationRef &i, const common::MessageEvent &e,
      d::TimePoint t, std::string_view observation) const override {
    return normalize_native_message(i, e.data, t, observation);
  }
  std::optional<d::UserRef> observed_self(
      const d::BotInstallationRef &,
      const common::MessageEvent &) const override {
    return {}; /* Bot API updates do not prove the receiving bot ID. */
  }
  d::ConversationRef configured_conversation(
      const d::BotInstallationRef &installation,
      const d::ConfiguredConversation &target) const override {
    reference_codec().validate(installation);
    target.validate();
    std::string kind;
    switch (target.kind) {
    case d::ConfiguredConversationKind::Private:
      kind = "private";
      break;
    case d::ConfiguredConversationKind::Group:
      kind = "group";
      break;
    case d::ConfiguredConversationKind::Channel:
      kind = "channel";
      break;
    }
    std::vector<std::string> components{target.native_id};
    if (target.topic && *target.topic != 1) {
      kind = "topic";
      components.push_back(std::to_string(*target.topic));
    }
    return {installation,
            reference_codec().make<d::ConversationId>(kind, components)};
  }
};
} // namespace
std::shared_ptr<const core::PlatformDomainBinding> domain_binding() {
  static auto binding = std::make_shared<const Binding>();
  return binding;
}
UserIdentityView user_identity_view(const d::UserRef &user) {
  reference_codec().validate(user);
  return {user,
          std::stoll(reference_codec().unpack(user.id).components.front())};
}
FileObservationView file_observation_view(const d::ReceivedMedia &media) {
  media.validate();
  reference_codec().validate(media.media);
  const auto parts = reference_codec().unpack(media.media.id);
  const auto kind = parts.kind == "photo" || parts.kind == "sticker-static"
                        ? d::MediaKind::Image
                    : parts.kind == "animation" ||
                            parts.kind == "sticker-webm" ||
                            parts.kind == "sticker-tgs"
                        ? d::MediaKind::Animation
                    : parts.kind == "video" || parts.kind == "video-note"
                        ? d::MediaKind::Video
                    : parts.kind == "audio" ? d::MediaKind::Audio
                    : parts.kind == "voice" ? d::MediaKind::Voice
                                            : d::MediaKind::File;
  if (media.kind != kind)
    throw std::invalid_argument("Telegram file observation kind mismatch");
  std::optional<std::string> unique;
  if (media.identity) {
    reference_codec().validate(*media.identity);
    unique = reference_codec().unpack(media.identity->id).components.front();
  }
  return {media.media, media.identity, parts.components.front(),
          std::move(unique)};
}

std::optional<StickerView> sticker_view(const d::ReceivedMedia &m) {
  // A structurally valid ReceivedMedia can still pair the wrong portable kind
  // or an invalid stable identity with a valid native locator. Apply the same
  // owning validation as the original-file observation before projecting it.
  (void)file_observation_view(m);
  auto p = reference_codec().unpack(m.media.id);
  if (!p.kind.starts_with("sticker-"))
    return {};
  return StickerView{m,
                     p.kind == "sticker-static" ? StickerFormat::StaticWebp
                     : p.kind == "sticker-webm" ? StickerFormat::VideoWebm
                                                : StickerFormat::AnimatedTgs,
                     p.components.size() == 2 ? std::optional{p.components[1]}
                                              : std::nullopt};
}
std::optional<d::ReceivedMedia> video_note_view(const d::ReceivedMedia &m) {
  (void)file_observation_view(m);
  if (reference_codec().unpack(m.media.id).kind == "video-note")
    return m;
  return {};
}
std::optional<MessageUpdateObservation> message_update_observation(
    const d::MessageReceived &message) {
  if (!message_update_view(message))
    return std::nullopt;
  const auto edited_at = message.context.edited_at;
  d::Json reply = nullptr;
  if (message.reply) {
    reply = {{"relation", static_cast<unsigned>(message.reply->relation())}};
    if (message.reply->reference())
      reply["target"] = message.reply->reference()->target();
  }
  const auto serialized =
      d::Json::array({message.message, d::detail::domain_encode(message.sender),
                      d::detail::domain_encode(message.body),
                      d::detail::domain_encode(message.observations),
                      d::detail::domain_encode(message.context.sender_profile),
                      reply})
          .dump();
  if (serialized.size() > d::detail::domain_maximum_bytes)
    throw d::detail::DomainBudgetExceeded{};
  const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  constexpr std::string_view prefix = "obcx.telegram.typed-edit.v1";
  const auto scope = d::Json(message.message).dump();
  const char separator = 0;
  std::array<unsigned char, 32> digest{};
  unsigned int size = 0;
  if (!context ||
      EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context.get(), prefix.data(), prefix.size()) != 1 ||
      EVP_DigestUpdate(context.get(), &separator, 1) != 1 ||
      EVP_DigestUpdate(context.get(), scope.data(), scope.size()) != 1 ||
      EVP_DigestUpdate(context.get(), &separator, 1) != 1 ||
      EVP_DigestUpdate(context.get(), serialized.data(), serialized.size()) !=
          1 ||
      EVP_DigestFinal_ex(context.get(), digest.data(), &size) != 1 ||
      size != digest.size())
    throw std::runtime_error("Telegram edit observation digest unavailable");
  std::string fingerprint;
  fingerprint.reserve(digest.size() * 2);
  constexpr char hex[] = "0123456789abcdef";
  for (const auto byte : digest) {
    fingerprint.push_back(hex[byte >> 4]);
    fingerprint.push_back(hex[byte & 15]);
  }
  return MessageUpdateObservation{message.message, edited_at,
                                  std::move(fingerprint)};
}
std::optional<d::MessageUpdated> message_update_view(
    const d::MessageReceived &message) {
  message.validate();
  // The marker is not a platform identity or authorization proof. Reject a
  // foreign or natively invalid source before exposing a Telegram update view.
  reference_codec().validate(message.message);
  for (const auto &block : message.body)
    if (const auto *marker = std::get_if<d::UnsupportedContent>(&block);
        marker && marker->category == "telegram.edited")
      return d::MessageUpdated{message};
  return {};
}
void validate_topic_result(const CreateTopicRequest &request,
                           const TopicReceipt &result) {
  request.validate();
  result.validate();
  reference_codec().validate(request.parent);
  reference_codec().validate(result.conversation);
  const auto parent = reference_codec().unpack(request.parent.id);
  const auto topic = reference_codec().unpack(result.conversation.id);
  if (result.conversation.installation != request.parent.installation ||
      parent.kind != "group" || topic.kind != "topic" ||
      parent.components[0] != topic.components[0])
    throw std::invalid_argument(
        "topic receipt does not belong to the requested parent");
}
bool is_topic_conversation(const d::ConversationRef &conversation) {
  reference_codec().validate(conversation);
  return reference_codec().unpack(conversation.id).kind == "topic";
}

} // namespace obcx::telegram::bot
