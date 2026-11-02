#include "onebot11/bot/domain_binding.hpp"
#include "onebot11/adapter/message_converter.hpp"
#include "onebot11/adapter/native_fields.hpp"
#include "received_media.hpp"
#include <charconv>
#include <limits>

namespace obcx::onebot11::bot {
namespace {
namespace domain = obcx::bot;
void decimal(std::string_view text, bool positive) {
  std::int64_t value{};
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      std::to_string(value) != text || (positive && value <= 0))
    throw std::invalid_argument("noncanonical QQ numeric identity");
}
std::string field(const domain::Json &value, std::string_view key,
                  std::size_t maximum = 20) {
  const auto &v = value.at(std::string(key));
  if (v.is_string()) {
    const auto &text = v.get_ref<const std::string &>();
    if (text.size() > maximum)
      throw std::invalid_argument("QQ native field exceeds bound");
    return text;
  }
  if (v.is_number_integer() &&
      (!v.is_number_unsigned() || v.get<std::uint64_t>() <= INT64_MAX))
    return std::to_string(v.get<std::int64_t>());
  throw std::invalid_argument("invalid QQ identity field");
}
void locator(std::string_view s) {
  domain::detail::validate_identifier(s, "QQ native media locator", 512);
  if (s == "." || s == ".." ||
      s.find_first_of("/\\:?#&=%") != std::string_view::npos)
    throw std::invalid_argument("QQ media locator cannot be a path or URL");
}
auto media_metadata(const domain::Json &j) -> domain::MediaMetadata {
  domain::MediaMetadata result;
  for (auto key : {"file_size", "size"})
    if (j.contains(key))
      try {
        auto value = field(j, key);
        decimal(value, false);
        auto size = std::stoll(value);
        if (size >= 0) {
          result.size_bytes = static_cast<std::uint64_t>(size);
          break;
        }
      } catch (const std::exception &) {
      }
  for (auto key : {"file_name", "name"})
    if (j.contains(key) && j[key].is_string())
      try {
        const auto &value = j[key].get_ref<const std::string &>();
        domain::detail::validate_identifier(value, "QQ media filename", 1024);
        // A display basename is metadata; a provider path or signed locator
        // belongs only to the private acquisition/archive boundary.
        if (value == "." || value == ".." ||
            value.find_first_of("/\\:?#") != std::string::npos)
          continue;
        result.filename = value;
        break;
      } catch (const std::exception &) {
      }
  for (auto key : {"width", "height"})
    if (j.contains(key))
      try {
        auto value = field(j, key);
        decimal(value, true);
        auto n = std::stoll(value);
        if (n <= UINT32_MAX)
          (std::string_view(key) == "width" ? result.width : result.height) =
              static_cast<std::uint32_t>(n);
      } catch (const std::exception &) {
      }
  return result;
}

} // namespace

auto QqDomainBinding::file_metadata(const domain::Json &data)
    -> domain::MediaMetadata {
  auto metadata = media_metadata(data);
  metadata.size_bytes.reset();
  for (const auto key : {"file_size", "size"}) {
    const auto declaration = data.find(key);
    if (declaration == data.end() || declaration->is_null())
      continue;
    std::uint64_t size{};
    if (declaration->is_number_unsigned()) {
      size = declaration->get<std::uint64_t>();
    } else if (declaration->is_number_integer() &&
               declaration->get<std::int64_t>() >= 0) {
      size = static_cast<std::uint64_t>(declaration->get<std::int64_t>());
    } else if (declaration->is_string()) {
      const auto &text = declaration->get_ref<const std::string &>();
      if (text.empty() || text.size() > 20)
        throw std::invalid_argument("invalid QQ file size declaration");
      const auto [end, error] =
          std::from_chars(text.data(), text.data() + text.size(), size);
      if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid QQ file size declaration");
    } else {
      throw std::invalid_argument("invalid QQ file size declaration");
    }
    if (metadata.size_bytes && *metadata.size_bytes != size)
      throw std::invalid_argument("conflicting QQ file size declarations");
    metadata.size_bytes = size;
  }
  // Existing Bridge display name is in "file", not file_id. It is metadata,
  // never a filename-derived query ID or a provider filesystem capability.
  const auto name = data.find("file");
  if (name != data.end() && name->is_string()) {
    try {
      const auto &value = name->get_ref<const std::string &>();
      domain::detail::validate_identifier(value, "QQ file filename", 1024);
      domain::detail::validate_utf8(value);
      if (value != "." && value != ".." &&
          value.find_first_of("/\\:?#") == std::string::npos)
        metadata.filename = value;
    } catch (...) {
      // Unknown filename is not replaced with a manufactured label.
    }
  }
  return metadata;
}

