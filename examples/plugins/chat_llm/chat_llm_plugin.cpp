#include "chat_llm_plugin.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <common/config_loader.hpp>
#include <common/logger.hpp>
#include <core/qq_bot.hpp>
#include <core/tg_bot.hpp>
#include <filesystem>
#include <fstream>
#include <network/http_client.hpp>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

namespace plugins {

ChatLLMPlugin::ChatLLMPlugin() {
  PLUGIN_DEBUG(get_name(), "ChatLLMPlugin constructor called");
}

ChatLLMPlugin::~ChatLLMPlugin() {
  shutdown();
  PLUGIN_DEBUG(get_name(), "ChatLLMPlugin destructor called");
}

auto ChatLLMPlugin::get_name() const -> std::string { return "chat_llm"; }

auto ChatLLMPlugin::get_version() const -> std::string { return "1.0.0"; }

auto ChatLLMPlugin::get_description() const -> std::string {
  return "Chat LLM plugin - provides /chat command for LLM interaction";
}

auto ChatLLMPlugin::initialize() -> bool {
  try {
    PLUGIN_INFO(get_name(), "Initializing Chat LLM Plugin...");

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

    // Register event callbacks for both QQ and Telegram bots
    try {
      auto [lock, bots] = get_bots();

      for (auto &bot_ptr : bots) {
        // Register for QQ bot
        if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
          qq_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await process_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered QQ message callback");
        }

        // Register for Telegram bot
        if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
          tg_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await process_message(bot, event);
              });
          PLUGIN_INFO(get_name(), "Registered Telegram message callback");
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "Chat LLM Plugin initialized successfully");
    PLUGIN_INFO(get_name(), "  Model: {}", model_name_);
    PLUGIN_INFO(get_name(), "  URL: {}:{}{}", url_host_, url_port_, url_path_);
    PLUGIN_INFO(get_name(), "  Max reply chars: {}", max_reply_chars_);
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during initialization: {}", e.what());
    return false;
  }
}

void ChatLLMPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing Chat LLM Plugin...");
    PLUGIN_INFO(get_name(), "Chat LLM Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during deinitialization: {}", e.what());
  }
}

void ChatLLMPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down Chat LLM Plugin...");
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

    // Optional fields
    if (auto val = config["bot_nickname"].value<std::string>()) {
      bot_nickname_ = *val;
    }

    if (auto val = config["max_reply_chars"].value<int64_t>()) {
      max_reply_chars_ = *val;
    }

    PLUGIN_INFO(get_name(), "Configuration loaded successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load configuration: {}", e.what());
    return false;
  }
}

auto ChatLLMPlugin::load_system_prompt() -> bool {
  try {
    // Resolve prompt_path relative to base_dir (repo root)
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
    // Simple URL parsing: scheme://host[:port][/path]
    // Example: https://api.openai.com/v1/chat/completions

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

    // Determine SSL
    url_use_ssl_ = (scheme == "https" || scheme == "HTTPS");

    // Determine port
    if (!port_str.empty()) {
      url_port_ = static_cast<uint16_t>(std::stoi(port_str));
    } else {
      url_port_ = url_use_ssl_ ? 443 : 80;
    }

    // Default path
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
  auto process_start = std::chrono::steady_clock::now();
  PLUGIN_INFO(get_name(), "[TIMING] process_message START at: {} ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  process_start.time_since_epoch())
                  .count());

  // Only process group messages
  if (event.message_type != "group" || !event.group_id.has_value()) {
    co_return;
  }

  const std::string &group_id = event.group_id.value();
  const std::string &user_id = event.user_id;
  const std::string &raw_message = event.raw_message;

  // Check if message starts with /chat
  if (!raw_message.starts_with("/chat")) {
    co_return;
  }

  PLUGIN_DEBUG(get_name(), "Received /chat command from user {} in group {}",
               user_id, group_id);

  // Parse user text: remove "/chat" prefix and trim
  std::string user_text = raw_message.substr(5); // Remove "/chat"

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
    co_await send_response(bot, group_id, "用法: /chat <内容>");
    co_return;
  }

  PLUGIN_INFO(
      get_name(),
      "Processing /chat request from user {} in group {}, text length: {}",
      user_id, group_id, user_text.size());

  // Call LLM API in thread pool to avoid blocking
  std::string response;
  std::string error_msg;
  bool has_error = false;

  try {
    response = co_await bot.run_heavy_task(
        [this, user_text]() { return call_llm_api(user_text); });
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "LLM API call failed: {}", e.what());
    error_msg = std::string("LLM 请求失败: ") + e.what();
    has_error = true;
  }

  if (has_error) {
    co_await send_response(bot, group_id, error_msg);
    co_return;
  }

  // Check response length
  if (static_cast<int64_t>(response.size()) > max_reply_chars_) {
    PLUGIN_INFO(get_name(),
                "LLM response too long ({} chars), full response: {}",
                response.size(), response);
    co_await send_response(bot, group_id,
                           "LLM 返回过长（" + std::to_string(response.size()) +
                               " chars），已记录到日志");
    co_return;
  }

  // Send response
  co_await send_response(bot, group_id, response);

  auto process_end = std::chrono::steady_clock::now();
  auto process_duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(process_end -
                                                            process_start)
          .count();
  PLUGIN_INFO(get_name(), "[TIMING] process_message END after: {} ms (TOTAL)",
              process_duration_ms);
}

