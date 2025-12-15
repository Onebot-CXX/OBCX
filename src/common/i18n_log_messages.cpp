#include "common/i18n_log_messages.hpp"
#include "common/embedded_locales.hpp"
#include <boost/locale/gnu_gettext.hpp>
#include <fmt/format.h>
#include <fstream>
#include <sstream>

namespace obcx::common {

I18nLogMessages::MessageMap I18nLogMessages::message_keys_;
std::string I18nLogMessages::current_locale_ = "en_US";
std::string I18nLogMessages::locale_dir_;
bool I18nLogMessages::initialized_ = false;
bool I18nLogMessages::use_embedded_ = true;
std::locale I18nLogMessages::current_locale_obj_;

// 内存中的 .mo 文件数据缓存（用于 callback）
static std::span<const std::byte> cached_mo_data_;

// Boost.Locale callback: 从内存返回 .mo 文件数据
static auto embedded_mo_loader(const std::string & /*file_name*/,
                               const std::string & /*encoding*/)
    -> std::vector<char> {
  if (cached_mo_data_.empty()) {
    return {};
  }
  // 将 std::byte span 转换为 std::vector<char>
  const auto *data_ptr = reinterpret_cast<const char *>(cached_mo_data_.data());
  return std::vector<char>(data_ptr, data_ptr + cached_mo_data_.size());
}

void I18nLogMessages::initialize(bool use_embedded,
                                 const std::string &locale_dir) {
  if (initialized_) {
    return;
  }

  use_embedded_ = use_embedded;

  // Setup message keys mapping
  setup_message_keys();

  if (use_embedded_) {
    initialize_from_embedded();
  } else {
    initialize_from_files(locale_dir);
  }

  initialized_ = true;
}

void I18nLogMessages::initialize_from_embedded() {
  // 使用 Boost.Locale gnu_gettext callback 机制从内存加载 .mo 文件
  try {
    auto embedded_locales = get_embedded_locales();

    // 查找当前 locale 的嵌入数据
    for (const auto &locale_data : embedded_locales) {
      if (locale_data.locale_name == current_locale_ &&
          !locale_data.data.empty()) {
        cached_mo_data_ = locale_data.data;
        break;
      }
    }

    if (cached_mo_data_.empty()) {
      // 没有找到对应的嵌入数据，使用 classic locale
      current_locale_obj_ = std::locale::classic();
      return;
    }

    // 解析 locale 名称（如 "zh_CN" -> language="zh", country="CN"）
    std::string language;
    std::string country;
    auto underscore_pos = current_locale_.find('_');
    if (underscore_pos != std::string::npos) {
      language = current_locale_.substr(0, underscore_pos);
      country = current_locale_.substr(underscore_pos + 1);
      // 移除可能的 .UTF-8 后缀
      auto dot_pos = country.find('.');
      if (dot_pos != std::string::npos) {
        country = country.substr(0, dot_pos);
      }
    } else {
      language = current_locale_;
    }

    // 配置 messages_info
    namespace blg = boost::locale::gnu_gettext;
    blg::messages_info info;
    info.language = language;
    info.country = country;
    info.encoding = "UTF-8";
    info.paths.push_back(""); // 需要一个路径（即使为空）
    info.domains.push_back(blg::messages_info::domain("messages"));
    info.callback = embedded_mo_loader;

    // 创建基础 locale
    boost::locale::generator gen;
    std::string locale_name = current_locale_;
    if (locale_name.find(".UTF-8") == std::string::npos &&
        locale_name.find(".utf8") == std::string::npos) {
      locale_name += ".UTF-8";
    }
    std::locale base_locale = gen(locale_name);

    // 安装消息 facet
    current_locale_obj_ =
        std::locale(base_locale, blg::create_messages_facet<char>(info));
    std::locale::global(current_locale_obj_);
  } catch (const std::exception &) {
    // Fallback: 使用 C locale
    current_locale_obj_ = std::locale::classic();
  }
}

void I18nLogMessages::initialize_from_files(const std::string &locale_dir) {
  // Set locale directory
  if (locale_dir.empty()) {
    // Use build directory for compiled .mo files
    locale_dir_ = "build/locales";
  } else {
    locale_dir_ = locale_dir;
  }

  // Try to initialize Boost.Locale with the locale directory
  try {
    boost::locale::generator gen;
    gen.add_messages_path(locale_dir_);
    gen.add_messages_domain("messages");

    // Try with .UTF-8 suffix
    std::string locale_name = current_locale_;
    if (locale_name.find(".UTF-8") == std::string::npos &&
        locale_name.find(".utf8") == std::string::npos) {
      locale_name += ".UTF-8";
    }

    current_locale_obj_ = gen(locale_name);
    std::locale::global(current_locale_obj_);
  } catch (const std::exception &) {
    // Fallback: just use C locale
    current_locale_obj_ = std::locale::classic();
  }
}

void I18nLogMessages::set_locale(const std::string &locale) {
  if (!initialized_) {
    initialize();
  }
  current_locale_ = locale;

  if (use_embedded_) {
    // 使用 Boost.Locale gnu_gettext callback 机制从内存重新加载 locale
    try {
      auto embedded_locales = get_embedded_locales();

      // 查找新 locale 的嵌入数据
      cached_mo_data_ = {}; // 清空缓存
      for (const auto &locale_data : embedded_locales) {
        if (locale_data.locale_name == locale && !locale_data.data.empty()) {
          cached_mo_data_ = locale_data.data;
          break;
        }
      }

      if (cached_mo_data_.empty()) {
        current_locale_obj_ = std::locale::classic();
        return;
      }

      // 解析 locale 名称
      std::string language;
      std::string country;
      auto underscore_pos = locale.find('_');
      if (underscore_pos != std::string::npos) {
        language = locale.substr(0, underscore_pos);
        country = locale.substr(underscore_pos + 1);
        auto dot_pos = country.find('.');
        if (dot_pos != std::string::npos) {
          country = country.substr(0, dot_pos);
        }
      } else {
        language = locale;
      }

      // 配置 messages_info
      namespace blg = boost::locale::gnu_gettext;
      blg::messages_info info;
      info.language = language;
      info.country = country;
      info.encoding = "UTF-8";
      info.paths.push_back("");
      info.domains.push_back(blg::messages_info::domain("messages"));
      info.callback = embedded_mo_loader;

      // 创建基础 locale
      boost::locale::generator gen;
      std::string locale_name = locale;
      if (locale_name.find(".UTF-8") == std::string::npos &&
          locale_name.find(".utf8") == std::string::npos) {
        locale_name += ".UTF-8";
      }
      std::locale base_locale = gen(locale_name);

      // 安装消息 facet
      current_locale_obj_ =
          std::locale(base_locale, blg::create_messages_facet<char>(info));
      std::locale::global(current_locale_obj_);
    } catch (const std::exception &) {
      current_locale_obj_ = std::locale::classic();
    }
  } else {
    // Try to set Boost.Locale from files
    try {
      boost::locale::generator gen;
      gen.add_messages_path(locale_dir_);
      gen.add_messages_domain("messages");

      // Try with .UTF-8 suffix first, then without
      std::string locale_name = locale;
      if (locale_name.find(".UTF-8") == std::string::npos &&
          locale_name.find(".utf8") == std::string::npos) {
        locale_name += ".UTF-8";
      }

      current_locale_obj_ = gen(locale_name);
      std::locale::global(current_locale_obj_);
    } catch (const std::exception &) {
      // Fallback: just update the current_locale_ string
      current_locale_obj_ = std::locale::classic();
    }
  }
}

std::string I18nLogMessages::get_message(LogMessageKey key) {
  if (!initialized_) {
    initialize();
  }

  auto it = message_keys_.find(key);
  if (it == message_keys_.end()) {
    return "Unknown message key";
  }

  const std::string &msg = it->second;

  // 使用 Boost.Locale 翻译消息（无论是 embedded 还是 file 模式）
  try {
    std::string translated =
        boost::locale::translate(msg).str(current_locale_obj_);
    return translated;
  } catch (const std::exception &) {
    // Fallback: 返回原始英文消息
    return msg;
  }
}

void I18nLogMessages::setup_message_keys() {
  // Fallback English messages for all message keys
  // These are used when .mo files cannot be loaded
  message_keys_[LogMessageKey::LOGGER_INIT_SUCCESS] =
      "Logger initialized successfully";
  message_keys_[LogMessageKey::LOGGER_INIT_FAILED] =
      "Logger initialization failed: {}";
  message_keys_[LogMessageKey::CONNECTION_ESTABLISHED] =
      "Connection established to {}:{}";
  message_keys_[LogMessageKey::CONNECTION_FAILED] = "Connection failed: {}";
  message_keys_[LogMessageKey::CONNECTION_CLOSED] = "Connection closed";
  message_keys_[LogMessageKey::PROXY_CONNECTION_ESTABLISHED] =
      "HTTP connection will be established through {} proxy {}:{} to {}:{}";
  message_keys_[LogMessageKey::PARSING_EVENT] = "Parsing event: {}";
  message_keys_[LogMessageKey::UNHANDLED_UPDATE_TYPE] =
      "Unhandled update type in update";
  message_keys_[LogMessageKey::NO_UPDATE_ID_FIELD] =
      "No update_id field in JSON";
  message_keys_[LogMessageKey::PARSE_ERROR] =
      "Failed to parse event: {}. JSON: {}";
  message_keys_[LogMessageKey::EVENT_PARSED_SUCCESS] =
      "Successfully parsed event";
  message_keys_[LogMessageKey::EVENT_PARSE_FAILED] =
      "Failed to parse event: {}";
  message_keys_[LogMessageKey::EXTRACTED_MESSAGE_ID] =
      "Extracted message_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_USER_ID] = "Extracted user_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_CHAT_ID] = "Extracted chat_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_MESSAGE_TEXT] =
      "Extracted message text: {}";
  message_keys_[LogMessageKey::EXTRACTED_FILE_ID] = "Extracted file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_PHOTO_FILE_ID] =
      "Extracted photo file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_STICKER_FILE_ID] =
      "Extracted sticker file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_VIDEO_FILE_ID] =
      "Extracted video file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_ANIMATION_FILE_ID] =
      "Extracted animation file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_DOCUMENT_FILE_ID] =
      "Extracted document file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_AUDIO_FILE_ID] =
      "Extracted audio file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_VOICE_FILE_ID] =
      "Extracted voice file_id: {}";
  message_keys_[LogMessageKey::EXTRACTED_VIDEO_NOTE_FILE_ID] =
      "Extracted video_note file_id: {}";
  message_keys_[LogMessageKey::CHAT_TYPE] = "Chat type: {}";
  message_keys_[LogMessageKey::SET_GROUP_ID] = "Set group_id: {}";
  message_keys_[LogMessageKey::SET_PRIVATE_CHAT] = "Set private chat";
  message_keys_[LogMessageKey::MARKED_EDIT_MESSAGE] =
      "Marked as edit message: message_id={}";
  message_keys_[LogMessageKey::PARSED_CALLBACK_QUERY] =
      "Successfully parsed callback query event";
  message_keys_[LogMessageKey::PARSE_CALLBACK_QUERY_FAILED] =
      "Failed to parse callback query event: {}";
  message_keys_[LogMessageKey::SEND_MESSAGE_REPLY_ID] =
      "Added reply message ID: {}";
  message_keys_[LogMessageKey::SEND_STICKER_REPLY_ID] =
      "sendSticker added reply message ID: {}";
  message_keys_[LogMessageKey::SEND_VIDEO_REPLY_ID] =
      "sendVideo added reply message ID: {}";
  message_keys_[LogMessageKey::SEND_VIDEO_NOTE_REPLY_ID] =
      "sendVideoNote added reply message ID: {}";
  message_keys_[LogMessageKey::SEND_AUDIO_REPLY_ID] =
      "sendAudio added reply message ID: {}";
  message_keys_[LogMessageKey::SEND_VOICE_REPLY_ID] =
      "sendVoice added reply message ID: {}";
  message_keys_[LogMessageKey::SEND_DOCUMENT_REPLY_ID] =
      "sendDocument added reply message ID: {}";
  message_keys_[LogMessageKey::START_POLLING] = "Start polling, interval: {}ms";
  message_keys_[LogMessageKey::STOP_POLLING] = "Stop polling";
  message_keys_[LogMessageKey::POLLING_FAILED] = "Polling failed: {}";
  message_keys_[LogMessageKey::POLLING_COROUTINE_EXIT] =
      "Polling coroutine exited";
  message_keys_[LogMessageKey::RECEIVED_UPDATES] = "Received updates: {}";
  message_keys_[LogMessageKey::PROCESSING_UPDATES] = "Processing {} updates";
  message_keys_[LogMessageKey::PROCESSING_UPDATE] =
      "Processing single update: {}";
  message_keys_[LogMessageKey::DISPATCHING_EVENT] = "Dispatching event";
  message_keys_[LogMessageKey::FAILED_PARSE_EVENT] =
      "Failed to parse event from update";
  message_keys_[LogMessageKey::EVENT_CALLBACK_NOT_SET] =
      "Event callback not set";
  message_keys_[LogMessageKey::SENDING_ACTION] = "Sending action: {}";
  message_keys_[LogMessageKey::API_REQUEST_FAILED] = "API request failed: {}";
  message_keys_[LogMessageKey::DOWNLOAD_FILE_FAILED] =
      "Download file failed: {}";
  message_keys_[LogMessageKey::DOWNLOAD_FILE_CONTENT_FAILED] =
      "Download file content failed: {}";
  message_keys_[LogMessageKey::PLUGIN_LOADED] = "Plugin loaded: {}";
  message_keys_[LogMessageKey::PLUGIN_LOAD_FAILED] = "Plugin load failed: {}";
  message_keys_[LogMessageKey::PLUGIN_INITIALIZED] = "Plugin initialized: {}";
  message_keys_[LogMessageKey::PLUGIN_INIT_FAILED] =
      "Plugin initialization failed: {}";
  message_keys_[LogMessageKey::MESSAGE_CONVERSION_START] =
      "Message conversion start";
  message_keys_[LogMessageKey::MESSAGE_CONVERSION_SUCCESS] =
      "Message conversion success";
  message_keys_[LogMessageKey::MESSAGE_CONVERSION_FAILED] =
      "Message conversion failed: {}";
  message_keys_[LogMessageKey::CONFIG_LOADING] = "Loading config: {}";
  message_keys_[LogMessageKey::CONFIG_LOADED_SUCCESS] =
      "Config loaded successfully";
  message_keys_[LogMessageKey::CONFIG_LOAD_FAILED] = "Config load failed: {}";
  message_keys_[LogMessageKey::DATABASE_CONNECTION_FAILED] =
      "Database connection failed: {}";
  message_keys_[LogMessageKey::DATABASE_QUERY_FAILED] =
      "Database query failed: {}";
  message_keys_[LogMessageKey::BOT_STARTED] = "Bot started";
  message_keys_[LogMessageKey::BOT_STOPPED] = "Bot stopped";
  message_keys_[LogMessageKey::BOT_ERROR] = "Bot error: {}";

