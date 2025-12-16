#include "common/embedded_locales.hpp"
#include <array>

namespace obcx::common {

// C++26 embed directive to embed .mo files
// Note: #embed expands to a comma-separated list of
// integer-constant-expressions We store as unsigned char array and cast at
// runtime via std::span

#ifdef __has_embed

// Check if we can embed the zh_CN locale
#if __has_embed("../../build/locales/zh_CN/LC_MESSAGES/messages.mo")
alignas(std::byte) static constexpr unsigned char zh_CN_mo_data_raw[] = {
#embed "../../build/locales/zh_CN/LC_MESSAGES/messages.mo"
};
static constexpr size_t zh_CN_mo_size = sizeof(zh_CN_mo_data_raw);
static constexpr bool has_zh_CN = true;
#else
static constexpr unsigned char zh_CN_mo_data_raw[] = {};
static constexpr size_t zh_CN_mo_size = 0;
static constexpr bool has_zh_CN = false;
#endif

// Check if we can embed the en_US locale
#if __has_embed("../../build/locales/en_US/LC_MESSAGES/messages.mo")
alignas(std::byte) static constexpr unsigned char en_US_mo_data_raw[] = {
#embed "../../build/locales/en_US/LC_MESSAGES/messages.mo"
};
static constexpr size_t en_US_mo_size = sizeof(en_US_mo_data_raw);
static constexpr bool has_en_US = true;
#else
static constexpr unsigned char en_US_mo_data_raw[] = {};
static constexpr size_t en_US_mo_size = 0;
static constexpr bool has_en_US = false;
#endif

#else
// Fallback: #embed not supported, use empty arrays
static constexpr unsigned char zh_CN_mo_data_raw[] = {};
static constexpr size_t zh_CN_mo_size = 0;
static constexpr unsigned char en_US_mo_data_raw[] = {};
static constexpr size_t en_US_mo_size = 0;
static constexpr bool has_zh_CN = false;
static constexpr bool has_en_US = false;
#endif

// Static array of embedded locales (constructed at runtime due to
// reinterpret_cast)
static const std::array embedded_locale_list = {
    EmbeddedLocaleData{"zh_CN", std::span{reinterpret_cast<const std::byte *>(
                                              zh_CN_mo_data_raw),
                                          zh_CN_mo_size}},
    EmbeddedLocaleData{"en_US", std::span{reinterpret_cast<const std::byte *>(
                                              en_US_mo_data_raw),
                                          en_US_mo_size}},
};

auto get_embedded_locales() -> std::span<const EmbeddedLocaleData> {
  return embedded_locale_list;
}

} // namespace obcx::common