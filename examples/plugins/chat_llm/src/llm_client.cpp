#include "common/logger.hpp"
#include "network/http_client.hpp"
#include <chat_llm/llm_client.hpp>
#include <nlohmann/json.hpp>

namespace plugins::chat_llm {

OpenAiCompatClient::OpenAiCompatClient(boost::asio::io_context &ioc,
                                       Config config)
    : ioc_(ioc), config_(std::move(config)), timeout_(config.timeout) {}

auto OpenAiCompatClient::chat_completion(
    const std::vector<nlohmann::json> &messages,
    const nlohmann::json &tools) -> LlmResponse {
  LlmResponse result;

  try {
    // Create HTTP client
    obcx::common::ConnectionConfig conn_config;
    conn_config.host = config_.host;
    conn_config.port = config_.port;
    conn_config.use_ssl = config_.use_ssl;

    obcx::network::HttpClient http_client(ioc_, conn_config);
    http_client.set_timeout(timeout_);

    // Build headers
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + config_.api_key;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json";
    headers["Accept-Encoding"] = "identity";

    auto post_with_mode = [&](ToolRequestMode mode) {
      std::string body = build_request_body(messages, tools, mode);
      PLUGIN_DEBUG("llm_client",
                   "Sending LLM request to {}:{}{} (mode: {}, body size: {})",
                   config_.host, config_.port, config_.path,
                   mode == ToolRequestMode::forced_function
                       ? "forced"
                       : mode == ToolRequestMode::auto_choice ? "auto" : "legacy",
                   body.size());
      return http_client.post_sync(config_.path, body, headers);
    };

    auto response = post_with_mode(ToolRequestMode::forced_function);
    if (!response.is_success() && tools.is_array() && !tools.empty()) {
      PLUGIN_WARN("llm_client",
                  "Forced tool_choice failed with HTTP {}, retrying with auto",
                  response.status_code);
      response = post_with_mode(ToolRequestMode::auto_choice);
    }
    if (!response.is_success() && tools.is_array() && !tools.empty()) {
      PLUGIN_WARN("llm_client",
                  "Auto tool_choice failed with HTTP {}, retrying with legacy function_call",
                  response.status_code);
      response = post_with_mode(ToolRequestMode::legacy_function_call);
    }

    PLUGIN_DEBUG("llm_client", "LLM API response status: {}",
                 response.status_code);
    PLUGIN_INFO("llm_client", "LLM API response size: {} bytes",
                response.body.size());

    if (!response.is_success()) {
      result.success = false;
      result.status_code = response.status_code;
      result.error_message =
          "LLM API 错误 (HTTP " + std::to_string(response.status_code) + ")";
      PLUGIN_ERROR("llm_client", "LLM API returned error: status={}",
                   response.status_code);
      return result;
    }

    // Parse response
    nlohmann::json response_json;
    try {
      response_json = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::parse_error &e) {
      PLUGIN_ERROR("llm_client", "Failed to parse LLM response JSON: {}",
                   e.what());
      result.success = false;
      result.error_message = "LLM 响应解析失败";
      return result;
    }

    // Check for error in response
    if (response_json.contains("error")) {
      std::string error_msg = "LLM API 错误";
      if (response_json["error"].contains("message")) {
        error_msg +=
            ": " + response_json["error"]["message"].get<std::string>();
      }
      PLUGIN_ERROR("llm_client", "LLM API returned error: {}",
                   response.body.substr(0, 200));
      result.success = false;
      result.error_message = error_msg;
      return result;
    }

    // Extract assistant message
    if (!response_json.contains("choices") ||
        !response_json["choices"].is_array() ||
        response_json["choices"].empty()) {
      PLUGIN_ERROR("llm_client", "LLM response missing choices array");
      result.success = false;
      result.error_message = "LLM 响应格式错误";
      return result;
    }

    auto &first_choice = response_json["choices"][0];
    if (!first_choice.contains("message") || !first_choice["message"].is_object()) {
      PLUGIN_ERROR("llm_client", "LLM response missing message object");
      result.success = false;
      result.error_message = "LLM 响应格式错误";
      return result;
    }

    auto &message = first_choice["message"];
    if (message.contains("content") && message["content"].is_string()) {
      result.content = message["content"].get<std::string>();
    }

    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
      for (const auto &item : message["tool_calls"]) {
        if (!item.is_object()) {
          continue;
        }
        if (!item.contains("id") || !item["id"].is_string()) {
          continue;
        }
        if (!item.contains("function") || !item["function"].is_object()) {
          continue;
        }
        const auto &fn = item["function"];
        if (!fn.contains("name") || !fn["name"].is_string()) {
          continue;
        }

        LlmResponse::ToolCall call;
        call.id = item["id"].get<std::string>();
        call.name = fn["name"].get<std::string>();
        if (fn.contains("arguments") && fn["arguments"].is_string()) {
          call.arguments = fn["arguments"].get<std::string>();
        }
        result.tool_calls.push_back(std::move(call));
      }
    }

    if (result.tool_calls.empty() && message.contains("function_call") &&
        message["function_call"].is_object()) {
      const auto &fn = message["function_call"];
      if (fn.contains("name") && fn["name"].is_string()) {
        LlmResponse::ToolCall call;
        call.id = "legacy_function_call_0";
        call.name = fn["name"].get<std::string>();
        if (fn.contains("arguments") && fn["arguments"].is_string()) {
          call.arguments = fn["arguments"].get<std::string>();
        }
        result.tool_calls.push_back(std::move(call));
      }
    }

    result.success = true;
    result.response_size = result.content.size();

    return result;

  } catch (const std::exception &e) {
    PLUGIN_ERROR("llm_client", "LLM API call failed: {}", e.what());
    result.success = false;
    result.error_message = std::string("LLM 请求失败: ") + e.what();
    return result;
  }
}

auto OpenAiCompatClient::build_request_body(
    const std::vector<nlohmann::json> &messages,
    const nlohmann::json &tools, ToolRequestMode mode) -> std::string {
  nlohmann::json request_body;
  request_body["model"] = config_.model_name;
  request_body["thinking"] = {{"type", "disabled"}};
  request_body["stream"] = false;
  request_body["messages"] = messages;
  if (tools.is_array() && !tools.empty()) {
    if (mode == ToolRequestMode::legacy_function_call) {
      nlohmann::json functions = nlohmann::json::array();
      for (const auto &tool : tools) {
        if (!tool.is_object() || !tool.contains("function") ||
            !tool["function"].is_object()) {
          continue;
        }
        functions.push_back(tool["function"]);
      }
      request_body["functions"] = std::move(functions);
      request_body["function_call"] = {
          {"name", "send_message"},
      };
    } else {
      request_body["tools"] = tools;
      if (mode == ToolRequestMode::forced_function) {
        request_body["tool_choice"] = {
            {"type", "function"},
            {"function", {{"name", "send_message"}}},
        };
      } else {
        request_body["tool_choice"] = "auto";
      }
    }
  }

  return request_body.dump();
}

} // namespace plugins::chat_llm