  // Task scheduler messages
  message_keys_[LogMessageKey::TASK_SCHEDULER_CREATED] =
      "TaskScheduler created with thread pool size: {}";
  message_keys_[LogMessageKey::TASK_SCHEDULER_STOPPING] =
      "Stopping TaskScheduler...";
  message_keys_[LogMessageKey::TASK_SCHEDULER_STOPPED] =
      "TaskScheduler stopped";

  // Bot initialization messages
  message_keys_[LogMessageKey::QQBOT_INSTANCE_CREATED] =
      "QQBot instance created, all core components initialized";
  message_keys_[LogMessageKey::QQBOT_STARTING_EVENT_LOOP] =
      "QQBot starting event loop...";
  message_keys_[LogMessageKey::QQBOT_EVENT_LOOP_ENDED] =
      "QQBot event loop ended";
  message_keys_[LogMessageKey::QQBOT_REQUESTING_STOP] =
      "Requesting QQBot to stop...";
  message_keys_[LogMessageKey::TELEGRAMBOT_INSTANCE_CREATED] =
      "TelegramBot instance created, all core components initialized";
  message_keys_[LogMessageKey::TELEGRAMBOT_STARTING_EVENT_LOOP] =
      "TelegramBot starting event loop...";
  message_keys_[LogMessageKey::TELEGRAMBOT_EVENT_LOOP_ENDED] =
      "TelegramBot event loop ended";
  message_keys_[LogMessageKey::TELEGRAMBOT_REQUESTING_STOP] =
      "Requesting TelegramBot to stop...";

  // Connection type messages
  message_keys_[LogMessageKey::CONNECTING_WITH_TYPE] =
      "Connecting to {}:{} using {} connection type";

  // Network connection messages
  message_keys_[LogMessageKey::HTTP_CONNECTION_ESTABLISHED] =
      "HTTP connection established to {}:{}";
  message_keys_[LogMessageKey::WEBSOCKET_ATTEMPTING_CONNECTION] =
      "Attempting to connect to ws://{}:{}";
  message_keys_[LogMessageKey::WEBSOCKET_CONNECTION_ESTABLISHED] =
      "WebSocket connection established";
  message_keys_[LogMessageKey::WEBSOCKET_CONNECTED_SUCCESSFULLY] =
      "WebSocket connected successfully to ws://{}:{}";

  // Message sending messages
  message_keys_[LogMessageKey::MESSAGE_SENT_SUCCESSFULLY] =
      "Message sent successfully: {}";

  message_keys_[LogMessageKey::OPERATION_SUCCESS] = "Operation success";
  message_keys_[LogMessageKey::OPERATION_FAILED] = "Operation failed: {}";
  message_keys_[LogMessageKey::INVALID_PARAMETER] = "Invalid parameter: {}";
}

} // namespace obcx::common