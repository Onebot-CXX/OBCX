#include "qq_face_catalog.hpp"

#include <algorithm>
#include <iterator>

namespace obcx::onebot11::bot {
namespace {
struct Entry {
  std::string_view id;
  QqFaceAsset asset;
};

// Each face embeds exactly one payload: the animation, or a static fallback.
// clang-format off
#define OBCX_QQ_FACE(id, name, animated) \
  {id, {name, animated, [] { \
    static constexpr unsigned char bytes[]{
#define OBCX_QQ_FACE_END \
    }; \
    return std::span<const unsigned char>{bytes}; \
  }()}},
// clang-format on
constexpr Entry catalog[]{
#include "../resources/qq_faces/index.inc"
};
#undef OBCX_QQ_FACE_END
#undef OBCX_QQ_FACE

static_assert(std::ranges::is_sorted(catalog, {}, &Entry::id));
} // namespace

auto find_qq_face(std::string_view id) -> std::optional<QqFaceAsset> {
  const auto *entry = std::ranges::lower_bound(catalog, id, {}, &Entry::id);
  if (entry == std::end(catalog) || entry->id != id)
    return std::nullopt;
  return entry->asset;
}
} // namespace obcx::onebot11::bot
