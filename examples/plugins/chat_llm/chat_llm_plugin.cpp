#include "chat_llm_plugin.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <chrono>
#include <common/config_loader.hpp>
#include <common/logger.hpp>
#include <core/qq_bot.hpp>
#include <core/tg_bot.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

namespace plugins {

ChatLLMPlugin::ChatLLMPlugin() {
  static int instance_counter = 0;
  instance_id_ = ++instance_counter;
}

ChatLLMPlugin::~ChatLLMPlugin() { shutdown(); }

auto ChatLLMPlugin::get_name() const -> std::string { return "chat_llm"; }

auto ChatLLMPlugin::get_version() const -> std::string { return "2.0.0"; }

auto ChatLLMPlugin::get_description() const -> std::string {
  return "Chat LLM plugin - provides /chat command for LLM interaction "
         "(refactored)";
}

auto ChatLLMPlugin::initialize() -> bool {
  // Determine base directory (repo root) from config path
  auto config_path = obcx::common::ConfigLoader::instance().get_config_path();
  base_dir_ = std::filesystem::path(config_path).parent_path().string();
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

  // Initialize components
  cmd_parser_ = std::make_unique<chat_llm::CommandParser>();
  prompt_builder_ =
      std::make_unique<chat_llm::PromptBuilder>(system_prompt_, 500);
  prompt_builder_->set_max_reply_chars(runtime_config_.max_reply_chars);
  prompt_builder_->set_history_limit(runtime_config_.history_limit);

  // Initialize database
  auto db_path = std::filesystem::path(base_dir_) / history_db_path_;

  repo_ = std::make_unique<chat_llm::MessageRepository>(db_path.string());
  if (!repo_->initialize()) {
    PLUGIN_ERROR(get_name(), "Failed to initialize database");
    return false;
  }

  // Register event callbacks for both QQ and Telegram bots
  auto [lock, bots] = get_bots();
  for (auto &bot_ptr : bots) {
    auto *bot_ref = bot_ptr.get();
    bot_ref->on_event<obcx::common::MessageEvent>(
        [this](obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
            -> boost::asio::awaitable<void> {
          co_await process_message(bot, event);
        });

    // Eagerly create runtime for each bot so that the proactive timer
    // starts immediately, rather than waiting for the first message.
    // Note: bot may not be connected yet at plugin init time (e.g. TGBot
    // requires connection_manager to be set up first), so we catch and
    // defer to lazy init in process_message().
    if (runtime_config_.proactive_enabled) {
      try {
        ensure_runtime(*bot_ref);
      } catch (const std::exception &e) {
        PLUGIN_WARN(get_name(),
                    "Could not eagerly create runtime for bot ({}). "
                    "Proactive timer will start on first message.",
                    e.what());
      }
    }
  }

  PLUGIN_INFO(get_name(), "Registered message callbacks");

  PLUGIN_INFO(get_name(), "Chat LLM Plugin initialized successfully");
  PLUGIN_INFO(get_name(), "  Model: {}", model_name_);
  PLUGIN_INFO(get_name(), "  URL: {}:{}{}", url_host_, url_port_, url_path_);
  PLUGIN_INFO(get_name(), "  Max reply chars: {}",
              runtime_config_.max_reply_chars);
  return true;
}

void ChatLLMPlugin::deinitialize() {
  PLUGIN_INFO(get_name(), "Deinitializing Chat LLM Plugin...");
  stop_all_runtimes();
  PLUGIN_INFO(get_name(), "Chat LLM Plugin deinitialized successfully");
}

void ChatLLMPlugin::shutdown() {
  PLUGIN_INFO(get_name(), "Shutting down Chat LLM Plugin...");
  stop_all_runtimes();
  PLUGIN_INFO(get_name(), "Chat LLM Plugin shutdown complete");
}

auto ChatLLMPlugin::load_configuration() -> bool {
  auto config_table = get_config_table();
  if (!config_table.has_value()) {
    PLUGIN_ERROR(get_name(), "No config found at [plugins.chat_llm.config]");
    return false;
  }

  const auto &config = config_table.value();

  // Required fields
  model_url_ = config["model_url"].value<std::string>().value();
  model_name_ = config["model_name"].value<std::string>().value();
  api_key_ = config["api_key"].value<std::string>().value();
  prompt_path_ = config["prompt_path"].value<std::string>().value();
  history_db_path_ = config["history_db_path"].value<std::string>().value();
  PLUGIN_INFO(get_name(), "History DB path configured: {}", history_db_path_);

  runtime_config_.max_reply_chars =
      static_cast<int>(config["max_reply_chars"].value<int64_t>().value());
  runtime_config_.history_limit =
      static_cast<int>(config["history_limit"].value<int64_t>().value());
  runtime_config_.history_ttl_days =
      static_cast<int>(config["history_ttl_days"].value<int64_t>().value());
  runtime_config_.collect_enabled =
      config["collect_enabled"].value<bool>().value();
  runtime_config_.collect_allowed_groups =
      get_config_value<std::vector<std::string>>("collect_allowed_groups")
          .value();
  runtime_config_.cleanup_interval = std::chrono::milliseconds(
      config["cleanup_interval_ms"].value<int64_t>().value());
  max_tool_steps_ =
      static_cast<int>(config["max_tool_steps"].value<int64_t>().value());

  // Proactive chat settings
  runtime_config_.proactive_enabled =
      config["proactive_enabled"].value<bool>().value();
  runtime_config_.proactive_interval = std::chrono::milliseconds(
      config["proactive_interval_ms"].value<int64_t>().value());
  runtime_config_.proactive_groups =
      get_config_value<std::vector<std::string>>("proactive_groups").value();

  PLUGIN_INFO(get_name(),
              "Final config: collect_enabled={}, history_limit={}, "
              "max_reply_chars={}, history_ttl_days={}, max_tool_steps={}, "
              "proactive_enabled={}, proactive_interval_ms={}",
              runtime_config_.collect_enabled, runtime_config_.history_limit,
              runtime_config_.max_reply_chars,
              runtime_config_.history_ttl_days, max_tool_steps_,
              runtime_config_.proactive_enabled,
              runtime_config_.proactive_interval.count());
  PLUGIN_INFO(get_name(), "Configuration loaded successfully");
  return true;
}

auto ChatLLMPlugin::load_system_prompt() -> bool {
  auto prompt_full_path = std::filesystem::path(base_dir_) / prompt_path_;

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

  PLUGIN_INFO(get_name(), "Loaded system prompt ({} chars)",
              system_prompt_.size());

  return true;
}

auto ChatLLMPlugin::parse_url(const std::string &url) -> bool {
  std::regex url_regex(R"(^(https?)://([^/:]+)(?::(\d+))?(/.*)$)",
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

  url_use_ssl_ = (scheme == "https" || scheme == "HTTPS");

  if (!port_str.empty()) {
    url_port_ = static_cast<uint16_t>(std::stoi(port_str));
  } else {
    url_port_ = url_use_ssl_ ? 443 : 80;
  }

  PLUGIN_DEBUG(get_name(),
               "Parsed URL - host: {}, port: {}, path: {}, ssl: {}",
               url_host_, url_port_, url_path_, url_use_ssl_);

  return true;
}

auto ChatLLMPlugin::get_llm_tools() const -> nlohmann::json {
  nlohmann::json def;
  def["type"] = "function";
  def["function"] = {
      {"name", "send_message"},
      {"description",
       "Send a message to the current group immediately. Use this when you "
       "need to send a message."},
      {"parameters",
       {
           {"type", "object"},
           {"properties", {{"text", {{"type", "string"}}}}},
           {"required", nlohmann::json::array({"text"})},
           {"additionalProperties", false},
       }},
  };
  return nlohmann::json::array({def});
}

auto ChatLLMPlugin::execute_tool_call(
    obcx::core::IBot &bot, const chat_llm::ParsedCommand &cmd,
    const chat_llm::LlmResponse::ToolCall &tool_call)
    -> boost::asio::awaitable<nlohmann::json> {
  auto args = nlohmann::json::parse(tool_call.arguments);
  const auto text = args["text"].get<std::string>();

  co_await send_response(bot, cmd, text);

  nlohmann::json result;
  result["tool"] = tool_call.name;
  result["call_id"] = tool_call.id;
  result["ok"] = true;
  result["sent"] = true;
  result["length"] = text.size();
  co_return result;
}

auto ChatLLMPlugin::execute_tool_call_for_test(
    obcx::core::IBot &bot, const chat_llm::ParsedCommand &cmd,
    const chat_llm::LlmResponse::ToolCall &tool_call)
    -> boost::asio::awaitable<nlohmann::json> {
  co_return co_await execute_tool_call(bot, cmd, tool_call);
}

auto ChatLLMPlugin::process_message(obcx::core::IBot &bot,
                                    const obcx::common::MessageEvent &event)
    -> boost::asio::awaitable<void> {
  auto rt = ensure_runtime(bot);
  const auto &rt_config = rt->get_config();

  // Cache bot's self_id from the first message event we see
  if (!event.self_id.empty()) {
    std::lock_guard<std::mutex> lock(runtimes_mutex_);
    bot_self_ids_[&bot] = event.self_id;
  }

  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  PLUGIN_INFO(get_name(), "Processing message from group: {}",
              event.group_id.value());
  if (!rt_config.collect_allowed_groups.empty()) {
    std::string groups_str;
    for (size_t i = 0; i < rt_config.collect_allowed_groups.size(); ++i) {
      if (i > 0)
        groups_str += ", ";
      groups_str += rt_config.collect_allowed_groups[i];
    }
    PLUGIN_DEBUG(get_name(), "Allowed groups: {}", groups_str);
  }

  // Parse command
  auto cmd = cmd_parser_->parse(bot, event);

  // Save text message to database (if collection enabled)
  const std::string &group_id = event.group_id.value();
  const std::string &user_id = event.user_id;
  const std::string &self_id = event.self_id;
  const std::string &message_id = event.message_id;
  std::string platform = cmd.platform;

  // Get text content
  std::string text_content;
  for (const auto &segment : event.message) {
    if (segment.type == "text") {
      text_content += segment.data["text"].get<std::string>();
    }
  }

  // Only process /chat commands or save non-command messages
  bool is_chat_command =
      (cmd.type == chat_llm::ParsedCommand::Type::chat && cmd.is_valid);

  // Save non-command text messages to history immediately
  if (!text_content.empty() && !is_chat_command) {
    int64_t timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            event.time.time_since_epoch())
            .count();

    chat_llm::MessageRecord record;
    record.platform = platform;
    record.group_id = group_id;
    record.message_id = message_id;
    record.user_id = user_id;
    record.content = text_content;
    record.timestamp_ms = timestamp_ms;
    record.is_bot = event.data["from"]["is_bot"].get<bool>();
    record.is_command = false;

    co_await bot.run_heavy_task([this, record, &rt_config]() {
      return repo_->append_message(record, rt_config.collect_enabled,
                                   rt_config.collect_allowed_groups);
    });
  }

  // Only process /chat commands
  if (!is_chat_command) {
    co_return;
  }

  // Step 1: Query database to build LLM request
  int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  chat_llm::ContextQuery ctx_query;
  ctx_query.platform = platform;
  ctx_query.group_id = group_id;
  ctx_query.before_timestamp_ms = now_ms;
  ctx_query.limit = rt_config.history_limit;

  auto history = co_await bot.run_heavy_task(
      [this, ctx_query]() { return repo_->fetch_context(ctx_query); });

  // Convert history records to MessageRecord format
  std::vector<chat_llm::MessageRecord> history_records;
  for (const auto &item : history) {
    chat_llm::MessageRecord rec;
    rec.platform = item.platform;
    rec.group_id = item.group_id;
    rec.user_id = item.user_id;
    rec.content = item.content;
    rec.timestamp_ms = item.timestamp_ms;
    rec.is_bot = item.is_bot;
    history_records.push_back(rec);
  }

  // Step 2: Insert /chat command to history (use cmd.text without prefix)
  int64_t cmd_timestamp_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          event.time.time_since_epoch())
          .count();

  chat_llm::MessageRecord cmd_record;
  cmd_record.platform = platform;
  cmd_record.group_id = group_id;
  cmd_record.message_id = message_id;
  cmd_record.user_id = user_id;
  cmd_record.content = cmd.text;
  cmd_record.timestamp_ms = cmd_timestamp_ms;
  cmd_record.is_bot = event.data["from"]["is_bot"].get<bool>();
  cmd_record.is_command = true;

  co_await bot.run_heavy_task([this, cmd_record, &rt_config]() {
    return repo_->append_message(cmd_record, rt_config.collect_enabled,
                                 rt_config.collect_allowed_groups);
  });

  // Step 3: Build messages for LLM
  auto openai_messages =
      prompt_builder_->build(history_records, user_id, cmd.text, self_id);
  std::vector<nlohmann::json> llm_messages;
  llm_messages.reserve(openai_messages.size());
  for (const auto &msg : openai_messages) {
    nlohmann::json m;
    m["role"] = (msg.role == chat_llm::MessageRole::system)
                    ? "system"
                    : (msg.role == chat_llm::MessageRole::user) ? "user"
                                                                : "assistant";
    m["content"] = msg.content;
    llm_messages.push_back(std::move(m));
  }

  nlohmann::json tool_policy_msg;
  tool_policy_msg["role"] = "system";
  tool_policy_msg["content"] =
      "You must respond by calling the send_message tool. Do not output plain text answers.";
  llm_messages.push_back(std::move(tool_policy_msg));

  // Step 4: Call LLM API (with tool-calling loop)
  const auto tools = get_llm_tools();
  bool sent_via_tool = false;
  int plain_text_rounds = 0;
  std::string last_error;
  for (int step = 0; step < max_tool_steps_; ++step) {
    auto llm_response =
        co_await bot.run_heavy_task([this, llm_messages, tools]() {
          boost::asio::io_context ioc;
          chat_llm::OpenAiCompatClient::Config client_config;
          client_config.model_name = model_name_;
          client_config.api_key = api_key_;
          client_config.host = url_host_;
          client_config.port = url_port_;
          client_config.path = url_path_;
          client_config.use_ssl = url_use_ssl_;
          client_config.timeout = std::chrono::milliseconds(120000);

          chat_llm::OpenAiCompatClient client(ioc, client_config);
          return client.chat_completion(llm_messages, tools);
        });

    PLUGIN_INFO(get_name(), "LLM step {} returned {} tool calls",
                step + 1, llm_response.tool_calls.size());

    if (!llm_response.success) {
      PLUGIN_ERROR(get_name(), "LLM request failed: {}", llm_response.error_message);
      last_error = llm_response.error_message;
      break;
    }

    if (llm_response.tool_calls.empty()) {
      if (!llm_response.content.empty()) {
        plain_text_rounds++;
        PLUGIN_WARN(get_name(),
                    "LLM returned plain text without tool call; asking model to call send_message");
        // Append the assistant's plain-text reply so the model sees its own
        // output and the conversation stays well-formed (alternating roles).
        nlohmann::json assistant_plain;
        assistant_plain["role"] = "assistant";
        assistant_plain["content"] = llm_response.content;
        llm_messages.push_back(std::move(assistant_plain));

        nlohmann::json reminder;
        reminder["role"] = "user";
        reminder["content"] =
            "Do not answer with plain text. To reply to user, you MUST call the send_message tool.";
        llm_messages.push_back(std::move(reminder));
      }
      continue;
    }

    nlohmann::json assistant_msg;
    assistant_msg["role"] = "assistant";
    if (llm_response.content.empty()) {
      assistant_msg["content"] = nullptr;
    } else {
      assistant_msg["content"] = llm_response.content;
    }
    assistant_msg["tool_calls"] = nlohmann::json::array();
    for (const auto &call : llm_response.tool_calls) {
      nlohmann::json tool_call_json;
      tool_call_json["id"] = call.id;
      tool_call_json["type"] = "function";
      tool_call_json["function"] = {
          {"name", call.name},
          {"arguments", call.arguments},
      };
      assistant_msg["tool_calls"].push_back(std::move(tool_call_json));
    }
    llm_messages.push_back(std::move(assistant_msg));

    bool sent_this_round = false;
    for (const auto &call : llm_response.tool_calls) {
      const auto tool_result = co_await execute_tool_call(bot, cmd, call);
      if (tool_result["sent"].get<bool>()) {
        sent_via_tool = true;
        sent_this_round = true;
      }
      nlohmann::json tool_msg;
      tool_msg["role"] = "tool";
      tool_msg["tool_call_id"] = call.id;
      tool_msg["content"] = tool_result.dump();
      llm_messages.push_back(std::move(tool_msg));
    }

    if (sent_this_round) {
      PLUGIN_INFO(get_name(), "send_message executed at step {}, stopping loop",
                  step + 1);
      break;
    }
  }

  if (!sent_via_tool) {
    PLUGIN_WARN(get_name(),
                "No send_message tool call produced within {} steps for "
                "message {}. plain_text_rounds={}, last_error={}",
                max_tool_steps_, cmd.message_id, plain_text_rounds,
                last_error.empty() ? "(none)" : last_error);
  }
}

auto ChatLLMPlugin::send_response(obcx::core::IBot &bot,
                                  const chat_llm::ParsedCommand &cmd,
                                  const std::string &text)
    -> boost::asio::awaitable<void> {
  auto rt = ensure_runtime(bot);

  obcx::common::Message message;

    if (cmd.platform == "telegram" && !cmd.message_id.empty()) {
      obcx::common::MessageSegment reply_segment;
      reply_segment.type = "reply";
      reply_segment.data["id"] = cmd.message_id;
      message.push_back(reply_segment);
    }

    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    message.push_back(text_segment);

    co_await bot.send_group_message(cmd.group_id, message);

    // Save bot's reply to database
    int64_t timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    std::string message_id =
        "local-bot-" + std::to_string(timestamp_ms) + "-" + cmd.group_id;

    chat_llm::MessageRecord bot_rec;
    bot_rec.platform = cmd.platform;
    bot_rec.group_id = cmd.group_id;
    bot_rec.message_id = message_id;
    bot_rec.user_id = cmd.self_id;
    bot_rec.content = text;
    bot_rec.timestamp_ms = timestamp_ms;
    bot_rec.is_bot = true;
    bot_rec.is_command = false;

    const auto &rt_config = rt->get_config();
    co_await bot.run_heavy_task([this, bot_rec, &rt_config]() {
      return repo_->append_message(bot_rec, rt_config.collect_enabled,
                                   rt_config.collect_allowed_groups);
    });
}

auto ChatLLMPlugin::send_proactive_message(obcx::core::IBot &bot,
                                           const std::string &platform,
                                           const std::string &group_id,
                                           const std::string &text)
    -> boost::asio::awaitable<void> {
  auto rt = ensure_runtime(bot);

  obcx::common::Message message;
  obcx::common::MessageSegment text_segment;
  text_segment.type = "text";
  text_segment.data["text"] = text;
  message.push_back(text_segment);

  co_await bot.send_group_message(group_id, message);

  // Save bot's proactive reply to database
  int64_t timestamp_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  std::string message_id =
      "local-bot-proactive-" + std::to_string(timestamp_ms) + "-" + group_id;

  // Look up cached self_id for this bot
  std::string self_id;
  {
    std::lock_guard<std::mutex> lock(runtimes_mutex_);
    self_id = bot_self_ids_.at(&bot);
  }

  chat_llm::MessageRecord bot_rec;
  bot_rec.platform = platform;
  bot_rec.group_id = group_id;
  bot_rec.message_id = message_id;
  bot_rec.user_id = self_id;
  bot_rec.content = text;
  bot_rec.timestamp_ms = timestamp_ms;
  bot_rec.is_bot = true;
  bot_rec.is_command = false;

  const auto &rt_config = rt->get_config();
  co_await bot.run_heavy_task([this, bot_rec, &rt_config]() {
    return repo_->append_message(bot_rec, rt_config.collect_enabled,
                                 rt_config.collect_allowed_groups);
  });
}

auto ChatLLMPlugin::run_proactive_for_group(obcx::core::IBot &bot,
                                            const std::string &platform,
                                            const std::string &group_id)
    -> boost::asio::awaitable<void> {
  auto rt = ensure_runtime(bot);
  const auto &rt_config = rt->get_config();

  PLUGIN_INFO(get_name(), "Proactive check for group {} (platform={})",
              group_id, platform);

  // Step 1: Fetch recent history for this group
  int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  chat_llm::ContextQuery ctx_query;
  ctx_query.platform = platform;
  ctx_query.group_id = group_id;
  ctx_query.before_timestamp_ms = now_ms;
  ctx_query.limit = rt_config.history_limit;

  auto history = co_await bot.run_heavy_task(
      [this, ctx_query]() { return repo_->fetch_context(ctx_query); });

  // Skip if no recent messages to analyze
  if (history.empty()) {
    PLUGIN_INFO(get_name(),
                "Proactive: no history for group {}, skipping", group_id);
    co_return;
  }

  // Only ask the LLM whether to speak when the latest visible message in the
  // group is from another user. If the bot was the last speaker, stay quiet
  // until somebody else talks.
  const auto &last_message = history.back();
  if (last_message.is_bot) {
    PLUGIN_DEBUG(get_name(),
                 "Proactive: latest message in group {} is from bot, "
                 "skipping LLM call",
                 group_id);
    co_return;
  }

  PLUGIN_INFO(get_name(),
              "Proactive: fetched {} history messages for group {}",
              history.size(), group_id);

  // Step 2: Build proactive prompt (no user message, just history + decision instruction)
  auto openai_messages = prompt_builder_->build_proactive(history, "");
  std::vector<nlohmann::json> llm_messages;
  llm_messages.reserve(openai_messages.size());
  for (const auto &msg : openai_messages) {
    nlohmann::json m;
    m["role"] = (msg.role == chat_llm::MessageRole::system)
                    ? "system"
                    : (msg.role == chat_llm::MessageRole::user) ? "user"
                                                                 : "assistant";
    m["content"] = msg.content;
    llm_messages.push_back(std::move(m));
  }

  // Step 3: Call LLM with tool_choice="auto" (NOT forced)
  // The LLM decides whether to call send_message or stay silent.
  const auto tools = get_llm_tools();

  auto llm_response = co_await bot.run_heavy_task(
      [this, llm_messages, tools]() {
        boost::asio::io_context ioc;
        chat_llm::OpenAiCompatClient::Config client_config;
        client_config.model_name = model_name_;
        client_config.api_key = api_key_;
        client_config.host = url_host_;
        client_config.port = url_port_;
        client_config.path = url_path_;
        client_config.use_ssl = url_use_ssl_;
        client_config.timeout = std::chrono::milliseconds(120000);

        chat_llm::OpenAiCompatClient client(ioc, client_config);
        // force_tool=false → tool_choice="auto"
        return client.chat_completion(llm_messages, tools, false);
      });

  if (!llm_response.success) {
    PLUGIN_ERROR(get_name(), "Proactive LLM request failed for group {}: {}",
                 group_id, llm_response.error_message);
    co_return;
  }

  // Log full LLM response for debugging
  PLUGIN_INFO(get_name(),
              "Proactive: LLM response for group {} — "
              "content=\"{}\" tool_calls={} response_size={}",
              group_id,
              llm_response.content.empty() ? "(empty)" : llm_response.content,
              llm_response.tool_calls.size(),
              llm_response.response_size);

  // Step 4: If LLM chose NOT to call any tool, it means it decided to stay silent
  if (llm_response.tool_calls.empty()) {
    PLUGIN_INFO(get_name(),
                "Proactive: LLM decided NOT to speak in group {} — "
                "reason: \"{}\"",
                group_id,
                llm_response.content.empty()
                    ? "(no content returned)"
                    : llm_response.content);
    co_return;
  }

  // Step 5: Execute tool calls (only send_message)
  for (const auto &call : llm_response.tool_calls) {
    if (call.name != "send_message") {
      PLUGIN_WARN(get_name(),
                  "Proactive: unexpected tool call '{}' in group {}, skipping",
                  call.name, group_id);
      continue;
    }

    auto args = nlohmann::json::parse(call.arguments);
    const auto text = args["text"].get<std::string>();

    PLUGIN_INFO(get_name(),
                "Proactive: sending message to group {} (len={})",
                group_id, text.size());
    co_await send_proactive_message(bot, platform, group_id, text);
  }
}

auto ChatLLMPlugin::ensure_runtime(obcx::core::IBot &bot)
    -> std::shared_ptr<chat_llm::Runtime> {
  std::lock_guard<std::mutex> lock(runtimes_mutex_);

  auto it = runtimes_.find(&bot);
  if (it != runtimes_.end()) {
    return it->second;
  }

  auto executor = bot.get_task_scheduler().get_io_context().get_executor();
  auto runtime = std::make_shared<chat_llm::Runtime>(executor, runtime_config_);

  // Schedule TTL cleanup task immediately
  runtime->schedule_cleanup_task([this, weak_rt = std::weak_ptr(runtime)]() {
    auto rt = weak_rt.lock();
    if (!rt || rt->is_stopping()) {
      return;
    }
    repo_->cleanup_ttl(runtime_config_.history_ttl_days);
  });

  // Schedule proactive chat task if enabled
  if (runtime_config_.proactive_enabled &&
      !runtime_config_.proactive_groups.empty()) {
    PLUGIN_INFO(get_name(),
                "Scheduling proactive chat timer (interval={}ms, groups={})",
                runtime_config_.proactive_interval.count(),
                runtime_config_.proactive_groups.size());

    runtime->schedule_proactive_task(
        [this, &bot, weak_rt = std::weak_ptr(runtime)]() {
          auto rt = weak_rt.lock();
          if (!rt || rt->is_stopping()) {
            return;
          }

          // Detect platform from bot type
          std::string platform =
              (dynamic_cast<obcx::core::QQBot *>(&bot) != nullptr) ? "qq"
                                                                    : "telegram";

          const auto &proactive_groups = rt->get_config().proactive_groups;

          // Spawn a coroutine for the proactive check
          boost::asio::co_spawn(
              rt->get_executor(),
              [this, &bot, platform, proactive_groups,
               weak_rt2 = std::weak_ptr(rt)]()
                  -> boost::asio::awaitable<void> {
                auto rt2 = weak_rt2.lock();
                if (!rt2 || rt2->is_stopping()) {
                  co_return;
                }

                for (const auto &group_id : proactive_groups) {
                  if (rt2->is_stopping()) {
                    co_return;
                  }

                  co_await run_proactive_for_group(bot, platform, group_id);
                }
              },
              boost::asio::detached);
        });
  }

  runtimes_[&bot] = runtime;
  return runtime;
}

void ChatLLMPlugin::stop_all_runtimes() {
  std::lock_guard<std::mutex> lock(runtimes_mutex_);
  for (auto &[_, rt] : runtimes_) {
    rt->stop();
  }
  runtimes_.clear();
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::ChatLLMPlugin)
