#pragma once

#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace obcx::onebot11::bot::detail {
// Process-private closed vendor extension, not a OneBot11 standard action and
// not an actor-visible arbitrary method/path. Uses the same owned I/O fencing.
enum class FileQueryKind : std::uint8_t { Group, Private };
inline auto file_query_name(FileQueryKind kind) -> std::string_view {
  switch (kind) {
  case FileQueryKind::Group:
    return "get_group_file_url";
  case FileQueryKind::Private:
    return "get_private_file_url";
  }
  throw std::invalid_argument("invalid QQ file query kind");
}
} // namespace obcx::onebot11::bot::detail
