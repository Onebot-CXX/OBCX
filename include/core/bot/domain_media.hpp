#pragma once
#include "core/bot/domain_codec.hpp"
#include "core/bot/references.hpp"

namespace obcx::bot {
enum class MediaKind : std::uint8_t {
  Image,
  Animation,
  Video,
  Audio,
  Voice,
  File
};
enum class MediaSourceKind : std::uint8_t { Reference, Bytes, LocalFile, Http };
struct MediaRef {
  BotInstallationRef installation;
  MediaId id;
  void validate() const {
    installation.validate();
    id.validate_surface(installation.surface);
  }
  auto operator==(const MediaRef &) const -> bool = default;
};
struct MediaIdentity {
  BotInstallationRef installation;
  MediaIdentityId id;
  void validate() const {
    installation.validate();
    id.validate_surface(installation.surface);
  }
  auto operator==(const MediaIdentity &) const -> bool = default;
};
struct MediaMetadata {
  std::optional<std::string> mime_type;
  std::optional<std::string> filename;
  std::optional<std::uint64_t> size_bytes;
  std::optional<std::uint32_t> width;
  std::optional<std::uint32_t> height;
  std::optional<std::uint64_t> duration_ms;
  void validate() const {
    if (mime_type)
      detail::validate_identifier(*mime_type, "MIME type", 256);
    if (filename)
      detail::validate_identifier(*filename, "filename", 1024);
    if ((width && !*width) || (height && !*height))
      throw std::invalid_argument("invalid media dimensions");
  }
};
struct BytesSource {
  std::vector<std::uint8_t> bytes;
  MediaMetadata metadata;
  void validate() const {
    metadata.validate();
    if (bytes.empty() || bytes.size() > detail::domain_maximum_bytes ||
        (metadata.size_bytes && *metadata.size_bytes != bytes.size()))
      throw std::invalid_argument("invalid owned media size");
  }
};
// The caller supplies the final path and owns mapping, mounts, accessibility
// and file lifetime. SDK/core does not remap it or require host-only visibility
// or preregistration. The adapter owns native encoding or supported upload I/O;
// structural validation does not prove file existence or size.
struct LocalFile {
  std::string path;
  MediaMetadata metadata;
  void validate() const {
    detail::validate_identifier(path, "media path", 4096);
    metadata.validate();
  }
};
struct HttpSource {
  std::string url;
  MediaMetadata metadata;
  void validate() const {
    detail::validate_identifier(url, "HTTP source", 8192);
    metadata.validate();
    const auto start = url.starts_with("https://")  ? 8U
                       : url.starts_with("http://") ? 7U
                                                    : 0U;
    if (!start)
      throw std::invalid_argument("media source requires HTTP(S)");
    const auto end = url.find_first_of("/?#", start);
    const auto authority = url.substr(start, end - start);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        url.find('#') != std::string::npos)
      throw std::invalid_argument("invalid media HTTP authority");
    // Actual transport preserves credential isolation and its redirect rules;
    // this value does not install a deployment-authorization policy.
  }
};
using MediaSource = std::variant<MediaRef, BytesSource, LocalFile, HttpSource>;
inline void validate_media_source(const MediaSource &source,
                                  const BotInstallationRef &installation) {
  std::visit(
      [&](const auto &value) {
        value.validate();
        if constexpr (std::same_as<std::decay_t<decltype(value)>, MediaRef>) {
          if (value.installation != installation)
            throw std::invalid_argument(
                "cross-installation media requires read then owned bytes");
        }
      },
      source);
}
struct MediaBlob {
  BytesSource source;
  std::optional<MediaKind> kind;
  std::optional<MediaIdentity> identity;
  void validate() const {
    source.validate();
    if (identity)
      identity->validate();
    if (kind && *kind > MediaKind::File)
      throw std::invalid_argument("invalid media kind");
  }
};
template <> struct detail::DomainRecordPreflight<MediaMetadata> {
  static void validate(const Json &d) {
    detail::preflight_optional_identifier(d, "mime_type", "MediaMetadata", 256);
    detail::preflight_optional_identifier(d, "filename", "MediaMetadata", 1024);
  }
};
template <> struct detail::DomainRecordPreflight<BytesSource> {
  static void validate(const Json &d) {
    const auto &bytes = d.at("bytes");
    if (!bytes.is_binary() || bytes.get_binary().empty() ||
        bytes.get_binary().size() > detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid owned media size");
    const auto &size = d.at("metadata").at("size_bytes");
    if (!size.is_null() &&
        detail::domain_decode<std::uint64_t>(size) != bytes.get_binary().size())
      throw std::invalid_argument("invalid owned media size");
  }
};
template <> struct detail::DomainRecordPreflight<LocalFile> {
  static void validate(const Json &d) {
    (void)detail::require_identifier(d, "path", "LocalFile", 4096);
  }
};
template <> struct detail::DomainRecordPreflight<HttpSource> {
  static void validate(const Json &d) {
    (void)detail::require_identifier(d, "url", "HttpSource", 8192);
  }
};

OBCX_DOMAIN_FIELDS(MediaRef, OBCX_DOMAIN_FIELD(installation),
                   OBCX_DOMAIN_FIELD(id));
OBCX_DOMAIN_FIELDS(MediaIdentity, OBCX_DOMAIN_FIELD(installation),
                   OBCX_DOMAIN_FIELD(id));
OBCX_DOMAIN_FIELDS(MediaMetadata, OBCX_DOMAIN_FIELD(mime_type),
                   OBCX_DOMAIN_FIELD(filename), OBCX_DOMAIN_FIELD(size_bytes),
                   OBCX_DOMAIN_FIELD(width), OBCX_DOMAIN_FIELD(height),
                   OBCX_DOMAIN_FIELD(duration_ms));
OBCX_DOMAIN_FIELDS(BytesSource, OBCX_DOMAIN_FIELD(bytes),
                   OBCX_DOMAIN_FIELD(metadata));
OBCX_DOMAIN_FIELDS(LocalFile, OBCX_DOMAIN_FIELD(path),
                   OBCX_DOMAIN_FIELD(metadata));
OBCX_DOMAIN_FIELDS(HttpSource, OBCX_DOMAIN_FIELD(url),
                   OBCX_DOMAIN_FIELD(metadata));
OBCX_DOMAIN_FIELDS(MediaBlob, OBCX_DOMAIN_FIELD(source),
                   OBCX_DOMAIN_FIELD(kind), OBCX_DOMAIN_FIELD(identity));
} // namespace obcx::bot
