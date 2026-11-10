#pragma once

#include "core/bot/ids.hpp"
#include "core/bot/json_codec.hpp"
#include "core/bot/validation.hpp"
#include <cstdint>
#include <vector>

namespace obcx::bot {
namespace binding {
class NativeReferenceCodec;
}
namespace detail {
inline void validate_utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i++]);
    if (c < 0x80)
      continue;
    unsigned n;
    std::uint32_t value, minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      n = 1;
      value = c & 31;
      minimum = 0x80;
    } else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      value = c & 15;
      minimum = 0x800;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      value = c & 7;
      minimum = 0x10000;
    } else
      throw std::invalid_argument("invalid UTF-8");
    if (text.size() - i < n)
      throw std::invalid_argument("truncated UTF-8");
    while (n--) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80)
        throw std::invalid_argument("invalid UTF-8 continuation");
      value = (value << 6) | (next & 63);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
      throw std::invalid_argument("noncanonical UTF-8");
  }
}
struct OpaqueParts {
  SurfaceId surface;
  std::string kind;
  std::vector<std::string> components;
};
inline auto encode_opaque(std::string_view tag, const OpaqueParts &parts)
    -> std::string {
  parts.surface.validate();
  if (!valid_bot_id(parts.kind) || parts.components.empty() ||
      parts.components.size() > 8)
    throw std::invalid_argument("invalid opaque identity shape");
  std::string bytes;
  auto append = [&](std::string_view part) {
    validate_identifier(part, "identity component", 1024);
    validate_utf8(part);
    const auto size = static_cast<std::uint32_t>(part.size());
    for (int shift = 24; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<char>(size >> shift));
    bytes.append(part);
  };
  append(parts.surface.value());
  append(parts.kind);
  for (const auto &part : parts.components)
    append(part);
  std::string result = "obcx1:" + std::string(tag) + ":";
  if (bytes.size() > (4096 - result.size()) / 2)
    throw std::invalid_argument("opaque identity exceeds limit");
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char byte : bytes) {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}
inline auto decode_opaque(std::string_view tag, std::string_view encoded)
    -> OpaqueParts {
  const auto prefix = "obcx1:" + std::string(tag) + ":";
  if (encoded.size() > 4096 || !encoded.starts_with(prefix) ||
      (encoded.size() - prefix.size()) % 2)
    throw std::invalid_argument("invalid opaque identity encoding");
  auto hex = [](char c) -> unsigned {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    throw std::invalid_argument("noncanonical identity hex");
  };
  std::string bytes;
  for (auto i = prefix.size(); i < encoded.size(); i += 2)
    bytes.push_back(
        static_cast<char>((hex(encoded[i]) << 4) | hex(encoded[i + 1])));
  std::vector<std::string> fields;
  for (std::size_t i = 0; i < bytes.size();) {
    if (fields.size() == 10 || bytes.size() - i < 4)
      throw std::invalid_argument("invalid identity fields");
    std::uint32_t size = 0;
    for (unsigned j = 0; j < 4; ++j)
      size = (size << 8) | static_cast<unsigned char>(bytes[i++]);
    if (size == 0 || size > 1024 || size > bytes.size() - i)
      throw std::invalid_argument("invalid identity field length");
    fields.push_back(bytes.substr(i, size));
    i += size;
  }
  if (fields.size() < 3)
    throw std::invalid_argument("incomplete opaque identity");
  OpaqueParts parts{
      SurfaceId{fields[0]}, fields[1], {fields.begin() + 2, fields.end()}};
  if (encode_opaque(tag, parts) != encoded)
    throw std::invalid_argument("noncanonical opaque identity");
  return parts;
}
struct ConversationTag {
  static constexpr std::string_view name = "conversation";
};
struct UserTag {
  static constexpr std::string_view name = "user";
};
struct MessageTag {
  static constexpr std::string_view name = "message";
};
struct MediaTag {
  static constexpr std::string_view name = "media";
};
struct MediaIdentityTag {
  static constexpr std::string_view name = "media_identity";
};
struct MediaGroupTag {
  static constexpr std::string_view name = "media_group";
};

template <class Tag> class OpaqueId {
public:
  using obcx_bot_json_factory = void;
  // Structural decoding is provider-independent. Native validation is mandatory
  // at process ingress/dispatch, not a prerequisite for actor persistence.
  static auto restore(std::string_view encoded) -> OpaqueId {
    (void)decode_opaque(Tag::name, encoded);
    return OpaqueId{std::string(encoded)};
  }
  static auto from_json(const Json &document) -> OpaqueId {
    validate_bot_wire(document);
    if (!document.is_string())
      throw std::invalid_argument("opaque identity must be a string");
    return restore(document.get_ref<const std::string &>());
  }
  auto serialized() const noexcept -> const std::string & { return encoded_; }
  void validate() const { (void)decode_opaque(Tag::name, encoded_); }
  void validate_surface(const SurfaceId &surface) const {
    if (decode_opaque(Tag::name, encoded_).surface != surface)
      throw std::invalid_argument("opaque identity surface mismatch");
  }
  auto operator<=>(const OpaqueId &) const = default;
  friend void to_json(Json &document, const OpaqueId &value) {
    value.validate();
    document = value.encoded_;
  }

private:
  using tag_type = Tag;
  explicit OpaqueId(std::string encoded) : encoded_(std::move(encoded)) {}
  std::string encoded_;
  friend class obcx::bot::binding::NativeReferenceCodec;
};
} // namespace detail
using ConversationId = detail::OpaqueId<detail::ConversationTag>;
using UserId = detail::OpaqueId<detail::UserTag>;
using MessageId = detail::OpaqueId<detail::MessageTag>;
using MediaId = detail::OpaqueId<detail::MediaTag>;
using MediaIdentityId = detail::OpaqueId<detail::MediaIdentityTag>;
using MediaGroupId = detail::OpaqueId<detail::MediaGroupTag>;
} // namespace obcx::bot
