#include "common/i18n_log_messages.hpp"
#include <fmt/format.h>

namespace obcx::common {

I18nLogMessages::MessageMap I18nLogMessages::message_keys_;
std::string I18nLogMessages::current_locale_ = "en_US";
std::string I18nLogMessages::locale_dir_;
bool I18nLogMessages::initialized_ = false;
std::locale I18nLogMessages::current_locale_obj_;

void I18nLogMessages::initialize(const std::string &locale_dir) {
  if (initialized_) {
    return;
  }

  // Set locale directory
  if (locale_dir.empty()) {
    // Use build directory for compiled .mo files
    locale_dir_ = "build/locales";
  } else {
    locale_dir_ = locale_dir;
  }

  // Setup message keys mapping
  setup_message_keys();

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

  initialized_ = true;
}

void I18nLogMessages::set_locale(const std::string &locale) {
  if (!initialized_) {
    initialize();
  }
  current_locale_ = locale;

  // Try to set Boost.Locale
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

std::string I18nLogMessages::get_message(LogMessageKey key) {
  if (!initialized_) {
    initialize();
  }

  auto it = message_keys_.find(key);
  if (it == message_keys_.end()) {
    return "Unknown message key";
  }

  const std::string &msg = it->second;

  // Try to load translated message from .mo file using Boost.Locale
  try {
    // Use the cached locale object
    std::string translated = boost::locale::translate(msg).str(current_locale_obj_);
    return translated;
  } catch (const std::exception &e) {
    // Fallback: return the original English message
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