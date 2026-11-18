#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace obcx::onebot11::bot {
struct QqFaceAsset {
  std::string_view name;
  bool animated;
  std::span<const unsigned char> bytes;
};

// The returned views refer to static storage. Unlisted IDs have no asset.
auto find_qq_face(std::string_view id) -> std::optional<QqFaceAsset>;
} // namespace obcx::onebot11::bot
