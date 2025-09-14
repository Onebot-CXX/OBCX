#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace obcx::common {

using json = nlohmann::json;

/**
 * \~chinese
 * @brief JSON工具类，提供便利的JSON操作方法
 *
 * \~english
 * @brief JSON utility class, providing convenient JSON manipulation methods.
 */
namespace JsonUtils {
/**
 * \~chinese
 * @brief 安全地从JSON中获取值
 * @tparam T 值类型
 * @param j JSON对象
 * @param key 键名
 * @param default_value 默认值
 * @return 获取的值或默认值
 *
 * \~english
 * @brief Safely gets a value from a JSON object.
 * @tparam T The value type.
 * @param j The JSON object.
 * @param key The key name.
 * @param default_value The default value.
 * @return The retrieved value or the default value.
 */
template <typename T>
auto get_value(const json &j, const std::string &key,
               const T &default_value = T{}) -> T {
  if (j.contains(key) && !j[key].is_null()) {
    try {
      return j[key].get<T>();
    } catch (const json::exception &) {
      return default_value;
    }
  }
  return default_value;
}

/**
 * \~chinese
 * @brief 安全地从JSON中获取可选值
 * @tparam T 值类型
 * @param j JSON对象
 * @param key 键名
 * @return std::optional包装的值
 *
 * \~english
 * @brief Safely gets an optional value from a JSON object.
 * @tparam T The value type.
 * @param j The JSON object.
 * @param key The key name.
 * @return An std::optional-wrapped value.
 */
template <typename T>
std::optional<T> get_optional(const json &j, const std::string &key) {
  if (j.contains(key) && !j[key].is_null()) {
    try {
      return j[key].get<T>();
    } catch (const json::exception &) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

/**
 * \~chinese
 * @brief 从JSON中安全地获取一个ID字段，该ID可能是字符串或数字。
 *
 * @param j The json object.
 * @param key The key for the ID field.
 * @return std::string The ID as a string. Returns empty string if not found or
 * not a string/number.
 *
 * \~english
 * @brief Safely gets an ID field from JSON, which could be a string or a
 * number.
 *
 * @param j The json object.
 * @param key The key for the ID field.
 * @return std::string The ID as a string. Returns empty string if not found or
 * not a string/number.
 */
auto get_id_as_string(const json &j, const std::string &key) -> std::string;

/**
 * \~chinese
 * @brief 从JSON中安全地获取一个可选的ID字段，该ID可能是字符串或数字。
 *
 * @param j The json object.
 * @param key The key for the ID field.
 * @return std::optional<std::string> The ID as a string if present, otherwise
 * std::nullopt.
 *
 * \~english
 * @brief Safely gets an optional ID field from JSON, which could be a string or
 * a number.
 *
 * @param j The json object.
 * @param key The key for the ID field.
 * @return std::optional<std::string> The ID as a string if present, otherwise
 * std::nullopt.
 */
auto get_optional_id_as_string(const json &j, const std::string &key)
    -> std::optional<std::string>;

/**
 * \~chinese
 * @brief 安全地设置JSON值
 * @tparam T 值类型
 * @param j JSON对象
 * @param key 键名
 * @param value 值
 *
 * \~english
 * @brief Safely sets a JSON value.
 * @tparam T The value type.
 * @param j The JSON object.
 * @param key The key name.
 * @param value The value.
 */
template <typename T>
void set_value(json &j, const std::string &key, const T &value) {
  j[key] = value;
}

/**
 * \~chinese
 * @brief 安全地设置可选JSON值
 * @tparam T 值类型
 * @param j JSON对象
 * @param key 键名
 * @param value 可选值
 *
 * \~english
 * @brief Safely sets an optional JSON value.
 * @tparam T The value type.
 * @param j The JSON object.
 * @param key The key name.
 * @param value The optional value.
 */
template <typename T>
void set_optional(json &j, const std::string &key,
                  const std::optional<T> &value) {
  if (value.has_value()) {
    j[key] = value.value();
  }
}

/**
 * \~chinese
 * @brief 验证JSON是否包含必需的字段
 * @param j JSON对象
 * @param required_fields 必需字段列表
 * @return 是否包含所有必需字段
 *
 * \~english
 * @brief Validates if the JSON object contains the required fields.
 * @param j The JSON object.
 * @param required_fields A list of required fields.
 * @return True if all required fields are present, false otherwise.
 */
bool validate_required_fields(const json &j,
                              const std::vector<std::string> &required_fields);

/**
 * \~chinese
 * @brief 合并两个JSON对象
 * @param target 目标JSON对象
 * @param source 源JSON对象
 * @param overwrite 是否覆盖已存在的字段
 *
 * \~english
 * @brief Merges two JSON objects.
 * @param target The target JSON object.
 * @param source The source JSON object.
 * @param overwrite Whether to overwrite existing fields.
 */
void merge(json &target, const json &source, bool overwrite = true);

/**
 * \~chinese
 * @brief 美化JSON字符串输出
 * @param j JSON对象
 * @param indent 缩进空格数
 * @return 格式化的JSON字符串
 *
 * \~english
 * @brief Pretty-prints a JSON string.
 * @param j The JSON object.
 * @param indent The number of spaces for indentation.
 * @return The formatted JSON string.
 */
auto pretty_print(const json &j, int indent = 2) -> std::string;

/**
 * \~chinese
 * @brief 安全地解析JSON字符串
 * @param str JSON字符串
 * @return 解析结果的可选值
 *
 * \~english
 * @brief Safely parses a JSON string.
 * @param str The JSON string.
 * @return An optional containing the parsed result.
 */
std::optional<json> parse(const std::string &str);

/**
 * \~chinese
 * @brief 检查JSON路径是否存在
 * @param j JSON对象
 * @param path JSON指针路径 (如 "/data/message")
 * @return 是否存在
 *
 * \~english
 * @brief Checks if a JSON path exists.
 * @param j The JSON object.
 * @param path The JSON pointer path (e.g., "/data/message").
 * @return True if the path exists, false otherwise.
 */
auto has_path(const json &j, const std::string &path) -> bool;

/**
 * \~chinese
 * @brief 通过路径获取JSON值
 * @tparam T 值类型
 * @param j JSON对象
 * @param path JSON指针路径
 * @param default_value 默认值
 * @return 获取的值或默认值
 *
 * \~english
 * @brief Gets a JSON value by path.
 * @tparam T The value type.
 * @param j The JSON object.
 * @param path The JSON pointer path.
 * @param default_value The default value.
 * @return The retrieved value or the default value.
 */
template <typename T>
auto get_by_path(const json &j, const std::string &path,
                 const T &default_value = T{}) -> T {
  try {
    auto ptr = json::json_pointer(path);
    if (j.contains(ptr)) {
      return j[ptr].get<T>();
    }
  } catch (const json::exception &) {
    // \~chinese 忽略路径错误
    // \~english Ignore path errors
  }
  return default_value;
}
} // namespace JsonUtils

} // namespace obcx::common
