#pragma once

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <chrono>
#include <common/message_type.hpp>
#include <cstdint>
#include <interfaces/plugin.hpp>
#include <memory>
#include <string>
#include <vector>

namespace obcx::network {
class HttpClient;
}

namespace plugins {

/**
 * @brief Chat LLM Plugin - provides /chat command for LLM interaction
 *
 * Supports both QQ and Telegram platforms.
 * Configuration (in config.toml [plugins.chat_llm.config]):
 *   - model_url: Full URL to OpenAI-compatible API endpoint
 *   - model_name: Model identifier (e.g., "gpt-4o-mini")
 *   - api_key: API key for authentication
 *   - prompt_path: Path to system prompt file (relative to repo root)
 *   - bot_nickname: Optional nickname for the bot
 *   - max_reply_chars: Maximum reply length (default: 500)
 */
class ChatLLMPlugin : public obcx::interface::IPlugin {
public:
  ChatLLMPlugin();
  ~ChatLLMPlugin() override;

  [[nodiscard]] auto get_name() const -> std::string override;
  [[nodiscard]] auto get_version() const -> std::string override;
  [[nodiscard]] auto get_description() const -> std::string override;

  auto initialize() -> bool override;
  void deinitialize() override;
  void shutdown() override;

  /**
   * @brief Process message event (public for tests)
   */
  auto process_message(obcx::core::IBot &bot,
                       const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

private:
  /**
   * @brief Load plugin configuration from config.toml
   */
  auto load_configuration() -> bool;

  /**
   * @brief Load system prompt from file
   */
  auto load_system_prompt() -> bool;

  /**
   * @brief Parse URL into components (scheme, host, port, path)
   */
  auto parse_url(const std::string &url) -> bool;

  /**
   * @brief Handle incoming message event (both QQ and Telegram)
   */
  auto handle_message(obcx::core::IBot &bot,
                      const obcx::common::MessageEvent &event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief Call LLM API and get response
   * @param user_text User's input text
   * @param group_id Group ID for history lookup
   * @param platform Platform name (qq/telegram) for history lookup
   * @param self_id Bot's self ID for role determination
   * @param user_id Current user's ID
   * @return LLM response text or error message
   */
  auto call_llm_api(const std::string &user_text, const std::string &group_id,
                    const std::string &platform, const std::string &self_id,
                    const std::string &user_id) -> std::string;

  /**
   * @brief Send text response to group and save to database
   * @param bot Bot instance
   * @param platform Platform name (qq/telegram)
   * @param group_id Group ID
   * @param self_id Bot's self ID
   * @param text Response text
   */
  auto send_response(obcx::core::IBot &bot, const std::string &platform,
                     const std::string &group_id, const std::string &self_id,
                     const std::string &text) -> boost::asio::awaitable<void>;

  /**
   * @brief History message item from database
   */
  struct HistoryItem {
    std::string user_id;
    std::string content;
    int64_t timestamp;
  };

  /**
   * @brief Fetch history messages from database
   * @param group_id Group ID to filter
   * @param platform Platform name to filter
   * @return Vector of history items (oldest first)
   */
  auto fetch_history_messages(const std::string &group_id,
                              const std::string &platform)
      -> std::vector<HistoryItem>;

  /**
   * @brief Initialize database and create table structure
   * @return true if successful, false on error
   */
  auto initialize_database() -> bool;

  /**
   * @brief Extract text content from Message segments
   * @param msg Message containing segments
   * @return Concatenated text content
   */
  auto get_text_content(const obcx::common::Message &msg) -> std::string;

  /**
   * @brief Save message to database (synchronous, for thread pool)
   * @param platform Platform name (qq/telegram)
   * @param group_id Group ID
   * @param message_id Platform message ID or generated local ID
   * @param user_id User ID (or bot's self_id for bot messages)
   * @param content Message text content
   * @param timestamp Unix timestamp in milliseconds
   * @param is_bot True if message is from bot
   * @return true if successful, false on error
   */
  auto save_message_impl(const std::string &platform,
                         const std::string &group_id,
                         const std::string &message_id,
                         const std::string &user_id, const std::string &content,
                         int64_t timestamp, bool is_bot) -> bool;

  /**
   * @brief Save message to database (async wrapper)
   * @param bot Bot instance for heavy task scheduling
   * @param platform Platform name (qq/telegram)
   * @param group_id Group ID
   * @param message_id Platform message ID or generated local ID
   * @param user_id User ID (or bot's self_id for bot messages)
   * @param content Message text content
   * @param timestamp Unix timestamp in milliseconds
   * @param is_bot True if message is from bot
   */
  auto save_message_async(obcx::core::IBot &bot, const std::string &platform,
                          const std::string &group_id,
                          const std::string &message_id,
                          const std::string &user_id,
                          const std::string &content, int64_t timestamp,
                          bool is_bot) -> boost::asio::awaitable<void>;

  // Configuration
  std::string model_url_;
  std::string model_name_;
  std::string api_key_;
  std::string prompt_path_;
  std::string bot_nickname_;
  int64_t max_reply_chars_ = 500;

  // History configuration
  std::string history_db_path_;
  int64_t history_limit_ = 10;

  // Parsed URL components
  std::string url_host_;
  uint16_t url_port_ = 443;
  std::string url_path_;
  bool url_use_ssl_ = true;

  // Cached system prompt
  std::string system_prompt_;

  // Base directory for resolving relative paths (repo root)
  std::string base_dir_;

  // Concurrency control: only one LLM request at a time across all groups
  std::atomic<bool> llm_busy_{false};
  std::atomic<uint64_t> llm_req_seq_{0};
  std::atomic<uint64_t> llm_active_req_{0};
  std::chrono::milliseconds llm_watchdog_{120000}; // 30 seconds watchdog
};

} // namespace plugins
