#include "chat_llm_plugin.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <common/config_loader.hpp>
#include <common/logger.hpp>
#include <core/qq_bot.hpp>
#include <core/tg_bot.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <network/http_client.hpp>
#include <nlohmann/json.hpp>
#include <regex>
#include <sqlite3.h>
#include <sstream>

namespace plugins {

ChatLLMPlugin::ChatLLMPlugin() {
  PLUGIN_DEBUG(get_name(), "ChatLLMPlugin constructor called");
}

ChatLLMPlugin::~ChatLLMPlugin() {
  shutdown();
  PLUGIN_DEBUG(get_name(), "ChatLLMPlugin destructor called");
}

auto ChatLLMPlugin::get_name() const -> std::string { return "chat_llm"; }

auto ChatLLMPlugin::get_version() const -> std::string { return "1.0.0"; }

auto ChatLLMPlugin::get_description() const -> std::string {
  return "Chat LLM plugin - provides /chat command for LLM interaction";
}

auto ChatLLMPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing Chat LLM Plugin...");

    // Determine base directory (repo root) from config path
    auto config_path = obcx::common::ConfigLoader::instance().get_config_path();
    if (!config_path.empty()) {
      base_dir_ = std::filesystem::path(config_path).parent_path().string();
    } else {
      base_dir_ = std::filesystem::current_path().string();
    }
    PLUGIN_INFO(get_name(), "Base directory: {}", base_dir_);

    // Load configuration
    if (!load_configuration()) {
      PLUGIN_ERROR(get_name(), "Failed to load plugin configuration");
      return false;
    }

    // Parse URL
    if (!parse_url(model_url_)) {
      PLUGIN_ERROR(get_name(), "Failed to parse model_url: {}", model_url_);
      return false;
    }

    // Load system prompt
    if (!load_system_prompt()) {
      PLUGIN_ERROR(get_name(), "Failed to load system prompt from: {}",
                   prompt_path_);
      return false;
    }

    // Initialize database (must succeed)
    if (!initialize_database()) {
      PLUGIN_ERROR(get_name(), "Failed to initialize database");
      return false;
    }

    // Register event callbacks for both QQ and Telegram bots
    try {
      auto [lock, bots] = get_bots();

      for (auto &bot_ptr : bots) {
        // Register for QQ bot
        if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
          qq_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await process_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered QQ message callback");
        }

        // Register for Telegram bot
        if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
          tg_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await process_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered Telegram message callback");
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "Chat LLM Plugin initialized successfully");
    PLUGIN_INFO(get_name(), "  Model: {}", model_name_);
    PLUGIN_INFO(get_name(), "  URL: {}:{}{}", url_host_, url_port_, url_path_);
    PLUGIN_INFO(get_name(), "  Max reply chars: {}", max_reply_chars_);
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during initialization: {}", e.what());
    return false;
  }
}

void ChatLLMPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing Chat LLM Plugin...");
    PLUGIN_INFO(get_name(), "Chat LLM Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during deinitialization: {}", e.what());
  }
}

void ChatLLMPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down Chat LLM Plugin...");
    PLUGIN_INFO(get_name(), "Chat LLM Plugin shutdown complete");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during shutdown: {}", e.what());
  }
}

