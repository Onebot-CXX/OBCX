#include <chat_llm/prompt_builder.hpp>

namespace plugins::chat_llm {

PromptBuilder::PromptBuilder(std::string system_prompt, int max_reply_chars)
    : system_prompt_(std::move(system_prompt)), history_limit_(10),
      max_reply_chars_(max_reply_chars) {}

auto PromptBuilder::build(const std::vector<MessageRecord> &history,
                          const std::string &user_id,
                          const std::string &user_text,
                          const std::string &self_id)
    -> std::vector<OpenAiMessage> {
  std::vector<OpenAiMessage> messages;

  // Add system prompt
  messages.push_back({MessageRole::system, system_prompt_});

  // Deduplicate and trim history
  auto deduped = deduplicate_history(history, user_id, user_text);
  auto trimmed = trim_context(deduped);

  // Add history messages
  for (const auto &record : trimmed) {
    auto role = determine_role(record, self_id);
    auto content = format_message(record);
    messages.push_back({role, content});
  }

  // Add current user message
  auto current_content = user_id + ": " + user_text;
  messages.push_back({MessageRole::user, current_content});

  return messages;
}

auto PromptBuilder::is_response_too_long(const std::string &response) const
    -> bool {
  return static_cast<int>(response.size()) > max_reply_chars_;
}

auto PromptBuilder::deduplicate_history(
    const std::vector<MessageRecord> &history, const std::string &user_id,
    const std::string &user_text) -> std::vector<MessageRecord> {
  std::vector<MessageRecord> result;

  // If last history message is identical to current, skip it
  bool skip_last = false;
  if (!history.empty()) {
    const auto &last = history.back();
    // Last message from same user with same content?
    if (last.user_id == user_id && last.content == user_text && !last.is_bot) {
      skip_last = true;
    }
  }

  result = history;
  if (skip_last && !result.empty()) {
    result.pop_back();
  }

  return result;
}

auto PromptBuilder::determine_role(const MessageRecord &record,
                                   const std::string &self_id) -> MessageRole {
  (void)self_id; // Unused, determined by is_bot field
  if (record.is_bot) {
    return MessageRole::assistant;
  }
  return MessageRole::user;
}

auto PromptBuilder::format_message(const MessageRecord &record) -> std::string {
  if (record.is_bot) {
    return record.content;
  }
  return record.user_id + ": " + record.content;
}

auto PromptBuilder::trim_context(const std::vector<MessageRecord> &history)
    -> std::vector<MessageRecord> {
  // Simple limit-based trimming
  // Future enhancement: token-aware trimming

  if (static_cast<int>(history.size()) <= history_limit_) {
    return history;
  }

  // Keep most recent messages (history is oldest-first, so take from end)
  size_t start = history.size() - history_limit_;
  return std::vector<MessageRecord>(history.begin() + start, history.end());
}

} // namespace plugins::chat_llm
