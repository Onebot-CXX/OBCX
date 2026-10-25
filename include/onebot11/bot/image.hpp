#pragma once

#include "common/message_type.hpp"
#include "core/bot/operation_traits.hpp"
#include "onebot11/bot/types.hpp"

#include <array>
#include <charconv>
#include <optional>
#include <string_view>

namespace obcx::onebot11::bot {

// A native get_image identifier, not a URL, server path or access credential.
// The actor must additionally authorize conversation/topic/attachment scope.
struct OneBotImageRef {
  using obcx_bot_json_factory = void;
  BotInstallationRef installation;
  std::string file;

  void validate() const {
    detail::require_onebot(installation, "OneBotImageRef");
    detail::validate_identifier(file, "OneBot image identifier", 1024);
    if (file == "." || file == ".." ||
        file.find_first_of("/\\:?#&=%") != std::string::npos) {
      throw std::invalid_argument(
          "OneBot image identifier is not a native file name");
    }
  }

  static auto from_json(const Json &document) -> OneBotImageRef {
    obcx::bot::detail::require_only_keys(document, "OneBotImageRef",
                                         {"installation", "file"});
    OneBotImageRef result{
        .installation = document.at("installation").get<BotInstallationRef>(),
        .file = detail::require_string(document, "file", "OneBotImageRef")};
    result.validate();
    return result;
  }

  static auto from_segment(BotInstallationRef installation,
                           const common::MessageSegment &segment)
      -> OneBotImageRef {
    if (segment.type != "image") {
      throw std::invalid_argument(
          "OneBot image reference requires an image segment");
    }
    OneBotImageRef result{.installation = std::move(installation),
                          .file = detail::require_string(
                              segment.data, "file", "OneBot image segment")};
    result.validate();
    return result;
  }

  auto operator==(const OneBotImageRef &) const -> bool = default;
};

inline void to_json(Json &document, const OneBotImageRef &image) {
  image.validate();
  document = {{"installation", image.installation}, {"file", image.file}};
}

inline auto is_supported_onebot_image_url(std::string_view url) -> bool {
  // Deliberately not a general URL fetch capability: no credentials, ports,
  // local hosts or arbitrary provider destinations. These QQ CDN links may
  // contain temporary signatures and must remain transient.
  constexpr std::array prefixes{
      "https://multimedia.nt.qq.com.cn/", "http://multimedia.nt.qq.com.cn/",
      "https://gchat.qpic.cn/",           "http://gchat.qpic.cn/",
      "https://c2cpicdw.qpic.cn/",        "http://c2cpicdw.qpic.cn/"};
  return url.size() <= 8192 &&
         std::ranges::any_of(
             prefixes,
             [url](auto prefix) { return url.starts_with(prefix); }) &&
         std::ranges::none_of(url, [](unsigned char c) {
           return c <= 0x20 || c == 0x7f || c == '\\' || c == '#';
         });
}

namespace detail {
inline auto image_optional_text(const Json &document, std::string_view key)
    -> std::string {
  if (!document.contains(std::string{key})) {
    return {};
  }
  return require_string(document, key, "OneBot image response");
}

inline auto image_optional_size(const Json &document, std::string_view key)
    -> std::optional<std::uint64_t> {
  if (!document.contains(std::string{key}) ||
      document.at(std::string{key}).is_null()) {
    return std::nullopt;
  }
  const auto &value = document.at(std::string{key});
  if (value.is_number_unsigned()) {
    return value.get<std::uint64_t>();
  }
  if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
    return static_cast<std::uint64_t>(value.get<std::int64_t>());
  }
  if (value.is_string()) {
    const auto text = value.get<std::string>();
    std::uint64_t size = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), size);
    if (!text.empty() && error == std::errc{} &&
        end == text.data() + text.size()) {
      return size;
    }
  }
  throw std::invalid_argument(
      "OneBot image size must be a nonnegative integer");
}
} // namespace detail

// A transient platform response. file_path is on the OneBot server, not on the
// actor host. An absent URL is not permission to read or forward that path.
// No expiry is guaranteed by get_image; callers supply an explicit reuse bound.
struct OneBotImageInfo {
  using obcx_bot_json_factory = void;
  OneBotImageRef image;
  std::string file_path;
  std::string url;
  std::string file_name;
  std::optional<std::uint64_t> file_size;

