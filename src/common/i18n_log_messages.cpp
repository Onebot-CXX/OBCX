#include "common/i18n_log_messages.hpp"
#include "common/embedded_locales.hpp"
#include <boost/locale/gnu_gettext.hpp>
#include <fstream>

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
  message_keys_[LogMessageKey::TASK_SCHEDULER_DESTROYED] =
      "TaskScheduler destroyed";
  message_keys_[LogMessageKey::TASK_SCHEDULER_SUBMIT_HEAVY_TASK] =
      "TaskScheduler: Submitting heavy task to thread pool (thread ID: {})";
  message_keys_[LogMessageKey::TASK_SCHEDULER_HEAVY_TASK_START] =
      "TaskScheduler: Starting heavy task execution (worker thread ID: {})";
  message_keys_[LogMessageKey::TASK_SCHEDULER_HEAVY_TASK_COMPLETE_VOID] =
      "TaskScheduler: Heavy task completed (no return value)";
  message_keys_[LogMessageKey::TASK_SCHEDULER_HEAVY_TASK_COMPLETE_RESULT] =
      "TaskScheduler: Heavy task completed, returning result";
  message_keys_[LogMessageKey::TASK_SCHEDULER_HEAVY_TASK_EXCEPTION] =
      "TaskScheduler: Exception occurred during heavy task execution";
  message_keys_[LogMessageKey::TASK_SCHEDULER_BATCH_START] =
      "TaskScheduler: Starting batch execution of {} heavy tasks";
  message_keys_[LogMessageKey::TASK_SCHEDULER_BATCH_COMPLETE] =
      "TaskScheduler: Batch task execution completed";
  message_keys_[LogMessageKey::TASK_SCHEDULER_TIMEOUT_TASK_START] =
      "TaskScheduler: Executing heavy task with timeout (timeout: {}ms)";
  message_keys_[LogMessageKey::TASK_SCHEDULER_TIMEOUT_TASK_FAILED] =
      "TaskScheduler: Timeout task execution failed: {}";

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
  message_keys_[LogMessageKey::QQBOT_INSTANCE_DESTROYED] =
      "QQBot instance destroyed.";
  message_keys_[LogMessageKey::TELEGRAMBOT_INSTANCE_DESTROYED] =
      "TelegramBot instance destroyed.";
  message_keys_[LogMessageKey::BOT_NOT_CONNECTED] =
      "Bot not connected, please call connect* methods first";
  message_keys_[LogMessageKey::EVENT_DISPATCHER_NOT_INITIALIZED] =
      "Event dispatcher not initialized, cannot dispatch error event";
  message_keys_[LogMessageKey::TELEGRAMBOT_ONLY_SUPPORT_HTTP] =
      "Telegram Bot only support TelegramHTTP";
  message_keys_[LogMessageKey::TELEGRAMBOT_RECEIVED_UPDATES] =
      "Received {} updates from Telegram";
  message_keys_[LogMessageKey::TELEGRAMBOT_POLLING_ERROR] =
      "Error polling updates: {}";
  message_keys_[LogMessageKey::TELEGRAMBOT_FUNCTION_NOT_IMPLEMENTED] =
      "TelegramBot::{} not implemented yet";
  message_keys_[LogMessageKey::TELEGRAMBOT_PHOTO_CONTENT] =
      "Photo object content: {}";
  message_keys_[LogMessageKey::TELEGRAMBOT_FILE_UNIQUE_ID_EXTRACTED] =
      "Extracted file_unique_id: '{}' (empty: {})";
  message_keys_[LogMessageKey::TELEGRAMBOT_MEDIA_CONTENT] =
      "{} object content: {}";
  message_keys_[LogMessageKey::TELEGRAMBOT_MEDIA_FILE_UNIQUE_ID_EXTRACTED] =
      "{} extracted file_unique_id: '{}' (empty: {})";
  message_keys_[LogMessageKey::TELEGRAMBOT_EXTRACT_MEDIA_ERROR] =
      "Error extracting media files: {}";
  message_keys_[LogMessageKey::TELEGRAMBOT_INVALID_CONNECTION_MANAGER] =
      "ConnectionManager is not TelegramConnectionManager type";
  message_keys_[LogMessageKey::TELEGRAMBOT_GET_DOWNLOAD_URL_FAILED] =
      "Failed to get media download url (file_id: {}, type: {}): {}";

  // Media Converter messages
  message_keys_[LogMessageKey::MEDIA_CONVERT_WEBM_TO_GIF_START] =
      "Start converting WebM to GIF: {} -> {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_EXECUTE_FFMPEG] =
      "Executing FFmpeg command: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_WEBM_TO_GIF_SUCCESS] =
      "WebM to GIF conversion success, output size: {} bytes";
  message_keys_[LogMessageKey::MEDIA_CONVERT_WEBM_TO_GIF_FAILED] =
      "WebM to GIF conversion failed or invalid output file";
  message_keys_[LogMessageKey::MEDIA_CONVERT_WEBM_TO_GIF_EXCEPTION] =
      "WebM to GIF conversion exception: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_WEBM_TO_GIF_FALLBACK_START] =
      "Start WebM to GIF conversion with fallback: {} -> {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TRY_LOSSLESS] =
      "Trying lossless conversion...";
  message_keys_[LogMessageKey::MEDIA_CONVERT_LOSSLESS_SUCCESS] =
      "Lossless WebM to GIF conversion success";
  message_keys_[LogMessageKey::MEDIA_CONVERT_LOSSLESS_FAILED] =
      "Lossless conversion failed, trying compressed conversion...";
  message_keys_[LogMessageKey::MEDIA_CONVERT_COMPRESSED_SUCCESS] =
      "Compressed WebM to GIF conversion success (320px)";
  message_keys_[LogMessageKey::MEDIA_CONVERT_ALL_FAILED] =
      "WebM to GIF conversion completely failed, need fallback to text/emoji";
  message_keys_[LogMessageKey::MEDIA_CONVERT_FALLBACK_EXCEPTION] =
      "WebM to GIF conversion with fallback exception: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TGS_TO_GIF_START] =
      "Start converting TGS to GIF: {} -> {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_EXECUTE_TGS] =
      "Executing TGS conversion command: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TGS_TO_GIF_SUCCESS] =
      "TGS to GIF conversion success, output size: {} bytes";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TGS_TO_GIF_FAILED] =
      "TGS to GIF conversion failed, lottie-convert might be missing";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TGS_TO_GIF_EXCEPTION] =
      "TGS to GIF conversion exception: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_DOCKER_PATH] =
      "Generated Docker shared path: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TEMP_PATH] =
      "Generated temp path: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_TEMP_PATH_FAILED] =
      "Failed to generate temp path: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_CLEANUP] =
      "Cleaning up temp file: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_CLEANUP_FAILED] =
      "Failed to cleanup temp file: {} - {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_EXECUTE_CMD] =
      "Executing system command: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_CMD_SUCCESS] =
      "Command executed successfully";
  message_keys_[LogMessageKey::MEDIA_CONVERT_CMD_FAILED] =
      "Command failed with return code: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_EXECUTE_CMD_EXCEPTION] =
      "System command execution exception: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_FILE_NOT_EXIST] =
      "File does not exist: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_FILE_EMPTY] = "File is empty: {}";
  message_keys_[LogMessageKey::MEDIA_CONVERT_FILE_VALID] =
      "File is valid: {} ({}bytes)";
  message_keys_[LogMessageKey::MEDIA_CONVERT_CHECK_FILE_EXCEPTION] =
      "Check file validity exception: {} - {}";

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
  message_keys_[LogMessageKey::WEBSOCKET_RUN_ERROR] = "WebSocket run error: {}";
  message_keys_[LogMessageKey::WEBSOCKET_UNHANDLED_EXCEPTION] =
      "WebSocket caught unhandled exception: {}";
  message_keys_[LogMessageKey::WEBSOCKET_CONNECTION_CLOSED] =
      "WebSocket connection closed.";
  message_keys_[LogMessageKey::WEBSOCKET_NOT_CONNECTED] =
      "WebSocket not connected, cannot send message.";
  message_keys_[LogMessageKey::WEBSOCKET_WRITE_WAIT_ERROR] =
      "Error while waiting for write completion: {}";
  message_keys_[LogMessageKey::WEBSOCKET_CLOSE_ERROR] =
      "WebSocket close error: {}";
  message_keys_[LogMessageKey::WEBSOCKET_CLOSE_EXCEPTION] =
      "WebSocket caught unhandled exception during close: {}";
  message_keys_[LogMessageKey::WEBSOCKET_WRITE_ERROR] =
      "Error writing message: {}";

  // Message sending messages
  message_keys_[LogMessageKey::MESSAGE_SENT_SUCCESSFULLY] =
      "Message sent successfully: {}";

  message_keys_[LogMessageKey::OPERATION_SUCCESS] = "Operation success";
  message_keys_[LogMessageKey::OPERATION_FAILED] = "Operation failed: {}";
  message_keys_[LogMessageKey::INVALID_PARAMETER] = "Invalid parameter: {}";

  // Telegram Connection Manager
  message_keys_[LogMessageKey::TELEGRAMBOT_UPDATE_PARSE_ERROR] =
      "Failed to parse update JSON: {}";

  // JSON Utils
  message_keys_[LogMessageKey::JSON_PRETTY_PRINT_FAILED] =
      "Failed to pretty print JSON: {}";
  message_keys_[LogMessageKey::JSON_PARSE_FAILED] = "Failed to parse JSON: {}";

  // Config Loader
  message_keys_[LogMessageKey::CONFIG_LOAD_SUCCESS] =
      "Config loaded successfully from: {}";
  message_keys_[LogMessageKey::CONFIG_PARSE_FAILED] =
      "Failed to parse config file {}: {}";
  message_keys_[LogMessageKey::CONFIG_LOAD_ERROR] =
      "Failed to load config file {}: {}";
  message_keys_[LogMessageKey::CONFIG_LOADED_FROM] =
      "Configuration loaded from: {}";

  // Plugin Manager
  message_keys_[LogMessageKey::PLUGIN_DIR_ADDED] = "Added plugin directory: {}";
  message_keys_[LogMessageKey::PLUGIN_DIR_NOT_EXIST] =
      "Plugin directory does not exist: {}";
  message_keys_[LogMessageKey::PLUGIN_ALREADY_LOADED] =
      "Plugin {} is already loaded";
  message_keys_[LogMessageKey::PLUGIN_NOT_FOUND_IN_DIRS] =
      "Plugin {} not found in plugin directories";
  message_keys_[LogMessageKey::PLUGIN_LOAD_SUCCESS] =
      "Plugin {} loaded successfully from {}";
  message_keys_[LogMessageKey::PLUGIN_UNLOADED] = "Plugin {} unloaded";
  message_keys_[LogMessageKey::ALL_PLUGINS_UNLOADED] = "All plugins unloaded";
  message_keys_[LogMessageKey::PLUGIN_NOT_FOUND] = "Plugin {} not found";
  message_keys_[LogMessageKey::PLUGIN_DEINIT_SUCCESS] =
      "Plugin {} deinitialized successfully";
  message_keys_[LogMessageKey::PLUGIN_DEINIT_FAILED] =
      "Plugin {} failed to deinitialize: {}";
  message_keys_[LogMessageKey::PLUGIN_INIT_SUCCESS] =
      "Plugin {} initialized successfully";
  message_keys_[LogMessageKey::PLUGIN_INIT_FAILED_MSG] =
      "Plugin {} failed to initialize";
  message_keys_[LogMessageKey::PLUGIN_INIT_EXCEPTION] =
      "Exception during plugin {} initialization: {}";
  message_keys_[LogMessageKey::PLUGIN_SHUTDOWN_SUCCESS] =
      "Plugin {} shutdown successfully";
  message_keys_[LogMessageKey::PLUGIN_SHUTDOWN_EXCEPTION] =
      "Exception during plugin {} shutdown: {}";
  message_keys_[LogMessageKey::PLUGIN_LIB_LOAD_FAILED] =
      "Failed to load plugin library {}: {}";
  message_keys_[LogMessageKey::PLUGIN_SYMBOL_CREATE_FAILED] =
      "Failed to load obcx_create_plugin symbol from {}: {}";
  message_keys_[LogMessageKey::PLUGIN_SYMBOL_DESTROY_FAILED] =
      "Failed to load obcx_destroy_plugin symbol from {}: {}";
  message_keys_[LogMessageKey::PLUGIN_CREATE_NULL] =
      "obcx_create_plugin returned nullptr for {}";
  message_keys_[LogMessageKey::PLUGIN_CREATE_EXCEPTION] =
      "Exception during plugin creation from {}: {}";

  // Main / Framework
  message_keys_[LogMessageKey::SHUTDOWN_IN_PROGRESS] =
      "Shutdown already in progress, ignoring signal {}";
  message_keys_[LogMessageKey::SHUTDOWN_SIGNAL_RECEIVED] =
      "Received signal {}, shutting down gracefully...";
  message_keys_[LogMessageKey::UNKNOWN_BOT_TYPE] = "Unknown bot type: {}";
  message_keys_[LogMessageKey::UNKNOWN_CONNECTION_TYPE] =
      "Unknown connection type: {} for bot type: {}";
  message_keys_[LogMessageKey::PROXY_CONFIG_INFO] =
      "Proxy config - Host: '{}', Port: {}, Type: '{}'";
  message_keys_[LogMessageKey::PLUGIN_LOAD_WARN] = "Failed to load plugin: {}";
  message_keys_[LogMessageKey::PLUGIN_INIT_WARN] =
      "Failed to initialize plugin: {}";
  message_keys_[LogMessageKey::BOT_SETUP_SUCCESS] =
      "Bot component setup completed successfully";
  message_keys_[LogMessageKey::BOT_SETUP_FAILED] =
      "Failed to setup bot component: {}";
  message_keys_[LogMessageKey::LOG_LOCALE_SET] = "Log locale set to: {}";
  message_keys_[LogMessageKey::FRAMEWORK_STARTING] =
      "OBCX Robot Framework starting...";
  message_keys_[LogMessageKey::NO_BOT_CONFIGS] = "No bot configurations found";
  message_keys_[LogMessageKey::SKIPPING_DISABLED_BOT] =
      "Skipping disabled bot component of type: {}";
  message_keys_[LogMessageKey::BOT_CREATE_FAILED] =
      "Failed to create bot component of type: {}";
  message_keys_[LogMessageKey::BOT_SETUP_FAILED_TYPE] =
      "Failed to setup bot component of type: {}";
  message_keys_[LogMessageKey::STARTING_BOT] =
      "Starting bot component of type: {}";
  message_keys_[LogMessageKey::BOT_RUNTIME_ERROR] =
      "Bot component runtime error: {}";
  message_keys_[LogMessageKey::NO_BOTS_STARTED] =
      "No bot components started successfully";
  message_keys_[LogMessageKey::ALL_COMPONENTS_STARTED] =
      "All components started successfully. OBCX Framework running...";
  message_keys_[LogMessageKey::FRAMEWORK_SHUTDOWN] =
      "Shutting down OBCX Framework...";
  message_keys_[LogMessageKey::WAITING_BOT_THREAD] =
      "Waiting for bot thread {} to finish...";
  message_keys_[LogMessageKey::BOT_THREAD_TIMEOUT] =
      "Bot thread {} did not finish within timeout, detaching";
  message_keys_[LogMessageKey::FRAMEWORK_SHUTDOWN_COMPLETE] =
      "OBCX Framework shutdown complete";

  // Proxy HTTP Client
  message_keys_[LogMessageKey::PROXY_CLIENT_CREATED] =
      "ProxyHttpClient created, proxy: {}:{} -> target: {}:{}";
  message_keys_[LogMessageKey::PROXY_POST_FAILED] =
      "ProxyHttpClient POST request failed: {}";
  message_keys_[LogMessageKey::PROXY_GET_FAILED] =
      "ProxyHttpClient GET request failed: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_SNI_FAILED] =
      "Failed to set SNI for HTTPS proxy: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_SSL_SUCCESS] =
      "HTTPS proxy SSL connection established";
  message_keys_[LogMessageKey::PROXY_RESPONSE_HEADER] = "Response header: {}";
  message_keys_[LogMessageKey::PROXY_TARGET_SNI_FAILED] =
      "Failed to set SNI for target: {}";
  message_keys_[LogMessageKey::PROXY_SSL_HANDSHAKE_SUCCESS] =
      "SSL handshake success (retry {})";
  message_keys_[LogMessageKey::PROXY_SSL_HANDSHAKE_FAILED_RETRY] =
      "SSL handshake failed (retry {}/{}): {}";
  message_keys_[LogMessageKey::PROXY_RETRY_WAIT] = "Waiting {}ms before retry";
  message_keys_[LogMessageKey::PROXY_STREAM_TRUNCATED] =
      "Stream truncated error detected, might need to rebuild tunnel";
  message_keys_[LogMessageKey::PROXY_HTTPS_CONNECT_SEND] =
      "Sending CONNECT request via HTTPS proxy: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_CONNECT_ERROR] =
      "HTTPS proxy CONNECT response: {}, status: {}, content: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_TUNNEL_SUCCESS] =
      "HTTPS proxy tunnel established: {}:{} -> {}:{}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_TUNNEL_START] =
      "Establishing SOCKS5 tunnel: {} -> {}:{}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_TUNNEL_SUCCESS] =
      "SOCKS5 tunnel established: {}:{} -> {}:{}";

  // HTTP Client
  message_keys_[LogMessageKey::HTTP_CLIENT_INIT] =
      "HTTP Client initialized for {}:{}";
  message_keys_[LogMessageKey::HTTP_CLIENT_CLOSED] = "HTTP Client closed";
  message_keys_[LogMessageKey::HTTP_POST_DEBUG] = "POST {} with body: {}";
  message_keys_[LogMessageKey::HTTP_RESPONSE_STATUS] =
      "Received response with status code: {}";
  message_keys_[LogMessageKey::HTTP_RESPONSE_BODY] = "Response body: {}";
  message_keys_[LogMessageKey::HTTP_POST_FAILED] =
      "HTTP POST request failed: {}";
  message_keys_[LogMessageKey::HTTP_GET_DEBUG] = "GET {}";
  message_keys_[LogMessageKey::HTTP_GET_FAILED] = "HTTP GET request failed: {}";
  message_keys_[LogMessageKey::HTTP_HEAD_FAILED] =
      "HTTP HEAD request failed: {}";

  // OneBot11 Adapter
  message_keys_[LogMessageKey::ONEBOT11_SERIALIZED_ACTION] =
      "Serialized action request: {}";
  message_keys_[LogMessageKey::ONEBOT11_SERIALIZED_GET_FORWARD_MSG] =
      "Serialized get_forward_msg request: {}";

  // OneBot11 Event Converter
  message_keys_[LogMessageKey::ONEBOT11_EVENT_PARSE_JSON_FAILED] =
      "EventConverter: Failed to parse JSON: {}";
  message_keys_[LogMessageKey::ONEBOT11_EVENT_HEARTBEAT] =
      "EventConverter: Received heartbeat, interval: {}ms";
  message_keys_[LogMessageKey::ONEBOT11_EVENT_CREATE_EXCEPTION] =
      "EventConverter: JSON exception creating event object: {}. JSON: {}";
  message_keys_[LogMessageKey::ONEBOT11_EVENT_UNKNOWN_POST_TYPE] =
      "EventConverter: Unknown post_type '{}'";

  // OneBot11 HTTP Connection Manager
  message_keys_[LogMessageKey::ONEBOT11_HTTP_MANAGER_INIT] =
      "HttpConnectionManager initialized";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_DISCONNECTED] =
      "HTTP connection disconnected";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_API_FAILED] =
      "HTTP API request failed: {}";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_POLLING_START] =
      "Start HTTP event polling, interval: {}ms";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_POLLING_STOP] =
      "Stop HTTP event polling";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_POLLING_FAILED] =
      "Event polling failed: {}";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_POLLING_EXIT] =
      "HTTP event polling coroutine exited";
  message_keys_[LogMessageKey::ONEBOT11_HTTP_PARSE_EVENT_FAILED] =
      "Failed to parse event JSON: {}";

  // OneBot11 WebSocket Connection Manager
  message_keys_[LogMessageKey::ONEBOT11_WS_PENDING_CLEARED] =
      "Cleared all pending requests, total: 0";
  message_keys_[LogMessageKey::ONEBOT11_WS_ALREADY_RUNNING] =
      "ConnectionManager already has a running connection.";
  message_keys_[LogMessageKey::ONEBOT11_WS_MSG_RECEIVED] =
      "Receive ws server message: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_DISCONNECTED_ERROR] =
      "Connection disconnected, error: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_RAW_MSG] =
      "WebSocket received raw message: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_FIND_PENDING] =
      "Looking for pending request with echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_CALL_COMPLETION] =
      "Calling completion handler for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_COMPLETION_NULL] =
      "Completion handler is null for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_CALL_RESOLVER] =
      "Calling promise resolver for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_RESOLVER_NULL] =
      "Promise resolver is null for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_API_RESPONSE_HANDLED] =
      "API response handled for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_UNKNOWN_API_RESPONSE] =
      "Received API response with unknown echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_CURRENT_PENDING] =
      "Current pending requests: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_JSON_PARSE_FAILED] =
      "Failed to parse WebSocket message JSON: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_INVALID_EVENT] =
      "Received invalid event JSON";
  message_keys_[LogMessageKey::ONEBOT11_WS_RECONNECT_SCHEDULED] =
      "Reconnection scheduled in {}ms";
  message_keys_[LogMessageKey::ONEBOT11_WS_RECONNECT_TIMER_ERROR] =
      "Reconnect timer error: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_USE_COROUTINE] =
      "Using coroutine mode for API request";
  message_keys_[LogMessageKey::ONEBOT11_WS_ADD_PENDING_COROUTINE] =
      "Added pending request (coroutine) with echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_MSG_SENT_COROUTINE] =
      "WebSocket message sent (coroutine): {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_REQUEST_TIMEOUT] =
      "API request timeout for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_RESPONSE_RECEIVED_TIMER] =
      "Response received, canceling timeout timer for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_CLEAN_PENDING_COROUTINE] =
      "Cleaning up pending request (coroutine) for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_API_TIMEOUT_COROUTINE] =
      "API request timed out (coroutine) for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_API_SUCCESS_COROUTINE] =
      "API request successful (coroutine) for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_USE_POLLING] =
      "Using polling mode for API request";
  message_keys_[LogMessageKey::ONEBOT11_WS_RESOLVER_CALLED] =
      "Promise resolver called for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_RESPONSE_SET] =
      "Response value set for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_POLLING_RESOLVER_SET] =
      "Polling future resolver set for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_ADD_PENDING_POLLING] =
      "Added pending request (polling) with echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_MSG_SENT_POLLING] =
      "WebSocket message sent (polling): {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_TIMEOUT_DETECTED] =
      "Timeout detected for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_TIMEOUT_EXCEPTION] =
      "API request timeout exception for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_START_POLLING_WAIT] =
      "Starting polling wait for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_CLEAN_PENDING_POLLING] =
      "Cleaning up pending request (polling) for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_API_TIMEOUT_POLLING] =
      "API request timed out (polling) for echo: {}";
  message_keys_[LogMessageKey::ONEBOT11_WS_API_SUCCESS_POLLING] =
      "API request successful (polling) for echo: {}";

  // Proxy HTTP Client Exceptions
  message_keys_[LogMessageKey::PROXY_HTTPS_SSL_HANDSHAKE_FAILED_EXCEPTION] =
      "HTTPS proxy SSL handshake failed: {}";
  message_keys_[LogMessageKey::PROXY_UNSUPPORTED_TYPE] =
      "Unsupported proxy type";
  message_keys_[LogMessageKey::PROXY_CONNECT_REQUEST_FAILED] =
      "Failed to send CONNECT request: {}";
  message_keys_[LogMessageKey::PROXY_CONNECT_RESPONSE_READ_FAILED] =
      "Failed to read CONNECT response: {}";
  message_keys_[LogMessageKey::PROXY_CONNECT_FAILED_EXCEPTION] =
      "Proxy CONNECT request failed: {}";
  message_keys_
      [LogMessageKey::PROXY_SSL_HANDSHAKE_FAILED_MAX_RETRIES_EXCEPTION] =
          "SSL handshake failed after {} retries: {}";
  message_keys_[LogMessageKey::PROXY_SSL_SEND_FAILED] =
      "SSL failed to send HTTP request: {}";
  message_keys_[LogMessageKey::PROXY_SSL_READ_FAILED] =
      "SSL failed to read HTTP response: {}";
  message_keys_[LogMessageKey::PROXY_HTTP_SEND_FAILED] =
      "Failed to send HTTP request: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_CONNECT_REQUEST_FAILED] =
      "Failed to send HTTPS CONNECT request: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_CONNECT_RESPONSE_FAILED] =
      "Failed to read HTTPS CONNECT response: {}";
  message_keys_[LogMessageKey::PROXY_HTTPS_CONNECT_STATUS_ERROR] =
      "HTTPS proxy CONNECT request failed: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_HANDSHAKE_FAILED] =
      "SOCKS5 handshake failed: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_RESPONSE_FAILED] =
      "Failed to read SOCKS5 response: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_VERSION_MISMATCH] =
      "SOCKS5 version mismatch";
  message_keys_[LogMessageKey::PROXY_AUTH_REQUIRED] =
      "Proxy authentication required but not provided";
  message_keys_[LogMessageKey::PROXY_SOCKS5_AUTH_REQUEST_FAILED] =
      "SOCKS5 authentication request failed: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_AUTH_RESPONSE_FAILED] =
      "Failed to read SOCKS5 authentication response: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_AUTH_FAILED] =
      "SOCKS5 authentication failed";
  message_keys_[LogMessageKey::PROXY_SOCKS5_UNSUPPORTED_AUTH] =
      "SOCKS5 unsupported authentication method";
  message_keys_[LogMessageKey::PROXY_SOCKS5_CONNECT_REQUEST_FAILED] =
      "SOCKS5 connect request failed: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_CONNECT_RESPONSE_FAILED] =
      "Failed to read SOCKS5 connect response: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_CONNECT_FAILED_EXCEPTION] =
      "SOCKS5 connect failed, error code: {}";
  message_keys_[LogMessageKey::PROXY_SOCKS5_ADDRESS_READ_FAILED] =
      "Failed to read SOCKS5 address data: {}";

  // OneBot11 WebSocket Connection Manager Exceptions
  message_keys_[LogMessageKey::ONEBOT11_WS_NO_CLIENT] =
      "No available WebSocket client";
  message_keys_[LogMessageKey::ONEBOT11_WS_API_TIMEOUT_MSG] =
      "API request timeout";
  message_keys_[LogMessageKey::ONEBOT11_WS_UNKNOWN_ERROR] =
      "Unknown error: no result and no error";

  // Logger Exceptions
  message_keys_[LogMessageKey::LOGGER_INIT_FAILED_EXCEPTION] =
      "Logger initialization failed: {}";

  // Plugin Exceptions
  message_keys_[LogMessageKey::PLUGIN_BOT_VECTOR_NOT_INIT] =
      "Bot vector not initialized. Call set_bots() first.";

  // Common Network Exceptions
  message_keys_[LogMessageKey::HTTP_CLIENT_NOT_INIT] =
      "HTTP client not initialized";
  message_keys_[LogMessageKey::HTTP_REQUEST_FAILED_STATUS] =
      "HTTP request failed: {}";
  message_keys_[LogMessageKey::DOWNLOAD_URL_INVALID_FORMAT] =
      "Invalid download URL format";
  message_keys_[LogMessageKey::DOWNLOAD_URL_NO_PATH] =
      "No path found in download URL";
  message_keys_[LogMessageKey::FILE_DOWNLOAD_FAILED_STATUS] =
      "File download failed, status code: {}";

  // Telegram Exceptions
  message_keys_[LogMessageKey::TELEGRAM_GETFILE_NO_PATH] =
      "No file_path field in getFile response";
  message_keys_[LogMessageKey::TELEGRAM_GETFILE_FAILED_STATUS] =
      "getFile request failed: {}";

  // Telegram Adapter Messages
  message_keys_[LogMessageKey::TELEGRAM_MSG_PHOTO] = "[Photo]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_STICKER] = "[Sticker]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_STICKER_WITH_EMOJI] =
      "[{} Sticker]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_VIDEO] = "[Video]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_ANIMATION] = "[Animation]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_DOCUMENT] = "[Document]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_DOCUMENT_WITH_NAME] =
      "[Document: {}]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_AUDIO] = "[Audio]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_AUDIO_WITH_TITLE] = "[Audio: {}]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_VOICE] = "[Voice]";
  message_keys_[LogMessageKey::TELEGRAM_MSG_VIDEO_NOTE] = "[Video Note]";

  // Telegram Connection Manager
  message_keys_[LogMessageKey::TELEGRAM_CONNECTION_MANAGER_INIT] =
      "TelegramConnectionManager initialized";

  // OneBot11 WebSocket Connection Manager Exceptions
  message_keys_[LogMessageKey::ONEBOT11_WS_CONNECTION_DISCONNECTED_EXCEPTION] =
      "Connection disconnected";
}

} // namespace obcx::common