auto ChatLLMPlugin::load_configuration() -> bool {
  try {
    auto config_table = get_config_table();
    if (!config_table.has_value()) {
      PLUGIN_ERROR(get_name(), "No config found at [plugins.chat_llm.config]");
      return false;
    }

    const auto &config = config_table.value();

    // Required fields
    if (auto val = config["model_url"].value<std::string>()) {
      model_url_ = *val;
    } else {
      PLUGIN_ERROR(get_name(), "Missing required config: model_url");
      return false;
    }

    if (auto val = config["model_name"].value<std::string>()) {
      model_name_ = *val;
    } else {
      PLUGIN_ERROR(get_name(), "Missing required config: model_name");
      return false;
    }

    if (auto val = config["api_key"].value<std::string>()) {
      api_key_ = *val;
    } else {
      PLUGIN_ERROR(get_name(), "Missing required config: api_key");
      return false;
    }

    if (auto val = config["prompt_path"].value<std::string>()) {
      prompt_path_ = *val;
    } else {
      PLUGIN_ERROR(get_name(), "Missing required config: prompt_path");
      return false;
    }

    // Required: history database path
    if (auto val = config["history_db_path"].value<std::string>()) {
      history_db_path_ = *val;
      PLUGIN_INFO(get_name(), "History DB path configured: {}",
                  history_db_path_);
    } else {
      PLUGIN_ERROR(get_name(), "Missing required config: history_db_path");
      return false;
    }

    // Optional fields
    if (auto val = config["bot_nickname"].value<std::string>()) {
      bot_nickname_ = *val;
    }

    if (auto val = config["max_reply_chars"].value<int64_t>()) {
      max_reply_chars_ = *val;
    }

    // Optional: history limit (default 10)
    if (auto val = config["history_limit"].value<int64_t>()) {
      history_limit_ = *val;
    }

    PLUGIN_INFO(get_name(), "Configuration loaded successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load configuration: {}", e.what());
    return false;
  }
}

auto ChatLLMPlugin::load_system_prompt() -> bool {
  try {
    // Resolve prompt_path relative to base_dir (repo root)
    std::filesystem::path prompt_full_path;
    if (std::filesystem::path(prompt_path_).is_absolute()) {
      prompt_full_path = prompt_path_;
    } else {
      prompt_full_path = std::filesystem::path(base_dir_) / prompt_path_;
    }

    PLUGIN_INFO(get_name(), "Loading system prompt from: {}",
                prompt_full_path.string());

    if (!std::filesystem::exists(prompt_full_path)) {
      PLUGIN_ERROR(get_name(), "System prompt file not found: {}",
                   prompt_full_path.string());
      return false;
    }

    std::ifstream file(prompt_full_path);
    if (!file.is_open()) {
      PLUGIN_ERROR(get_name(), "Failed to open system prompt file: {}",
                   prompt_full_path.string());
      return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    system_prompt_ = buffer.str();

    if (system_prompt_.empty()) {
      PLUGIN_WARN(get_name(), "System prompt file is empty");
    } else {
      PLUGIN_INFO(get_name(), "Loaded system prompt ({} chars)",
                  system_prompt_.size());
    }

    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load system prompt: {}", e.what());
    return false;
  }
}

auto ChatLLMPlugin::initialize_database() -> bool {
  try {
    // Resolve database path
    std::filesystem::path db_path;
    if (std::filesystem::path(history_db_path_).is_absolute()) {
      db_path = history_db_path_;
    } else {
      db_path = std::filesystem::path(base_dir_) / history_db_path_;
    }

    // Ensure parent directory exists
    std::filesystem::path parent_dir = db_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
      std::filesystem::create_directories(parent_dir);
      PLUGIN_INFO(get_name(), "Created database directory: {}",
                  parent_dir.string());
    }

    PLUGIN_INFO(get_name(), "Initializing database: {}", db_path.string());

    // Open database
    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(db_path.string().c_str(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                 SQLITE_OPEN_FULLMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
      PLUGIN_ERROR(get_name(), "Failed to open database: {} (error: {})",
                   db_path.string(), sqlite3_errmsg(db));
      if (db != nullptr) {
        sqlite3_close(db);
      }
      return false;
    }

    // Create table if not exists
    const std::string create_table_sql = R"(
      CREATE TABLE IF NOT EXISTS messages (
        platform TEXT NOT NULL,
        group_id TEXT NOT NULL,
        message_id TEXT NOT NULL,
        user_id TEXT NOT NULL,
        content TEXT NOT NULL,
        timestamp INTEGER NOT NULL,
        is_bot INTEGER NOT NULL,
        PRIMARY KEY (platform, message_id)
      );
    )";

    char *err_msg = nullptr;
    rc = sqlite3_exec(db, create_table_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::string error = err_msg != nullptr ? err_msg : "unknown error";
      PLUGIN_ERROR(get_name(), "Failed to create messages table: {}", error);
      if (err_msg != nullptr) {
        sqlite3_free(err_msg);
      }
      sqlite3_close(db);
      return false;
    }

    // Create index for faster queries
    const std::string create_index_sql = R"(
      CREATE INDEX IF NOT EXISTS idx_group_time 
      ON messages(group_id, timestamp DESC);
    )";

    rc = sqlite3_exec(db, create_index_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::string error = err_msg != nullptr ? err_msg : "unknown error";
      PLUGIN_WARN(get_name(), "Failed to create index: {}", error);
      if (err_msg != nullptr) {
        sqlite3_free(err_msg);
      }
      // Non-fatal, continue
    }

    sqlite3_close(db);
    PLUGIN_INFO(get_name(), "Database initialized successfully");
    return true;

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during database initialization: {}",
                 e.what());
    return false;
  }
}