auto QqDomainBinding::attachment_metadata(std::string_view type,
                                          const domain::Json &data)
    -> domain::MediaMetadata {
  if (type == "file")
    return file_metadata(data);
  if (type == "image" || type == "video" || type == "record")
    return media_metadata(data);
  throw std::invalid_argument("unsupported QQ attachment metadata kind");
}

auto QqNativeReferenceCodec::surface() const -> domain::SurfaceId {
  return domain::SurfaceId{"onebot11.qq"};
}
void QqNativeReferenceCodec::validate_native(
    std::string_view tag, std::string_view kind,
    std::span<const std::string> c) const {
  if (tag == "conversation" && (kind == "group" || kind == "private") &&
      c.size() == 1) {
    decimal(c[0], true);
    return;
  }
  if (tag == "user" && kind == "user" && c.size() == 1) {
    decimal(c[0], true);
    return;
  }
  if (tag == "message" && kind == "message" && c.size() == 1) {
    decimal(c[0], false);
    return;
  }
  if (tag == "media" && detail::is_received_media_kind(kind) && c.size() == 1) {
    detail::validate_received_media_token(c[0]);
    return;
  }
  if (tag == "media" &&
      (kind == "forward-image" || kind == "forward-video" ||
       kind == "forward-record" || kind == "forward-file" ||
       kind == "forward-file-id") &&
      c.size() == 7) {
    if (c[0] != "group" && c[0] != "private")
      throw std::invalid_argument("invalid QQ forward media scope");
    decimal(c[1], true);
    decimal(c[2], false);
    locator(c[3]);
    decimal(c[4], false);
    decimal(c[5], false);
    if (c[4].front() == '-' || c[5].front() == '-')
      throw std::invalid_argument("negative QQ forward index");
    if (kind == "forward-file-id") {
      domain::detail::validate_identifier(c[6], "QQ file identifier", 1024);
      domain::detail::validate_utf8(c[6]);
      if (c[6].find("://") != std::string_view::npos || c[6].starts_with("//"))
        throw std::invalid_argument("QQ file identifier is not a URL");
    } else {
      locator(c[6]);
    }
    return;
  }
  if (tag == "media" &&
      (kind == "image" || kind == "video" || kind == "record" ||
       kind == "file" || kind == "file-id") &&
      c.size() == 5) {
    if (c[0] != "group" && c[0] != "private")
      throw std::invalid_argument("invalid QQ media scope");
    decimal(c[1], true);
    decimal(c[2], false);
    decimal(c[3], false);
    if (c[3].front() == '-')
      throw std::invalid_argument("negative QQ attachment index");
    if (kind == "file-id") {
      // Unlike legacy "file" locators, this kind is minted only from an
      // explicit provider file_id. A slash in that ID is not an OS path.
      domain::detail::validate_identifier(c[4], "QQ file identifier", 1024);
      domain::detail::validate_utf8(c[4]);
      if (c[4].find("://") != std::string_view::npos || c[4].starts_with("//"))
        throw std::invalid_argument("QQ file identifier is not a URL");
    } else {
      locator(c[4]);
    }
    return;
  }
  throw std::invalid_argument("unsupported QQ native reference vocabulary");
}
auto QqNativeReferenceCodec::normalize_username(std::string_view username) const
    -> std::string {
  // QQ account identity is numeric. A display name is never a username.
  decimal(username, true);
  return std::string(username);
}
auto QqDomainBinding::codec() const -> const QqNativeReferenceCodec & {
  return codec_;
}
auto QqDomainBinding::observed_self(const domain::BotInstallationRef &i,
                                    const common::MessageEvent &event) const
    -> std::optional<domain::UserRef> {
  codec_.validate(i);
  if (event.self_id.empty())
    return std::nullopt;
  return domain::UserRef{i,
                         codec_.make<domain::UserId>("user", {event.self_id})};
}
auto QqDomainBinding::configured_conversation(
    const domain::BotInstallationRef &installation,
    const domain::ConfiguredConversation &target) const
    -> domain::ConversationRef {
  codec_.validate(installation);
  target.validate();
  if (target.topic ||
      target.kind == domain::ConfiguredConversationKind::Channel)
    throw std::invalid_argument("unsupported QQ configured conversation");
  const auto kind = target.kind == domain::ConfiguredConversationKind::Group
                        ? "group"
                        : "private";
  return {installation,
          codec_.make<domain::ConversationId>(kind, {target.native_id})};
}
auto QqDomainBinding::normalize_deleted_notice(
    const domain::BotInstallationRef &installation,
    const common::NoticeEvent &event, domain::TimePoint received_at) const
    -> std::optional<domain::MessageDeleted> {
  codec_.validate(installation);
  if (event.notice_type != "group_recall" &&
      event.notice_type != "friend_recall")
    return std::nullopt;
  domain::detail::validate_domain_wire(event.data);
  if (!event.data.is_object() || event.post_type != "notice" ||
      event.data.value("post_type", std::string{}) != "notice" ||
      event.data.value("notice_type", std::string{}) != event.notice_type)
    throw std::invalid_argument("QQ recall notice kind mismatch");
  adapter::onebot11::validate_event_identities(event.data);
  const auto message_id = field(event.data, "message_id");
  const bool group = event.notice_type == "group_recall";
  const auto peer = field(event.data, group ? "group_id" : "user_id");
  if ((group && (!event.group_id || *event.group_id != peer)) ||
      (!group && (event.group_id || event.user_id != peer)))
    throw std::invalid_argument("QQ recall notice scope mismatch");
  // The notice supplies the exact group or friend. Never derive private scope
  // from a message author, cached self, or a global message-id lookup.
  domain::MessageDeleted result{
      {{installation, codec_.make<domain::ConversationId>(
                          group ? "group" : "private", {peer})},
       codec_.make<domain::MessageId>("message", {message_id})},
      received_at};
  result.validate();
  return result;
}

