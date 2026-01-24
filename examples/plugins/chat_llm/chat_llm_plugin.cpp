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
  PLUGIN_INFO(get_name(), "ChatLLMPlugin constructor called (instance_id: {})",
              instance_id_);
}

ChatLLMPlugin::~ChatLLMPlugin() {
  PLUGIN_INFO(get_name(), "ChatLLMPlugin destructor called (instance_id: {})",
              instance_id_);
  shutdown();
}

auto ChatLLMPlugin::get_name() const -> std::string { return "chat_llm"; }

auto ChatLLMPlugin::get_version() const -> std::string { return "2.0.0"; }

auto ChatLLMPlugin::get_description() const -> std::string {
  return "Chat LLM plugin - provides /chat command for LLM interaction "
         "(refactored)";
}

auto ChatLLMPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing Chat LLM Plugin (instance_id: {})",
                instance_id_);

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

    // Initialize components
    cmd_parser_ = std::make_unique<chat_llm::CommandParser>();
    prompt_builder_ =
        std::make_unique<chat_llm::PromptBuilder>(system_prompt_, 500);

    // Initialize database
    std::filesystem::path db_path;
    if (std::filesystem::path(history_db_path_).is_absolute()) {
      db_path = history_db_path_;
    } else {
      db_path = std::filesystem::path(base_dir_) / history_db_path_;
    }

    repo_ = std::make_unique<chat_llm::MessageRepository>(db_path.string());
    if (!repo_->initialize()) {
      PLUGIN_ERROR(get_name(), "Failed to initialize database");
      return false;
    }

    PLUGIN_INFO(get_name(),
                "Config after database init: collect_enabled={}, "
                "allowed_groups_count={}",
                runtime_config_.collect_enabled,
                runtime_config_.collect_allowed_groups.size());

    // Register event callbacks for both QQ and Telegram bots
    auto [lock, bots] = get_bots();
    for (auto &bot_ptr : bots) {
      auto *bot_ref = bot_ptr.get();
      bot_ref->on_event<obcx::common::MessageEvent>(
          [this](obcx::core::IBot &bot, const obcx::common::MessageEvent &event)
              -> boost::asio::awaitable<void> {
            co_await process_message(bot, event);
          });
    }

    PLUGIN_INFO(get_name(), "Registered message callbacks");

    // Final verification before returning
    PLUGIN_INFO(get_name(),
                "Final config check before init complete: collect_enabled={}, "
                "allowed_groups_count={}",
                runtime_config_.collect_enabled,
                runtime_config_.collect_allowed_groups.size());

    PLUGIN_INFO(get_name(), "Chat LLM Plugin initialized successfully");
    PLUGIN_INFO(get_name(), "  Model: {}", model_name_);
    PLUGIN_INFO(get_name(), "  URL: {}:{}{}", url_host_, url_port_, url_path_);
    PLUGIN_INFO(get_name(), "  Max reply chars: {}",
                runtime_config_.max_reply_chars);
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during initialization: {}", e.what());
    return false;
  }
}

void ChatLLMPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing Chat LLM Plugin...");
    stop_all_runtimes();
    PLUGIN_INFO(get_name(), "Chat LLM Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during deinitialization: {}", e.what());
  }
}

void ChatLLMPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down Chat LLM Plugin...");
    stop_all_runtimes();
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

    if (auto val = config["history_db_path"].value<std::string>()) {
      history_db_path_ = *val;
      PLUGIN_INFO(get_name(), "History DB path configured: {}",
                  history_db_path_);
    } else {
      PLUGIN_ERROR(get_name(), "Missing required config: history_db_path");
      return false;
    }

    // Optional fields
    if (auto val = config["max_reply_chars"].value<int64_t>()) {
      if (prompt_builder_) {
        prompt_builder_->set_max_reply_chars(static_cast<int>(*val));
      }
      runtime_config_.max_reply_chars = static_cast<int>(*val);
      PLUGIN_DEBUG(get_name(), "Loaded max_reply_chars: {}", *val);
    }

    if (auto val = config["history_limit"].value<int64_t>()) {
      if (prompt_builder_) {
        prompt_builder_->set_history_limit(static_cast<int>(*val));
      }
      runtime_config_.history_limit = static_cast<int>(*val);
      PLUGIN_DEBUG(get_name(), "Loaded history_limit: {}", *val);
    }

    if (auto val = config["history_ttl_days"].value<int64_t>()) {
      runtime_config_.history_ttl_days = static_cast<int>(*val);
      PLUGIN_DEBUG(get_name(), "Loaded history_ttl_days: {}", *val);
    }

    if (auto val = config["collect_enabled"].value<bool>()) {
      runtime_config_.collect_enabled = *val;
      PLUGIN_INFO(get_name(), "Loaded collect_enabled: {}", *val);
    } else {
      PLUGIN_INFO(get_name(),
                  "collect_enabled not found in config, using default: {}",
                  runtime_config_.collect_enabled);
    }

    if (auto val = get_config_value<std::vector<std::string>>(
            "collect_allowed_groups")) {
      runtime_config_.collect_allowed_groups = *val;
      PLUGIN_INFO(get_name(), "Loaded collect_allowed_groups (count: {})",
                  val->size());
    }

    if (auto val = config["cleanup_interval_ms"].value<int64_t>()) {
      runtime_config_.cleanup_interval =
          std::chrono::milliseconds(static_cast<int64_t>(*val));
      PLUGIN_DEBUG(get_name(), "Loaded cleanup_interval_ms: {}", *val);
    }

    PLUGIN_INFO(get_name(),
                "Final config: collect_enabled={}, history_limit={}, "
                "max_reply_chars={}, history_ttl_days={}",
                runtime_config_.collect_enabled, runtime_config_.history_limit,
                runtime_config_.max_reply_chars,
                runtime_config_.history_ttl_days);
    PLUGIN_INFO(get_name(), "Configuration loaded successfully");

    // Immediately verify config is still correct (debug potential memory
    // corruption)
    PLUGIN_INFO(get_name(),
                "Config verification after load_configuration(): "
                "collect_enabled={}, allowed_groups_count={}",
                runtime_config_.collect_enabled,
                runtime_config_.collect_allowed_groups.size());
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load configuration: {}", e.what());
    return false;
  }
}

