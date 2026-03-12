#pragma once

#include "common/message_type.hpp"
#include "interfaces/bot.hpp"
#include <memory>
#include <string>
#include <vector>

namespace plugins::chat_llm {

/**
 * @brief Parsed command from a MessageEvent
 */
struct ParsedCommand {
  enum class Type { none, chat };

  Type type = Type::none;
  std::string text;
  bool is_valid = false;

  // Platform info (for filtering/context)
  std::string platform;
  std::string group_id;
  std::string user_id;
  std::string self_id;
  std::string message_id;
};

/**
 * @brief Parse message events to extract commands and user input
 * Handles platform-specific message formats (QQ at mentions, Telegram entities)
 */
class CommandParser {
public:
  CommandParser() = default;
  ~CommandParser() = default;

  /**
   * @brief Parse a message event
   * @param bot The bot instance for platform detection
   * @param event The message event to parse
   * @return ParsedCommand containing command type and extracted text
   */
  [[nodiscard]] auto parse(const obcx::core::IBot &bot,
                           const obcx::common::MessageEvent &event)
      -> ParsedCommand;

private:
  /**
   * @brief Extract plain text content from message segments
   */
  [[nodiscard]] auto extract_text(const obcx::common::Message &msg)
      -> std::string;

  /**
   * @brief Detect platform from bot instance
   */
  [[nodiscard]] auto detect_platform(const obcx::core::IBot &bot)
      -> std::string;
};

} // namespace plugins::chat_llm
