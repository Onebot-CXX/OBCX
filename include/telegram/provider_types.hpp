#ifndef OBCX_INCLUDE_TELEGRAM_PROVIDER_TYPES_HPP_
#define OBCX_INCLUDE_TELEGRAM_PROVIDER_TYPES_HPP_

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace obcx::core {

struct MediaFileInfo {
  std::string file_id;
  std::string file_unique_id;
  std::string file_type;
  std::optional<std::int64_t> file_size;
  std::optional<std::string> mime_type;
  std::optional<std::string> file_name;
};

struct TelegramMediaUpload {
  std::string type;
  std::string filename;
  std::string mime_type;
  std::string data;
};

// Telegram measures message-entity offsets and lengths in UTF-16 code units.
struct TelegramTextEntity {
  std::string type;
  std::size_t offset{};
  std::size_t length{};
};

inline void to_json(nlohmann::json &document,
                    const TelegramTextEntity &entity) {
  document = {{"type", entity.type},
              {"offset", entity.offset},
              {"length", entity.length}};
}

[[nodiscard]] inline auto telegram_utf16_code_units(std::string_view text)
    -> std::size_t {
  std::size_t units = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t width = 1;
    if ((lead & 0xE0U) == 0xC0U) {
      width = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      width = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      width = 4;
    }
    if (index + width > text.size()) {
      width = 1;
    }
    units += width == 4 ? 2U : 1U;
    index += width;
  }
  return units;
}

} // namespace obcx::core

#endif // OBCX_INCLUDE_TELEGRAM_PROVIDER_TYPES_HPP_
