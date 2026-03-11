#include <chat_llm/prompt_builder.hpp>

#include <nlohmann/json.hpp>
#include <sstream>

namespace plugins::chat_llm {

PromptBuilder::PromptBuilder(std::string system_prompt, int max_reply_chars)
    : system_prompt_(std::move(system_prompt)), history_limit_(10),
      max_reply_chars_(max_reply_chars) {}

auto PromptBuilder::build(const std::vector<MessageRecord> &history,
                          const std::string &user_id,
                          const std::string &user_text,
                          const std::string &self_id,
                          const nlohmann::json &tools)
    -> std::vector<OpenAiMessage> {
  std::vector<OpenAiMessage> messages;

  // Add system prompt
  messages.push_back({MessageRole::system, system_prompt_});
  const auto tool_instruction = build_tool_instruction(tools);
  if (!tool_instruction.empty()) {
    messages.push_back({MessageRole::system, tool_instruction});
  }

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

auto PromptBuilder::build_proactive(const std::vector<MessageRecord> &history,
                                    const std::string &self_id,
                                    const nlohmann::json &tools)
    -> std::vector<OpenAiMessage> {
  std::vector<OpenAiMessage> messages;

  // Add system prompt
  messages.push_back({MessageRole::system, system_prompt_});
  const auto tool_instruction = build_tool_instruction(tools);
  if (!tool_instruction.empty()) {
    messages.push_back({MessageRole::system, tool_instruction});
  }

  // Add proactive context: tell the LLM this is a timer-triggered call
  messages.push_back(
      {MessageRole::system,
       "This is an automatic timer-triggered call, not a user-initiated conversation. "
       "You may choose not to respond. If you decide to speak, you **MUST** call the "
       "send_message tool to send your message. Do NOT output plain text directly. "
       "If you decide not to speak, do NOT call any tools and do NOT output any text."});

  // Trim history
  auto trimmed = trim_context(history);

  // Add history messages
  for (const auto &record : trimmed) {
    auto role = determine_role(record, self_id);
    auto content = format_message(record);
    messages.push_back({role, content});
  }

  // Add proactive decision instruction as the final user message.
  // This tells the LLM to review the conversation and decide whether
  // it is appropriate to chime in proactively.
  messages.push_back(
      {MessageRole::system,
       "Above is the recent chat history of the group. Review these messages and "
       "decide whether it is appropriate for you to proactively join the conversation. "
       "If you have something to say (e.g. the topic interests you, someone mentioned "
       "you, there is a question you can help with, or you want to make a witty remark), "
       "call the send_message tool. If you have nothing to say, the conversation has "
       "ended, or you have spoken recently, do NOT call any tools. "
       "Maintain your usual speaking style."});

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

auto PromptBuilder::build_tool_instruction(const nlohmann::json &tools) const
    -> std::string {
  if (tools.empty()) {
    return "";
  }

  std::ostringstream oss;
  oss << "Tool calling instructions:\n";
  oss << "- Use tools through structured tool calls, not by describing the call in plain text.\n";
  oss << "- When a tool is the correct action, call it directly with valid JSON arguments.\n";
  oss << "- Do not invent undeclared arguments.\n";

  for (const auto &tool : tools) {
    const auto &function = tool["function"];
    const auto &parameters = function["parameters"];
    oss << "- Tool `" << function["name"].get<std::string>() << "`: "
        << function["description"].get<std::string>() << "\n";
    oss << "  Arguments schema: " << parameters.dump() << "\n";
  }

  return oss.str();
}

} // namespace plugins::chat_llm
