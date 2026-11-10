#pragma once

#include "core/bot/bot_codec.hpp"
#include <string>
#include <string_view>

namespace obcx::onebot11::bot::detail {
// Pure native representation: no filesystem lookup, normalization or mapping.
// Only absolute POSIX final paths are supported by this OneBot recipe.
inline void validate_media_path(std::string_view path) {
  obcx::bot::detail::validate_identifier(path, "QQ final media path", 4096);
  if (!path.starts_with('/'))
    throw std::invalid_argument(
        "QQ media requires an absolute POSIX final path");
}
inline auto media_path_literal(unsigned char c) -> bool {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
         c == '~' || c == '/';
}
inline auto media_file_uri_size(std::string_view path) -> std::size_t {
  validate_media_path(path);
  std::size_t size = 7; // file:// followed by the absolute path
  for (unsigned char c : path)
    size += media_path_literal(c) ? 1 : 3;
  return size;
}
inline auto media_file_uri(std::string_view path) -> std::string {
  std::string uri;
  uri.reserve(media_file_uri_size(path));
  uri = "file://";
  constexpr char hex[] = "0123456789ABCDEF";
  for (unsigned char c : path) {
    if (media_path_literal(c))
      uri.push_back(static_cast<char>(c));
    else {
      uri.push_back('%');
      uri.push_back(hex[c >> 4]);
      uri.push_back(hex[c & 15]);
    }
  }
  return uri;
}
} // namespace obcx::onebot11::bot::detail