auto ChatLLMPlugin::get_text_content(const obcx::common::Message &msg)
    -> std::string {
  std::string result;
  for (const auto &segment : msg) {
    if (segment.type == "text") {
      // Extract text from segment.data["text"]
      if (segment.data.contains("text")) {
        try {
          std::string text = segment.data["text"].get<std::string>();
          result += text;
        } catch (const std::exception &e) {
          PLUGIN_WARN(get_name(), "Failed to extract text from segment: {}",
                      e.what());
        }
      }
    }
  }
  return result;
}

auto ChatLLMPlugin::save_message_impl(const std::string &platform,
                                      const std::string &group_id,
                                      const std::string &message_id,
                                      const std::string &user_id,
                                      const std::string &content,
                                      int64_t timestamp, bool is_bot) -> bool {
  if (content.empty()) {
    return true; // Skip empty messages
  }

  try {
    // Resolve database path
    std::filesystem::path db_path;
    if (std::filesystem::path(history_db_path_).is_absolute()) {
      db_path = history_db_path_;
    } else {
      db_path = std::filesystem::path(base_dir_) / history_db_path_;
    }

    // Open database
    sqlite3 *db = nullptr;
    int rc =
        sqlite3_open_v2(db_path.string().c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
      PLUGIN_ERROR(get_name(), "Failed to open database for saving: {}",
                   sqlite3_errmsg(db));
      if (db != nullptr) {
        sqlite3_close(db);
      }
      return false;
    }

    // Prepare INSERT OR IGNORE statement (prevents duplicates)
    const std::string sql = R"(
      INSERT OR IGNORE INTO messages (platform, group_id, message_id, user_id, content, timestamp, is_bot)
      VALUES (?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      PLUGIN_ERROR(get_name(), "Failed to prepare INSERT statement: {}",
                   sqlite3_errmsg(db));
      sqlite3_close(db);
      return false;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, group_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, user_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, timestamp);
    sqlite3_bind_int(stmt, 7, is_bot ? 1 : 0);

    // Execute
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      PLUGIN_ERROR(get_name(), "Failed to execute INSERT: {}",
                   sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return true;

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during save_message_impl: {}",
                 e.what());
    return false;
  }
}

auto ChatLLMPlugin::save_message_async(
    obcx::core::IBot &bot, const std::string &platform,
    const std::string &group_id, const std::string &message_id,
    const std::string &user_id, const std::string &content, int64_t timestamp,
    bool is_bot) -> boost::asio::awaitable<void> {
  try {
    // Run database write in background thread pool
    bool success =
        co_await bot.run_heavy_task([this, platform, group_id, message_id,
                                     user_id, content, timestamp, is_bot]() {
          return save_message_impl(platform, group_id, message_id, user_id,
                                   content, timestamp, is_bot);
        });

    if (!success) {
      PLUGIN_WARN(get_name(), "Failed to save message from user {} in group {}",
                  user_id, group_id);
    } else {
      PLUGIN_DEBUG(get_name(), "Saved message from user {} in group {}",
                   user_id, group_id);
    }
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception in save_message_async: {}", e.what());
  }
}