auto ChatLLMPlugin::load_system_prompt() -> bool {
  try {
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

auto ChatLLMPlugin::parse_url(const std::string &url) -> bool {
  try {
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

    url_use_ssl_ = (scheme == "https" || scheme == "HTTPS");

    if (!port_str.empty()) {
      url_port_ = static_cast<uint16_t>(std::stoi(port_str));
    } else {
      url_port_ = url_use_ssl_ ? 443 : 80;
    }

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
  auto rt = ensure_runtime(bot);
  if (!rt || rt->is_stopping()) {
    co_return;
  }

  const auto &rt_config = rt->get_config();
  PLUGIN_DEBUG(get_name(),
               "process_message using instance_id={}, runtime config: "
               "collect_enabled={}, allowed_groups_count={}",
               instance_id_, rt_config.collect_enabled,
               rt_config.collect_allowed_groups.size());

  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  PLUGIN_INFO(get_name(), "Processing message from group: {}",
              event.group_id.value());
  PLUGIN_DEBUG(get_name(),
               "Collection config: collect_enabled={}, allowed_groups_count={}",
               rt_config.collect_enabled,
               rt_config.collect_allowed_groups.size());
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
    if (segment.type == "text" && segment.data.contains("text")) {
      try {
        text_content += segment.data["text"].get<std::string>();
      } catch (...) {
      }
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
    record.is_bot = false;
    try {
      if (event.data.contains("from") &&
          event.data["from"].contains("is_bot")) {
        record.is_bot = event.data["from"]["is_bot"].get<bool>();
      }
    } catch (...) {
    }
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
  cmd_record.is_bot = false;
  try {
    if (event.data.contains("from") && event.data["from"].contains("is_bot")) {
      cmd_record.is_bot = event.data["from"]["is_bot"].get<bool>();
    }
  } catch (...) {
  }
  cmd_record.is_command = true;

  co_await bot.run_heavy_task([this, cmd_record, &rt_config]() {
    return repo_->append_message(cmd_record, rt_config.collect_enabled,
                                 rt_config.collect_allowed_groups);
  });

  // Step 3: Build messages for LLM
  auto messages =
      prompt_builder_->build(history_records, user_id, cmd.text, self_id);

  // Convert to JSON strings
  std::vector<std::string> messages_json;
  nlohmann::json sys_msg;
  sys_msg["role"] = "system";
  sys_msg["content"] = prompt_builder_->get_system_prompt();
  messages_json.push_back(sys_msg.dump());

  for (const auto &msg : messages) {
    nlohmann::json m;
    m["role"] = (msg.role == chat_llm::MessageRole::system) ? "system"
                : (msg.role == chat_llm::MessageRole::user) ? "user"
                                                            : "assistant";
    m["content"] = msg.content;
    messages_json.push_back(m.dump());
  }

  // Step 4: Call LLM API
  auto response_text =
      co_await bot.run_heavy_task([this, messages_json]() -> std::string {
        // Create LLM client with new io_context per call (safer)
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
        auto response = client.chat_completion(messages_json);

        if (!response.success) {
          return response.error_message;
        }
        return response.content;
      });

  // Check response length
  if (prompt_builder_->is_response_too_long(response_text)) {
    PLUGIN_INFO(get_name(), "LLM response too long ({} chars), truncating",
                response_text.size());
    response_text = "LLM 返回过长（" + std::to_string(response_text.size()) +
                    " chars），已记录到日志";
  }

  // Step 5: Send response
  co_await send_response(bot, cmd, response_text);
}

auto ChatLLMPlugin::send_response(obcx::core::IBot &bot,
                                  const chat_llm::ParsedCommand &cmd,
                                  const std::string &text)
    -> boost::asio::awaitable<void> {
  auto rt = ensure_runtime(bot);
  if (!rt || rt->is_stopping()) {
    co_return;
  }

  try {
    obcx::common::Message message;
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
    bot_rec.user_id = cmd.user_id; // Use self_id ideally
    bot_rec.content = text;
    bot_rec.timestamp_ms = timestamp_ms;
    bot_rec.is_bot = true;
    bot_rec.is_command = false;

    const auto &rt_config = rt->get_config();
    co_await bot.run_heavy_task([this, bot_rec, &rt_config]() {
      return repo_->append_message(bot_rec, rt_config.collect_enabled,
                                   rt_config.collect_allowed_groups);
    });

  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send response: {}", e.what());
  }
}

auto ChatLLMPlugin::ensure_runtime(obcx::core::IBot &bot)
    -> std::shared_ptr<chat_llm::Runtime> {
  std::lock_guard<std::mutex> lock(runtimes_mutex_);

  auto it = runtimes_.find(&bot);
  if (it != runtimes_.end() && it->second && !it->second->is_stopping()) {
    return it->second;
  }

  PLUGIN_INFO(get_name(),
              "Creating new Runtime for bot (instance_id={}) with config: "
              "collect_enabled={}, allowed_groups_count={}",
              instance_id_, runtime_config_.collect_enabled,
              runtime_config_.collect_allowed_groups.size());

  auto executor = bot.get_task_scheduler().get_io_context().get_executor();
  auto runtime = std::make_shared<chat_llm::Runtime>(executor, runtime_config_);

  // Verify config in runtime
  const auto &runtime_cfg = runtime->get_config();
  PLUGIN_INFO(get_name(),
              "Runtime created (instance_id={}) with actual config: "
              "collect_enabled={}, allowed_groups_count={}",
              instance_id_, runtime_cfg.collect_enabled,
              runtime_cfg.collect_allowed_groups.size());

  // Schedule TTL cleanup task immediately
  runtime->schedule_cleanup_task([this, weak_rt = std::weak_ptr(runtime)]() {
    auto rt = weak_rt.lock();
    if (!rt || rt->is_stopping()) {
      return;
    }
    try {
      repo_->cleanup_ttl(runtime_config_.history_ttl_days);
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Cleanup task failed: {}", e.what());
    }
  });

  runtimes_[&bot] = runtime;
  return runtime;
}

void ChatLLMPlugin::stop_all_runtimes() {
  std::lock_guard<std::mutex> lock(runtimes_mutex_);
  for (auto &[_, rt] : runtimes_) {
    if (rt) {
      rt->stop();
    }
  }
  runtimes_.clear();
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::ChatLLMPlugin)
