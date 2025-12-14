#pragma once

#include <boost/locale.hpp>
#include <fmt/format.h>
#include <map>
#include <string>

namespace obcx::common {

/**
 * \if CHINESE
 * @brief 日志消息枚举，用于多语言支持
 * \endif
 * \if ENGLISH
 * @brief Log message enumeration for multilingual support
 * \endif
 */
enum class LogMessageKey {
  // Logger initialization
  LOGGER_INIT_SUCCESS,
  LOGGER_INIT_FAILED,

  // Connection manager messages
  CONNECTION_ESTABLISHED,
  CONNECTION_FAILED,
  CONNECTION_CLOSED,
  PROXY_CONNECTION_ESTABLISHED,

  // Protocol adapter messages
  PARSING_EVENT,
  UNHANDLED_UPDATE_TYPE,
  NO_UPDATE_ID_FIELD,
  PARSE_ERROR,
  EVENT_PARSED_SUCCESS,
  EVENT_PARSE_FAILED,

  // Message extraction messages
  EXTRACTED_MESSAGE_ID,
  EXTRACTED_USER_ID,
  EXTRACTED_CHAT_ID,
  EXTRACTED_MESSAGE_TEXT,
  EXTRACTED_FILE_ID,
  EXTRACTED_PHOTO_FILE_ID,
  EXTRACTED_STICKER_FILE_ID,
  EXTRACTED_VIDEO_FILE_ID,
  EXTRACTED_ANIMATION_FILE_ID,
  EXTRACTED_DOCUMENT_FILE_ID,
  EXTRACTED_AUDIO_FILE_ID,
  EXTRACTED_VOICE_FILE_ID,
  EXTRACTED_VIDEO_NOTE_FILE_ID,

  // Chat type messages
  CHAT_TYPE,
  SET_GROUP_ID,
  SET_PRIVATE_CHAT,

  // Message type specific messages
  MARKED_EDIT_MESSAGE,
  PARSED_CALLBACK_QUERY,
  PARSE_CALLBACK_QUERY_FAILED,

  // Media and attachment messages
  SEND_MESSAGE_REPLY_ID,
  SEND_STICKER_REPLY_ID,
  SEND_VIDEO_REPLY_ID,
  SEND_VIDEO_NOTE_REPLY_ID,
  SEND_AUDIO_REPLY_ID,
  SEND_VOICE_REPLY_ID,
  SEND_DOCUMENT_REPLY_ID,

  // Polling messages
  START_POLLING,
  STOP_POLLING,
  POLLING_FAILED,
  POLLING_COROUTINE_EXIT,
  RECEIVED_UPDATES,
  PROCESSING_UPDATES,
  PROCESSING_UPDATE,
  DISPATCHING_EVENT,
  FAILED_PARSE_EVENT,
  EVENT_CALLBACK_NOT_SET,

  // HTTP request messages
  SENDING_ACTION,
  API_REQUEST_FAILED,
  DOWNLOAD_FILE_FAILED,
  DOWNLOAD_FILE_CONTENT_FAILED,

  // Plugin messages
  PLUGIN_LOADED,
  PLUGIN_LOAD_FAILED,
  PLUGIN_INITIALIZED,
  PLUGIN_INIT_FAILED,

  // Message conversion messages
  MESSAGE_CONVERSION_START,
  MESSAGE_CONVERSION_SUCCESS,
  MESSAGE_CONVERSION_FAILED,

  // Config loader messages
  CONFIG_LOADING,
  CONFIG_LOADED_SUCCESS,
  CONFIG_LOAD_FAILED,

  // Database messages
  DATABASE_CONNECTION_FAILED,
  DATABASE_QUERY_FAILED,

  // Bot messages
  BOT_STARTED,
  BOT_STOPPED,
  BOT_ERROR,

  // Task scheduler messages
  TASK_SCHEDULER_CREATED,
  TASK_SCHEDULER_STOPPING,
  TASK_SCHEDULER_STOPPED,

  // Bot initialization messages
  QQBOT_INSTANCE_CREATED,
  QQBOT_STARTING_EVENT_LOOP,
  QQBOT_EVENT_LOOP_ENDED,
  QQBOT_REQUESTING_STOP,
  TELEGRAMBOT_INSTANCE_CREATED,
  TELEGRAMBOT_STARTING_EVENT_LOOP,
  TELEGRAMBOT_EVENT_LOOP_ENDED,
  TELEGRAMBOT_REQUESTING_STOP,

  // Connection type messages
  CONNECTING_WITH_TYPE,

  // Network connection messages
  HTTP_CONNECTION_ESTABLISHED,
  WEBSOCKET_ATTEMPTING_CONNECTION,
  WEBSOCKET_CONNECTION_ESTABLISHED,
  WEBSOCKET_CONNECTED_SUCCESSFULLY,

  // Message sending messages
  MESSAGE_SENT_SUCCESSFULLY,

  // Generic messages
  OPERATION_SUCCESS,
  OPERATION_FAILED,
  INVALID_PARAMETER,
};

/**
 * \if CHINESE
 * @brief 国际化日志消息管理类
 * \endif
 * \if ENGLISH
 * @brief Internationalized logging message manager
 * \endif
 */
class I18nLogMessages {
public:
  /**
   * \if CHINESE
   * @brief 初始化消息映射表
   * @param locale_dir 国际化文件目录路径
   * \endif
   * \if ENGLISH
   * @brief Initialize message mappings
   * @param locale_dir Path to the i18n directory containing .mo files
   * \endif
   */
  static void initialize(const std::string &locale_dir = "");

  /**
   * \if CHINESE
   * @brief 设置当前语言环境
   * @param locale 语言环境字符串，如 "zh_CN" 或 "en_US"
   * \endif
   * \if ENGLISH
   * @brief Set the current locale
   * @param locale Locale string, e.g., "zh_CN" or "en_US"
   * \endif
   */
  static void set_locale(const std::string &locale);

  /**
   * \if CHINESE
   * @brief 获取指定日志消息的翻译文本
   * @param key 日志消息键
   * @return 翻译后的文本
   * \endif
   * \if ENGLISH
   * @brief Get the translated text for a log message
   * @param key The log message key
   * @return The translated text
   * \endif
   */
  static auto get_message(LogMessageKey key) -> std::string;

  /**
   * \if CHINESE
   * @brief 获取格式化后的翻译文本
   * @param key 日志消息键
   * @param args 格式化参数
   * @return 格式化后的翻译文本
   * \endif
   * \if ENGLISH
   * @brief Get formatted translated text
   * @param key The log message key
   * @param args Format arguments
   * @return The formatted translated text
   * \endif
   */
  template <typename... Args>
  static auto format_message(LogMessageKey key, Args &&...args) -> std::string {
    std::string msg = get_message(key);
    try {
      return fmt::format(fmt::runtime(msg), std::forward<Args>(args)...);
    } catch (const std::exception &) {
      return msg;
    }
  }

private:
  using MessageMap = std::map<LogMessageKey, std::string>;
  static MessageMap message_keys_;
  static std::string current_locale_;
  static std::string locale_dir_;
  static bool initialized_;
  static std::locale current_locale_obj_;

  static void setup_message_keys();
};

} // namespace obcx::common