auto ChatLLMPlugin::parse_url(const std::string &url) -> bool {
  try {
    // Simple URL parsing: scheme://host[:port][/path]
    // Example: https://api.openai.com/v1/chat/completions

    std::regex url_regex(R"(^(https?)://([^/:]+)(?::(\d+))?(/.*)?$)",
                         std::regex::icase);
    std::smatch match;

    if (!std::regex_match(url, match, url_regex)) {
      PLUGIN_ERROR(get_name(), "Invalid URL format: {}", url);
      return false;
    }

    std::string scheme = match[1].str();
    url_host_ = match[2].str();
    std::string port_str = match[3].str();
    url_path_ = match[4].str();

    // Determine SSL
    url_use_ssl_ = (scheme == "https" || scheme == "HTTPS");

    // Determine port
    if (!port_str.empty()) {
      url_port_ = static_cast<uint16_t>(std::stoi(port_str));
    } else {
      url_port_ = url_use_ssl_ ? 443 : 80;
    }

    // Default path
    if (url_path_.empty()) {
      url_path_ = "/";
    }

    PLUGIN_DEBUG(get_name(),
                 "Parsed URL - host: {}, port: {}, path: {}, ssl: {}",
                 url_host_, url_port_, url_path_, url_use_ssl_);

    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to parse URL: {}", e.what());
    return false;
  }
}