auto ChatLLMPlugin::call_llm_api(const std::string &user_text) -> std::string {
  auto start_time = std::chrono::steady_clock::now();
  PLUGIN_INFO(get_name(), "[TIMING] call_llm_api START at: {} ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  start_time.time_since_epoch())
                  .count());

  try {
    // Create HTTP client
    boost::asio::io_context ioc;
    obcx::common::ConnectionConfig config;
    config.host = url_host_;
    config.port = url_port_;
    config.use_ssl = url_use_ssl_;

    obcx::network::HttpClient http_client(ioc, config);

    // Build request body
    nlohmann::json request_body;
    request_body["model"] = model_name_;
    request_body["stream"] = false;
    request_body["messages"] = nlohmann::json::array();
    request_body["messages"].push_back(
        {{"role", "system"}, {"content", system_prompt_}});
    request_body["messages"].push_back(
        {{"role", "user"}, {"content", user_text}});

    std::string body = request_body.dump();

    PLUGIN_DEBUG(get_name(), "Sending LLM request to {}:{}{}", url_host_,
                 url_port_, url_path_);

    // Build headers
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + api_key_;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json";
    headers["Accept-Encoding"] = "identity";

    // Send request
    auto response = http_client.post_sync(url_path_, body, headers);

    PLUGIN_DEBUG(get_name(), "LLM API response status: {}",
                 response.status_code);
    PLUGIN_INFO(get_name(), "LLM API response body ({} chars): {}",
                response.body.size(), response.body);

    if (!response.is_success()) {
      return "LLM API 返回错误 (HTTP " + std::to_string(response.status_code) +
             ")";
    }

    // Parse response
    nlohmann::json response_json;
    try {
      response_json = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::parse_error &e) {
      PLUGIN_ERROR(get_name(), "Failed to parse LLM response JSON: {}",
                   e.what());
      return "LLM 响应解析失败";
    }

    // Check for error in response
    if (response_json.contains("error")) {
      std::string error_msg = "LLM API 错误";
      if (response_json["error"].contains("message")) {
        error_msg +=
            ": " + response_json["error"]["message"].get<std::string>();
      }
      PLUGIN_ERROR(get_name(), "LLM API returned error: {}", response.body);
      return error_msg;
    }

    // Extract assistant message
    if (!response_json.contains("choices") ||
        !response_json["choices"].is_array() ||
        response_json["choices"].empty()) {
      PLUGIN_ERROR(get_name(), "LLM response missing choices array");
      return "LLM 响应格式错误";
    }

    auto &first_choice = response_json["choices"][0];
    if (!first_choice.contains("message") ||
        !first_choice["message"].contains("content")) {
      PLUGIN_ERROR(get_name(), "LLM response missing message.content");
      return "LLM 响应格式错误";
    }

    return first_choice["message"]["content"].get<std::string>();

  } catch (const std::exception &e) {
    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           end_time - start_time)
                           .count();
    PLUGIN_ERROR(
        get_name(),
        "[TIMING] call_llm_api END (EXCEPTION) after: {} ms, error: {}",
        duration_ms, e.what());
    throw;
  }

  auto end_time = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         end_time - start_time)
                         .count();
  PLUGIN_INFO(get_name(), "[TIMING] call_llm_api END after: {} ms",
              duration_ms);
}

auto ChatLLMPlugin::send_response(obcx::core::IBot &bot,
                                  const std::string &group_id,
                                  const std::string &text)
    -> boost::asio::awaitable<void> {
  auto send_start_time = std::chrono::steady_clock::now();
  PLUGIN_INFO(get_name(), "[TIMING] send_response START at: {} ms, text: '{}'",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  send_start_time.time_since_epoch())
                  .count(),
              text.substr(0, std::min(size_t(50), text.size())));

  try {
    obcx::common::Message message;
    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    message.push_back(text_segment);

    co_await bot.send_group_message(group_id, message);
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to send response: {}", e.what());
  }

  auto send_end_time = std::chrono::steady_clock::now();
  auto send_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              send_end_time - send_start_time)
                              .count();
  PLUGIN_INFO(
      get_name(),
      "[TIMING] send_response END after: {} ms (bot.send_group_message)",
      send_duration_ms);
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::ChatLLMPlugin)
