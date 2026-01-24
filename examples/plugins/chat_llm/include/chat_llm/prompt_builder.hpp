#pragma once

#include "chat_llm/message_repository.hpp"
#include <string>
#include <vector>

namespace plugins::chat_llm {

/**
 * @brief Message role for OpenAI format
 */
enum class MessageRole { system, user, assistant };

/**
 * @brief Single message in OpenAI format
 */
struct OpenAiMessage {
  MessageRole role;
  std::string content;
};

/**
 * @brief Builder for LLM prompt/context
 *
 * Handles:
 * - System prompt injection
 * - History de-duplication and filtering
 * - Character/token limit enforcement
 * - Proper role assignment (user vs assistant based on user_id == self_id)
 */
class PromptBuilder {
public:
  PromptBuilder(std::string system_prompt, int max_reply_chars = 500);

  /**
   * @brief Build complete messages array for LLM API
   * @param history Context history (oldest first)
   * @param user_id Current user ID
   * @param user_text Current user input
   * @param self_id Bot's self ID for role determination
   * @return Vector of OpenAI-formatted messages
   */
  [[nodiscard]] auto build(const std::vector<MessageRecord> &history,
                           const std::string &user_id,
                           const std::string &user_text,
                           const std::string &self_id)
      -> std::vector<OpenAiMessage>;

  /**
   * @brief Set the maximum context limit (number of messages)
   */
  void set_history_limit(int limit) { history_limit_ = limit; }

  /**
   * @brief Estimate if response exceeds limit
   * @return true if response is too long
   */
  [[nodiscard]] auto is_response_too_long(const std::string &response) const
      -> bool;

  /**
   * @brief Get the configured max reply character limit
   */
  [[nodiscard]] auto get_max_reply_chars() const -> int {
    return max_reply_chars_;
  }

  /**
   * @brief Get the system prompt
   */
  [[nodiscard]] auto get_system_prompt() const -> const std::string & {
    return system_prompt_;
  }

  /**
   * @brief Set the configured max reply character limit
   */
  void set_max_reply_chars(int limit) { max_reply_chars_ = limit; }

private:
  std::string system_prompt_;
  int history_limit_;
  int max_reply_chars_;

  /**
   * @brief Deduplicate: remove messages identical to current turn
   */
  [[nodiscard]] auto deduplicate_history(
      const std::vector<MessageRecord> &history, const std::string &user_id,
      const std::string &user_text) -> std::vector<MessageRecord>;

  /**
   * @brief Determine role from message record (based on is_bot field)
   */
  [[nodiscard]] auto determine_role(const MessageRecord &record,
                                    const std::string &self_id) -> MessageRole;

  /**
   * @brief Format message with user_id prefix for multi-user context
   */
  [[nodiscard]] auto format_message(const MessageRecord &record) -> std::string;

  /**
   * @brief Trim context to fit within character limits
   */
  [[nodiscard]] auto trim_context(const std::vector<MessageRecord> &history)
      -> std::vector<MessageRecord>;
};

} // namespace plugins::chat_llm