auto ChatLLMPlugin::process_message(obcx::core::IBot &bot,
                                    const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  auto process_start = std::chrono::steady_clock::now();
  PLUGIN_INFO(get_name(), "[TIMING] process_message START at: {} ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  process_start.time_since_epoch())
                  .count());

  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  // Determine platform based on bot type
  std::string platform;
  if (dynamic_cast<obcx::core::QQBot *>(&bot) != nullptr) {
    platform = "qq";
  } else if (dynamic_cast<obcx::core::TGBot *>(&bot) != nullptr) {
    platform = "telegram";
  } else {
    platform = "unknown";
  }

  // Save all group text messages to database
  const std::string &group_id = event.group_id.value();
  const std::string &user_id = event.user_id;
  const std::string &self_id = event.self_id;
  const std::string &message_id = event.message_id;

  // Extract text content from message segments
  std::string text_content = get_text_content(event.message);

  if (!text_content.empty()) {
    // Convert event.time to milliseconds timestamp
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                            event.time.time_since_epoch())
                            .count();

    // Save user message asynchronously
    co_await save_message_async(bot, platform, group_id, message_id, user_id,
                                text_content, timestamp, false);
  }

  const std::string &raw_message = event.raw_message;

  // Check if message starts with /chat
  if (!raw_message.starts_with("/chat")) {
    co_return;
  }

  PLUGIN_DEBUG(get_name(), "Received /chat command from user {} in group {}",
               user_id, group_id);

  // Parse user text: remove "/chat" prefix and trim
  std::string user_text = raw_message.substr(5); // Remove "/chat"

  // Trim leading whitespace
  size_t start = user_text.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) {
    user_text.clear();
  } else {
    user_text = user_text.substr(start);
  }

  // Trim trailing whitespace
  size_t end = user_text.find_last_not_of(" \t\n\r");
  if (end != std::string::npos) {
    user_text = user_text.substr(0, end + 1);
  }

  // Check if user text is empty
  if (user_text.empty()) {
    co_await send_response(bot, platform, group_id, self_id,
                           "用法: /chat <内容>");
    co_return;
  }

  PLUGIN_INFO(
      get_name(),
      "Processing /chat request from user {} in group {}, text length: {}",
      user_id, group_id, user_text.size());

  // Global busy check: only one LLM request at a time across all groups
  if (llm_busy_.exchange(true)) {
    PLUGIN_INFO(get_name(),
                "LLM busy, discarding request from user {} in group {}",
                user_id, group_id);
    co_return; // Silent discard
  }

  uint64_t req_id = ++llm_req_seq_;
  llm_active_req_ = req_id;
  PLUGIN_INFO(get_name(), "LLM request started, req_id={}, busy=true", req_id);

  // Get executor from current coroutine
  auto executor = co_await boost::asio::this_coro::executor;

  // Shared state for watchdog + result communication
  using AsyncResult = std::pair<std::string, std::exception_ptr>;
  auto result_promise = std::make_shared<std::promise<AsyncResult>>();
  auto result_future = result_promise->get_future();

  // Shared timer pointer so background coroutine can cancel it
  auto watchdog_ptr =
      std::make_shared<boost::asio::steady_timer>(executor, llm_watchdog_);

  // Spawn background coroutine to execute LLM request
  boost::asio::co_spawn(
      executor,
      [this, &bot, req_id, user_text, group_id, platform, self_id, user_id,
       result_promise, watchdog_ptr]() -> boost::asio::awaitable<void> {
        AsyncResult result;
        try {
          result.first = co_await bot.run_heavy_task(
              [this, user_text, group_id, platform, self_id, user_id]() {
                return call_llm_api(user_text, group_id, platform, self_id,
                                    user_id);
              });
          result.second = nullptr;
          result_promise->set_value(result);
          watchdog_ptr->cancel();
          PLUGIN_INFO(get_name(), "LLM request completed, req_id={}", req_id);
        } catch (...) {
          result.first.clear();
          result.second = std::current_exception();
          result_promise->set_value(result);
          watchdog_ptr->cancel();
          PLUGIN_ERROR(get_name(),
                       "LLM request failed with exception, req_id={}", req_id);
        }
      },
      boost::asio::detached);

  // Wait for either: task completion OR watchdog timeout
  boost::system::error_code ec;
  co_await watchdog_ptr->async_wait(boost::asio::redirect_error(ec));

  // Helper to release busy safely
  auto release_busy = [this, req_id]() {
    uint64_t expected = req_id;
    if (llm_active_req_.compare_exchange_strong(expected, 0)) {
      llm_busy_ = false;
      PLUGIN_INFO(get_name(),
                  "Busy released, req_id={}. Next request can proceed", req_id);
    } else {
      PLUGIN_WARN(get_name(),
                  "Busy release skipped: req_id mismatch (expected {}, got {})",
                  req_id, expected);
    }
  };

  if (ec == boost::asio::error::operation_aborted) {
    // Timer was cancelled - task completed first
    PLUGIN_INFO(get_name(), "LLM request finished before timeout, req_id={}",
                req_id);

    // Get result
    auto [response, eptr] = result_future.get();

    if (eptr) {
      // Exception occurred
      std::string error_msg;
      try {
        std::rethrow_exception(eptr);
      } catch (const std::exception &e) {
        PLUGIN_ERROR(get_name(), "LLM API call failed: {}", e.what());
        error_msg = std::string("LLM 请求失败: ") + e.what();
      }
      co_await send_response(bot, platform, group_id, self_id, error_msg);
    } else {
      // Success
      // Check response length
      if (static_cast<int64_t>(response.size()) > max_reply_chars_) {
        PLUGIN_INFO(get_name(),
                    "LLM response too long ({} chars), full response: {}",
                    response.size(), response);
        co_await send_response(bot, platform, group_id, self_id,
                               "LLM 返回过长（" +
                                   std::to_string(response.size()) +
                                   " chars），已记录到日志");
      } else {
        co_await send_response(bot, platform, group_id, self_id, response);
      }
    }

    release_busy();

  } else {
    // Watchdog timeout: silently discard this request and release busy
    PLUGIN_WARN(get_name(),
                "LLM request watchdog timeout ({} ms), req_id={}. Silently "
                "discarding request.",
                llm_watchdog_.count(), req_id);
    release_busy();

    co_return; // Do NOT send any response
  }

  auto process_end = std::chrono::steady_clock::now();
  auto process_duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(process_end -
                                                            process_start)
          .count();
  PLUGIN_INFO(get_name(), "[TIMING] process_message END after: {} ms (TOTAL)",
              process_duration_ms);
}

