#include "common/logger.hpp"
#include "network/http_client.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <chat_llm/llm_client.hpp>
#include <nlohmann/json.hpp>

namespace plugins::chat_llm {

OpenAiCompatClient::OpenAiCompatClient(boost::asio::io_context &ioc,
                                       Config config)
    : ioc_(ioc), config_(std::move(config)), timeout_(config.timeout) {}

auto OpenAiCompatClient::chat_completion(
    const std::vector<nlohmann::json> &messages, const nlohmann::json &tools,
    bool force_tool) -> LlmResponse {
  LlmResponse result;

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

  std::string body = build_request_body(messages, tools, force_tool);
  PLUGIN_DEBUG("llm_client", "Sending LLM request to {}:{}{} (body size: {})",
               config_.host, config_.port, config_.path, body.size());
  obcx::network::HttpResponse response;
  boost::asio::co_spawn(
      ioc_,
      [&]() -> boost::asio::awaitable<void> {
        response = co_await http_client.post(config_.path, body, headers);
      },
      boost::asio::detached);
  ioc_.run();
  ioc_.restart();

  PLUGIN_DEBUG("llm_client", "LLM API response status: {}",
               response.status_code);
  PLUGIN_INFO("llm_client", "LLM API response size: {} bytes",
              response.body.size());

  if (!response.is_success()) {
    result.success = false;
    result.status_code = response.status_code;
    result.error_message =
        "LLM API Error (HTTP " + std::to_string(response.status_code) + ")";
    PLUGIN_ERROR("llm_client", "LLM API returned error: status={}",
                 response.status_code);
    return result;
  }

  // Parse response
  auto response_json = nlohmann::json::parse(response.body);

  // Check for API-level error in response body
  if (response_json.contains("error")) {
    std::string error_msg =
        "LLM API Error: " +
        response_json["error"]["message"].get<std::string>();
    PLUGIN_ERROR("llm_client", "LLM API returned error: {}",
                 response.body.substr(0, 200));
    result.success = false;
    result.error_message = error_msg;
    return result;
  }

  // Extract assistant message
  auto &message = response_json["choices"][0]["message"];

  // content can be null when tool_calls is present (OpenAI API spec)
  if (!message["content"].is_null()) {
    std::string raw_content = message["content"].get<std::string>();

    // Strip <think>...</think> blocks (model reasoning output).
    // Some models (e.g. GLM-4) embed thinking in the content field
    // wrapped in <think> tags. We keep thinking enabled for better
    // decision-making but strip the tags from the final content.
    std::string cleaned;
    size_t pos = 0;
    while (pos < raw_content.size()) {
      auto open = raw_content.find("<think>", pos);
      if (open == std::string::npos) {
        cleaned.append(raw_content, pos);
        break;
      }
      cleaned.append(raw_content, pos, open - pos);
      auto close = raw_content.find("</think>", open);
      if (close == std::string::npos) {
        // Unclosed <think> tag — discard everything after it
        break;
      }
      pos = close + 8; // length of "</think>"
    }

    // Trim leading/trailing whitespace
    auto start = cleaned.find_first_not_of(" \t\n\r");
    auto end = cleaned.find_last_not_of(" \t\n\r");
    result.content = (start == std::string::npos)
                         ? ""
                         : cleaned.substr(start, end - start + 1);
  }

  // tool_calls is only present when the model decides to call tools (OpenAI API
  // spec)
  if (message.contains("tool_calls")) {
    for (const auto &item : message["tool_calls"]) {
      const auto &fn = item["function"];
      LlmResponse::ToolCall call;
      call.id = item["id"].get<std::string>();
      call.name = fn["name"].get<std::string>();
      call.arguments = fn["arguments"].get<std::string>();
      result.tool_calls.push_back(std::move(call));
    }
  }

  result.success = true;
  result.response_size = result.content.size();

  return result;
}

auto OpenAiCompatClient::build_request_body(
    const std::vector<nlohmann::json> &messages, const nlohmann::json &tools,
    bool force_tool) -> std::string {
  nlohmann::json request_body;
  request_body["model"] = config_.model_name;
  // request_body["thinking"] = {{"type", "disabled"}};
  request_body["stream"] = false;
  request_body["messages"] = messages;
  if (tools.is_array() && !tools.empty()) {
    request_body["tools"] = tools;
    if (force_tool) {
      request_body["tool_choice"] = {
          {"type", "function"},
          {"function", {{"name", "send_message"}}},
      };
    } else {
      request_body["tool_choice"] = "auto";
    }
  }

  return request_body.dump();
}

} // namespace plugins::chat_llm
