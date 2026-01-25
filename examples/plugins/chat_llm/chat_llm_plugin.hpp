#pragma once

#include <boost/asio/awaitable.hpp>
#include <common/message_type.hpp>
#include <interfaces/plugin.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "chat_llm/llm_client.hpp"
#include "chat_llm/message_repository.hpp"
#include "chat_llm/prompt_builder.hpp"
#include "chat_llm/runtime.hpp"
#include <chat_llm/command_parser.hpp>

namespace plugins {

/**
 * @brief Chat LLM Plugin - provides /chat command for LLM interaction
 *
 * Refactored version using modular components:
 * - CommandParser: Parse /chat commands from events
 * - MessageRepository: SQLite storage with v2 schema (fixes Telegram conflicts)
 * - PromptBuilder: Build LLM context with de-duplication
 * - LlmClient: OpenAI-compatible API client
 * - Runtime: Shared state for safe lifecycle during hot reload
 *
 * Configuration (in config.toml [plugins.chat_llm.config]):
 *   - model_url: Full URL to OpenAI-compatible API endpoint
 *   - model_name: Model identifier (e.g., "gpt-4o-mini")
 *   - api_key: API key for authentication
 *   - prompt_path: Path to system prompt file (relative to repo root)
 *   - history_db_path: Path to history database
 *   - collect_enabled: Enable message collection (default: false)
 *   - collect_allowed_groups: Groups allowed for collection (default: all)
 *   - history_ttl_days: TTL for history cleanup in days (default: 1)
 *   - history_limit: Number of history messages for context (default: 10)
 *   - max_reply_chars: Maximum reply length (default: 500)
 *   - llm_timeout_ms: LLM request timeout (default: 120000)
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
  auto load_configuration() -> bool;
  auto load_system_prompt() -> bool;
  auto parse_url(const std::string &url) -> bool;
  auto send_response(obcx::core::IBot &bot, const chat_llm::ParsedCommand &cmd,
                     const std::string &text) -> boost::asio::awaitable<void>;
  auto filter_llm_response(const std::string &response) -> std::string;

  auto ensure_runtime(obcx::core::IBot &bot)
      -> std::shared_ptr<chat_llm::Runtime>;
  void stop_all_runtimes();

  // Components
  // Per-bot runtime (lazy init on first message)
  std::unordered_map<obcx::core::IBot *, std::shared_ptr<chat_llm::Runtime>>
      runtimes_;
  std::mutex runtimes_mutex_;
  chat_llm::RuntimeConfig runtime_config_;
  std::unique_ptr<chat_llm::MessageRepository> repo_;
  std::unique_ptr<chat_llm::CommandParser> cmd_parser_;
  std::unique_ptr<chat_llm::PromptBuilder> prompt_builder_;
  std::unique_ptr<chat_llm::LlmClient> llm_client_;

  // Configuration
  std::string model_url_;
  std::string model_name_;
  std::string api_key_;
  std::string prompt_path_;
  std::string history_db_path_;
  std::string system_prompt_;
  std::string base_dir_;

  // Parsed URL components
  std::string url_host_;
  uint16_t url_port_ = 443;
  std::string url_path_;
  bool url_use_ssl_ = true;

  // Instance tracking (for debugging plugin reload issues)
  int instance_id_;
};
} // namespace plugins
