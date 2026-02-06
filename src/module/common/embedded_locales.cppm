module;

import std;

export module common.embedded_locales;

namespace obcx::common {

/**
 * \if CHINESE
 * @brief 内嵌的语言资源数据
 * \endif
 * \if ENGLISH
 * @brief Embedded locale resource data
 * \endif
 */
struct EmbeddedLocaleData {
  std::string_view locale_name;
  std::span<const std::byte> data;
};

/**
 * \if CHINESE
 * @brief 获取所有内嵌的语言资源
 * @return 语言资源数组的 span
 * \endif
 * \if ENGLISH
 * @brief Get all embedded locale resources
 * @return Span of embedded locale data
 * \endif
 */
auto get_embedded_locales() -> std::span<const EmbeddedLocaleData>;

} // namespace obcx::common
