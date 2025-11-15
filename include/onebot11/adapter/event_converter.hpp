#pragma once

#include "common/message_type.hpp"
#include <optional>
#include <string_view>

namespace obcx::adapter::onebot11 {

/**
 * \~chinese
 * @brief 事件转换器
 *
 * 负责将 OneBot v11 的原始事件 JSON 字符串，
 * 转换为内部的、类型安全的 Event (std::variant) 对象。
 *
 * \~english
 * @brief Event Converter
 *
 * Responsible for converting raw OneBot v11 event JSON strings
 * into internal, type-safe Event (std::variant) objects.
 */
namespace EventConverter {
/**
 * \~chinese
 * @brief 从 v11 的 JSON 字符串解析事件。
 *
 * 该函数会判断 'post_type' 字段，并创建相应类型的事件对象。
 * 对于消息事件，它会调用 MessageConverter 来处理 'message' 字段。
 *
 * @param json_str 从 OneBot v11 实现接收到的原始JSON字符串。
 * @return 如果解析成功，返回一个包含具体事件的 std::optional；
 *         如果JSON无效或不是一个合法的事件，则返回 std::nullopt。
 *
 * \~english
 * @brief Parses an event from a v11 JSON string.
 *
 * This function checks the 'post_type' field and creates the corresponding
 * event object. For message events, it calls MessageConverter to handle the
 * 'message' field.
 *
 * @param json_str The raw JSON string received from the OneBot v11
 * implementation.
 * @return An std::optional containing the specific event if parsing is
 * successful; returns std::nullopt if the JSON is invalid or not a valid
 * event.
 */
auto from_v11_json(std::string_view json_str) -> std::optional<common::Event>;
}; // namespace EventConverter

} // namespace obcx::adapter::onebot11