auto ChatLLMPlugin::call_llm_api(const std::string &user_text,
                                 const std::string &group_id,
                                 const std::string &platform,
                                 const std::string &self_id,
                                 const std::string &user_id) -> std::string {
  auto start_time = std::chrono::steady_clock::now();
  PLUGIN_INFO(get_name(), "[TIMING] call_llm_api START at: {} ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  start_time.time_since_epoch())
                  .count());

  try {
    // Create HTTP client
    boost::asio::io_context ioc;
    obcx::common::ConnectionConfig config;
    config.host = url_host_;
    config.port = url_port_;
    config.use_ssl = url_use_ssl_;

    obcx::network::HttpClient http_client(ioc, config);

    // Build request body
    nlohmann::json request_body;
    request_body["model"] = model_name_;
    request_body["stream"] = false;
    request_body["messages"] = nlohmann::json::array();

    // Add system prompt
    request_body["messages"].push_back(
        {{"role", "system"}, {"content", system_prompt_}});

    // Fetch and add history messages (if history_db_path_ is configured)
    if (!history_db_path_.empty() && platform != "unknown") {
      auto history = fetch_history_messages(group_id, platform);
      PLUGIN_INFO(get_name(),
                  "Fetched {} history messages for group {} platform {}",
                  history.size(), group_id, platform);

      for (const auto &item : history) {
        // Determine role: if user_id matches self_id, it's assistant
        std::string role = "user";
        if (!self_id.empty() && item.user_id == self_id) {
          role = "assistant";
        }

        // Format content with user_id prefix for multi-user context
        std::string content = item.user_id + ": " + item.content;

        request_body["messages"].push_back(
            {{"role", role}, {"content", content}});
      }
    }

    // Add current user message
    std::string current_content = user_id + ": " + user_text;
    request_body["messages"].push_back(
        {{"role", "user"}, {"content", current_content}});

    std::string body = request_body.dump();

    PLUGIN_DEBUG(get_name(), "Sending LLM request to {}:{}{}", url_host_,
                 url_port_, url_path_);

    // Build headers
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + api_key_;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json";
    headers["Accept-Encoding"] = "identity";

    // Send request
    PLUGIN_INFO(get_name(),
                "Sending LLM request to {}:{}{} (body size: {} bytes)",
                url_host_, url_port_, url_path_, body.size());
    auto response = http_client.post_sync(url_path_, body, headers);

    PLUGIN_DEBUG(get_name(), "LLM API response status: {}",
                 response.status_code);
    PLUGIN_INFO(get_name(), "LLM API response body ({} chars): {}",
                response.body.size(), response.body);

    if (!response.is_success()) {
      return "LLM API 返回错误 (HTTP " + std::to_string(response.status_code) +
             ")";
    }

    // Parse response
    nlohmann::json response_json;
    try {
      response_json = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::parse_error &e) {
      PLUGIN_ERROR(get_name(), "Failed to parse LLM response JSON: {}",
                   e.what());
      return "LLM 响应解析失败";
    }

    // Check for error in response
    if (response_json.contains("error")) {
      std::string error_msg = "LLM API 错误";
      if (response_json["error"].contains("message")) {
        error_msg +=
            ": " + response_json["error"]["message"].get<std::string>();
      }
      PLUGIN_ERROR(get_name(), "LLM API returned error: {}", response.body);
      return error_msg;
    }

    // Extract assistant message
    if (!response_json.contains("choices") ||
        !response_json["choices"].is_array() ||
        response_json["choices"].empty()) {
      PLUGIN_ERROR(get_name(), "LLM response missing choices array");
      return "LLM 响应格式错误";
    }

    auto &first_choice = response_json["choices"][0];
    if (!first_choice.contains("message") ||
        !first_choice["message"].contains("content")) {
      PLUGIN_ERROR(get_name(), "LLM response missing message.content");
      return "LLM 响应格式错误";
    }

    return first_choice["message"]["content"].get<std::string>();

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           end_time - start_time)
                           .count();
    PLUGIN_ERROR(
        get_name(),
        "[TIMING] call_llm_api END (EXCEPTION) after: {} ms, error: {}",
        duration_ms, e.what());
    throw;
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         end_time - start_time)
                         .count();
  PLUGIN_INFO(get_name(), "[TIMING] call_llm_api END after: {} ms",
              duration_ms);
}

