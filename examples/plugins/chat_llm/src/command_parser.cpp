#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include <chat_llm/command_parser.hpp>

namespace plugins::chat_llm {

auto CommandParser::parse(const obcx::core::IBot &bot,
                          const obcx::common::MessageEvent &event)
    -> ParsedCommand {
  ParsedCommand cmd;
  cmd.platform = detect_platform(bot);
  cmd.group_id = event.group_id.value_or("");
  cmd.user_id = event.user_id;
  cmd.message_id = event.message_id;

  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    return cmd;
  }

  // Extract text from message segments
  std::string text_content = extract_text(event.message);

  // Check if message starts with /chat
  if (!text_content.starts_with("/chat")) {
    return cmd;
  }

  // Parse user text: remove "/chat" prefix and trim
  std::string user_text = text_content.substr(5); // Remove "/chat"

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
    cmd.type = ParsedCommand::Type::chat;
    cmd.text = "用法: /chat <内容>";
    cmd.is_valid = true;
    return cmd;
  }

  cmd.type = ParsedCommand::Type::chat;
  cmd.text = user_text;
  cmd.is_valid = true;
  return cmd;
}

auto CommandParser::extract_text(const obcx::common::Message &msg)
    -> std::string {
  std::string result;
  for (const auto &segment : msg) {
    if (segment.type == "text") {
      result += segment.data["text"].get<std::string>();
    }
  }
  return result;
}

auto CommandParser::detect_platform(const obcx::core::IBot &bot)
    -> std::string {
  if (dynamic_cast<const obcx::core::QQBot *>(&bot) != nullptr) {
    return "qq";
  }
  return "telegram";
}

} // namespace plugins::chat_llm