auto QqDomainBinding::normalize_message(const domain::BotInstallationRef &i,
                                        const common::MessageEvent &event,
                                        domain::TimePoint received,
                                        std::string_view observation) const
    -> domain::MessageReceived {
  std::vector<std::size_t> ignored;
  return normalize_with_positions(i, event, received, observation, ignored);
}
auto QqDomainBinding::normalize_with_positions(
    const domain::BotInstallationRef &i, const common::MessageEvent &event,
    domain::TimePoint received, std::string_view observation,
    std::vector<std::size_t> &positions) const -> domain::MessageReceived {
  positions.clear();
  if (event.message.size() > domain::detail::domain_maximum_items)
    throw std::invalid_argument("QQ content count exceeds bound");
  positions.reserve(event.message.size());
  codec_.validate(i);
  adapter::onebot11::validate_event_identities(event.data);
  std::string address;
  if (event.message_type == "group" && event.group_id && !event.guild_id &&
      !event.channel_id)
    address = *event.group_id;
  else if (event.message_type == "private" && !event.group_id &&
           !event.guild_id && !event.channel_id)
    address = event.user_id;
  else
    throw std::invalid_argument("ambiguous QQ event scope");
  domain::MessageRef message{
      {i, codec_.make<domain::ConversationId>(event.message_type, {address})},
      codec_.make<domain::MessageId>("message", {event.message_id})};
  domain::Sender sender = domain::UnknownSender{};
  if (!event.anonymous && !event.user_id.empty()) {
    try {
      sender = domain::KnownUser{
          {i, codec_.make<domain::UserId>("user", {event.user_id})}};
    } catch (const std::invalid_argument &) {
    }
  }
  domain::MessageReceived result{
      message, std::move(sender), std::nullopt, received,
      {},      std::nullopt,      std::nullopt};
  result.context.kind = event.message_type;
  result.context.group_id = event.group_id.value_or("");
  result.context.observed_self = observed_self(i, event);
  if (event.data.contains("time")) {
    auto text = field(event.data, "time");
    decimal(text, false);
    auto seconds = std::stoll(text);
    if (seconds > INT64_MAX / 1000 || seconds < INT64_MIN / 1000)
      throw std::invalid_argument("QQ time overflow");
    result.sent_at = domain::TimePoint{seconds * 1000};
  }
  const auto append = [&](domain::ReceivedRichText::value_type value) {
    if (result.body.empty() ||
        !std::holds_alternative<domain::ReceivedRichText>(result.body.back()))
      result.body.emplace_back(domain::ReceivedRichText{});
    std::get<domain::ReceivedRichText>(result.body.back())
        .push_back(std::move(value));
  };
  const auto reply_count = std::ranges::count_if(
      event.message, [](const auto &s) { return s.type == "reply"; });
  for (std::size_t index = 0; index < event.message.size(); ++index) {
    const auto &segment = event.message[index];
    struct PositionRecorder {
      const domain::ReceivedBody &body;
      std::vector<std::size_t> &positions;
      ~PositionRecorder() {
        positions.push_back(body.empty() ? 0 : body.size() - 1);
      }
    } recorder{result.body, positions};
    try {
      if (segment.type == "text") {
        domain::Text text{segment.data.at("text").get<std::string>(), {}};
        text.validate();
        append(std::move(text));
        continue;
      }
      if (segment.type == "at") {
        auto id = field(segment.data, "qq");
        if (id == "all")
          append(domain::MentionEveryone{});
        else
          append(
              domain::Mention{{i, codec_.make<domain::UserId>("user", {id})}});
        continue;
      }
      if (segment.type == "reply" && reply_count == 1 && !result.reply) {
        auto target = domain::MessageRef{
            message.conversation, codec_.make<domain::MessageId>(
                                      "message", {field(segment.data, "id")})};
        auto ref = domain::binding::EvidenceFactory::reply_ref(
            message, target, std::string(observation));
        result.reply = domain::binding::EvidenceFactory::reply_info(
            message, ref, std::nullopt, domain::ReplyRelation::SameConversation,
            domain::ReplyAvailability::ReferenceOnly);
        continue;
      }
      if (segment.type == "image" || segment.type == "video" ||
          segment.type == "record" || segment.type == "file") {
        const auto native_id = segment.data.find("file_id");
        const bool file_id_present =
            segment.type == "file" && native_id != segment.data.end() &&
            !native_id->is_null() &&
            (!native_id->is_string() ||
             !native_id->get_ref<const std::string &>().empty());
        const auto file =
            field(segment.data, file_id_present ? "file_id" : "file",
                  file_id_present ? 1024 : 512);
        auto metadata = attachment_metadata(segment.type, segment.data);
        auto kind = segment.type == "image"    ? domain::MediaKind::Image
                    : segment.type == "video"  ? domain::MediaKind::Video
                    : segment.type == "record" ? domain::MediaKind::Voice
                                               : domain::MediaKind::File;
        // A provider filename is a locator, not evidence of content identity.
        result.body.emplace_back(domain::ReceivedMedia{
            {i, codec_.make<domain::MediaId>(
                    file_id_present ? "file-id" : segment.type,
                    {event.message_type, address, event.message_id,
                     std::to_string(index), file})},
            kind,
            std::move(metadata),
            std::nullopt,
            {}});
        continue;
      }
    } catch (const std::exception &) {
      // Invalid native content stays visible; it never becomes verified
      // evidence.
    }
    const auto safe_type =
        std::ranges::all_of(segment.type, [](unsigned char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                 c == '-';
        });
    result.body.emplace_back(domain::UnsupportedContent{
        "onebot11." + (safe_type ? segment.type.substr(0, 80) : "unknown"),
        std::nullopt});
  }
  capture_observations(event, result, positions);
  domain::binding::validate_native_event(codec_, result);
  return result;
}
} // namespace obcx::onebot11::bot
