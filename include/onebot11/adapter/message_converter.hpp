#pragma once

#include "common/message_type.hpp"
#include <string>

namespace obcx::adapter::onebot11 {

/**
 * \~chinese
 * @brief 消息转换器
 *
 * 负责在 OneBot v11 的原始消息字符串（含CQ码）和内部的、
 * 基于 OneBot v12 理念的结构化 Message 对象之间进行双向转换。
 *
 * \~english
 * @brief Message Converter
 *
 * Responsible for bidirectional conversion between raw OneBot v11 message
 * strings (including CQ codes) and internal, structured Message objects based
 * on the OneBot v12 philosophy.
 */
namespace MessageConverter {
/**
 * \~chinese
 * @brief 将包含CQ码的v11原始消息字符串解析为内部的 Message 对象。
 *
 * 例如: "你好[CQ:face,id=123]世界"
 * 将被转换为:
 * [
 *   { "type": "text", "data": { "text": "你好" } },
 *   { "type": "face", "data": { "id": "123" } },
 *   { "type": "text", "data": { "text": "世界" } }
 * ]
 *
 * @param raw_message v11格式的原始消息字符串。
 * @return 转换后的内部 Message 对象。
 *
 * \~english
 * @brief Parses a raw v11 message string with CQ codes into an internal
 * Message object.
 *
 * For example: "你好[CQ:face,id=123]世界"
 * will be converted to:
 * [
 *   { "type": "text", "data": { "text": "你好" } },
 *   { "type": "face", "data": { "id": "123" } },
 *   { "type": "text", "data": { "text": "世界" } }
 * ]
 *
 * @param raw_message The raw message string in v11 format.
 * @return The converted internal Message object.
 */
auto from_v11_string(const std::string &raw_message) -> common::Message;

/**
 * \~chinese
 * @brief 将内部的 Message 对象序列化为v11的CQ码字符串。
 * @param message 内部 Message 对象。
 * @return v11格式的原始消息字符串。
 *
 * \~english
 * @brief Serializes an internal Message object into a v11 CQ code string.
 * @param message The internal Message object.
 * @return The raw message string in v11 format.
 */
auto to_v11_string(const common::Message &message) -> std::string;

auto cq_unescape(std::string s) -> std::string;

auto cq_escape(std::string s) -> std::string;
}; // namespace MessageConverter
} // namespace obcx::adapter::onebot11