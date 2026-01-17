#pragma once

#include <boost/asio/awaitable.hpp>
#include <common/message_type.hpp>
#include <interfaces/plugin.hpp>
#include <memory>
#include <string>

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
   * @return LLM response text or error message
   */
  auto call_llm_api(const std::string &user_text) -> std::string;

  /**
   * @brief Send text response to group
   */
  auto send_response(obcx::core::IBot &bot, const std::string &group_id,
                     const std::string &text) -> boost::asio::awaitable<void>;

  // Configuration
  std::string model_url_;
  std::string model_name_;
  std::string api_key_;
  std::string prompt_path_;
  std::string bot_nickname_;
  int64_t max_reply_chars_ = 500;

  // Parsed URL components
  std::string url_host_;
  uint16_t url_port_ = 443;
  std::string url_path_;
  bool url_use_ssl_ = true;

  // Cached system prompt
  std::string system_prompt_;

  // Base directory for resolving relative paths (repo root)
  std::string base_dir_;
};

} // namespace plugins