auto ChatLLMPlugin::fetch_history_messages(const std::string &group_id,
                                           const std::string &platform)
    -> std::vector<HistoryItem> {
  std::vector<HistoryItem> result;

  if (history_db_path_.empty()) {
    return result;
  }

  // Resolve database path (relative to base_dir_ if not absolute)
  std::filesystem::path db_path;
  if (std::filesystem::path(history_db_path_).is_absolute()) {
    db_path = history_db_path_;
  } else {
    db_path = std::filesystem::path(base_dir_) / history_db_path_;
  }

  PLUGIN_DEBUG(get_name(), "Opening history database: {}", db_path.string());

  // Open database in read-only mode
  sqlite3 *db = nullptr;
  int rc =
      sqlite3_open_v2(db_path.string().c_str(), &db,
                      SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    PLUGIN_WARN(get_name(), "Failed to open history database: {} (error: {})",
                db_path.string(), sqlite3_errmsg(db));
    if (db) {
      sqlite3_close(db);
    }
    return result;
  }

  // Query recent messages
  const std::string sql = R"(
    SELECT user_id, content, timestamp
    FROM messages
    WHERE group_id = ? AND platform = ?
    ORDER BY timestamp DESC
    LIMIT ?;
  )";

  sqlite3_stmt *stmt = nullptr;
  rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    PLUGIN_WARN(get_name(), "Failed to prepare SQL statement: {}",
                sqlite3_errmsg(db));
    sqlite3_close(db);
    return result;
  }

  // Bind parameters
  sqlite3_bind_text(stmt, 1, group_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, history_limit_);

  // Fetch results (in DESC order, newest first)
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    HistoryItem item;

    const char *user_id_ptr =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const char *content_ptr =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));

    item.user_id = user_id_ptr ? user_id_ptr : "";
    item.content = content_ptr ? content_ptr : "";
    item.timestamp = sqlite3_column_int64(stmt, 2);

    // Skip empty content
    if (!item.content.empty()) {
      result.push_back(item);
    }
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);

  // Reverse to get oldest-first order (for LLM context)
  std::reverse(result.begin(), result.end());

  PLUGIN_DEBUG(get_name(), "Fetched {} history messages from database",
               result.size());

  return result;
}

auto ChatLLMPlugin::send_response(obcx::core::IBot &bot,
                                  const std::string &platform,
                                  const std::string &group_id,
                                  const std::string &self_id,
                                  const std::string &text)
    -> boost::asio::awaitable<void> {
  auto send_start_time = std::chrono::steady_clock::now();
  PLUGIN_INFO(get_name(), "[TIMING] send_response START at: {} ms, text: '{}'",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  send_start_time.time_since_epoch())
                  .count(),
              text.substr(0, std::min(size_t(50), text.size())));

  try {
    obcx::common::Message message;
    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    message.push_back(text_segment);

    co_await bot.send_group_message(group_id, message);

    // Save bot's reply to database
    // Generate local message ID for bot replies
    int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    std::string message_id =
        "local-bot-" + std::to_string(timestamp) + "-" + group_id;

    co_await save_message_async(bot, platform, group_id, message_id, self_id,
                                text, timestamp, true);

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send response: {}", e.what());
  }

  auto send_end_time = std::chrono::steady_clock::now();
  auto send_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              send_end_time - send_start_time)
                              .count();
  PLUGIN_INFO(
      get_name(),
      "[TIMING] send_response END after: {} ms (bot.send_group_message)",
      send_duration_ms);
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::ChatLLMPlugin)
