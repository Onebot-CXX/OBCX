#include "common/i18n_log_messages.hpp"
#include <fmt/format.h>

namespace obcx::common {

I18nLogMessages::MessageMap I18nLogMessages::message_keys_;
std::string I18nLogMessages::current_locale_ = "en_US";
std::string I18nLogMessages::locale_dir_;
bool I18nLogMessages::initialized_ = false;

void I18nLogMessages::initialize(const std::string &locale_dir) {
  if (initialized_) {
    return;
  }

  // Set locale directory
  if (locale_dir.empty()) {
    // Try to find the locale directory relative to the executable or use
    // default
    locale_dir_ = "locales";
  } else {
    locale_dir_ = locale_dir;
  }

  // Setup message keys mapping
  setup_message_keys();

  // Try to initialize Boost.Locale with the locale directory
  try {
    std::locale::global(boost::locale::generator().generate(current_locale_));
  } catch (const std::exception &) {
    // Fallback: just use C locale
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
    std::locale::global(boost::locale::generator().generate(locale));
  } catch (const std::exception &) {
    // Fallback: just update the current_locale_ string
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
    boost::locale::generator gen;
    gen.add_messages_path(locale_dir_);
    gen.add_messages_domain("messages");

    std::locale loc = gen(current_locale_);

    // Create a message facet for translation
    if (std::has_facet<boost::locale::message_format<char>>(loc)) {
      const auto &facet =
          std::use_facet<boost::locale::message_format<char>>(loc);
      // domain_id 0 is the default domain
      const char *translated = facet.get(0, "", msg.c_str());
      if (translated) {
        return {translated};
      }
    }

    return msg;
  } catch (const std::exception &) {
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
  message_keys_[LogMessageKey::OPERATION_SUCCESS] = "Operation success";
  message_keys_[LogMessageKey::OPERATION_FAILED] = "Operation failed: {}";
  message_keys_[LogMessageKey::INVALID_PARAMETER] = "Invalid parameter: {}";
}

} // namespace obcx::common