  void validate() const {
    image.validate();
    if (file_path.empty() && url.empty()) {
      throw std::invalid_argument("OneBot image response has no path or URL");
    }
    if (!file_path.empty()) {
      detail::validate_identifier(file_path, "OneBot image server path", 16384);
    }
    if (!url.empty() && !is_supported_onebot_image_url(url)) {
      throw std::invalid_argument(
          "OneBot image response has an unsupported URL");
    }
    if (!file_name.empty()) {
      detail::validate_identifier(file_name, "OneBot image filename", 1024);
    }
  }

  static auto from_json(const Json &document) -> OneBotImageInfo {
    obcx::bot::detail::require_only_keys(
        document, "OneBotImageInfo",
        {"image", "file_path", "url", "file_name", "file_size"});
    OneBotImageInfo result{
        .image = document.at("image").get<OneBotImageRef>(),
        .file_path =
            detail::require_string(document, "file_path", "OneBotImageInfo"),
        .url = detail::require_string(document, "url", "OneBotImageInfo"),
        .file_name =
            detail::require_string(document, "file_name", "OneBotImageInfo"),
        .file_size = detail::image_optional_size(document, "file_size")};
    result.validate();
    return result;
  }

  static auto from_provider(OneBotImageRef image, const Json &document)
      -> OneBotImageInfo {
    detail::require_object(document, "OneBot image response");
    auto name = detail::image_optional_text(document, "file_name");
    const auto legacy_name = detail::image_optional_text(document, "filename");
    auto size = detail::image_optional_size(document, "file_size");
    const auto legacy_size = detail::image_optional_size(document, "size");
    if ((!name.empty() && !legacy_name.empty() && name != legacy_name) ||
        (size && legacy_size && size != legacy_size)) {
      throw std::invalid_argument(
          "OneBot image response has conflicting metadata");
    }
    if (name.empty())
      name = legacy_name;
    if (!size)
      size = legacy_size;
    // Whitelist only metadata; never carry base64 or unrelated response data.
    OneBotImageInfo result{.image = std::move(image),
                           .file_path =
                               detail::image_optional_text(document, "file"),
                           .url = detail::image_optional_text(document, "url"),
                           .file_name = std::move(name),
                           .file_size = size};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document, const OneBotImageInfo &info) {
  info.validate();
  document = {
      {"image", info.image},
      {"file_path", info.file_path},
      {"url", info.url},
      {"file_name", info.file_name},
      {"file_size", info.file_size ? Json(*info.file_size) : Json(nullptr)}};
}

struct GetOneBotImageRequest {
  using obcx_bot_json_factory = void;
  inline static const auto &action = actions::get_image;
  OneBotImageRef image;
  // Required: LLOneBot's extension. URL-only callers explicitly select false.
  bool download;

  void validate() const { image.validate(); }

  static auto from_json(const Json &document) -> GetOneBotImageRequest {
    obcx::bot::detail::require_only_keys(document, "GetOneBotImageRequest",
                                         {"action", "image", "download"});
    if (document.contains("action") &&
        document.at("action").get<obcx::bot::ActionId>() != action) {
      throw std::invalid_argument("GetOneBotImageRequest action mismatch");
    }
    GetOneBotImageRequest result{
        .image = document.at("image").get<OneBotImageRef>(),
        .download = document.at("download").get<bool>()};
    result.validate();
    return result;
  }
};

inline void to_json(Json &document, const GetOneBotImageRequest &request) {
  request.validate();
  document = {{"action", request.action},
              {"image", request.image},
              {"download", request.download}};
}

} // namespace obcx::onebot11::bot

namespace obcx::bot {
template <>
struct OperationTraits<onebot11::bot::GetOneBotImageRequest>
    : OperationContract<onebot11::bot::GetOneBotImageRequest,
                        onebot11::bot::OneBotImageInfo, false> {
  static auto supports_surface(const SurfaceId &surface) -> bool {
    return surface == onebot11::bot::surface;
  }
  static auto installation(const request_type &request)
      -> const BotInstallationRef & {
    return request.image.installation;
  }
  static void validate_result(const request_type &request,
                              const result_type &result) {
    result.validate();
    if (result.image != request.image) {
      throw std::invalid_argument("OneBot image result does not match request");
    }
  }
};
} // namespace obcx::bot
