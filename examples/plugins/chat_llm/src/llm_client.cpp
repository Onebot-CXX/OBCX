#include "common/logger.hpp"
#include "network/http_client.hpp"
#include <chat_llm/llm_client.hpp>
#include <nlohmann/json.hpp>

namespace plugins::chat_llm {

OpenAiCompatClient::OpenAiCompatClient(boost::asio::io_context &ioc,
                                       Config config)
    : ioc_(ioc), config_(std::move(config)), timeout_(config.timeout) {}

auto OpenAiCompatClient::chat_completion(
    const std::vector<std::string> &messages_json) -> LlmResponse {
  LlmResponse result;

  try {
    // Build request body
    std::string body = build_request_body(messages_json);

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

    PLUGIN_DEBUG("llm_client", "Sending LLM request to {}:{}{} (body size: {})",
                 config_.host, config_.port, config_.path, body.size());

    // Send request
    auto response = http_client.post_sync(config_.path, body, headers);

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
    if (!first_choice.contains("message") ||
        !first_choice["message"].contains("content")) {
      PLUGIN_ERROR("llm_client", "LLM response missing message.content");
      result.success = false;
      result.error_message = "LLM 响应格式错误";
      return result;
    }

    result.success = true;
    result.content = first_choice["message"]["content"].get<std::string>();
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
    const std::vector<std::string> &messages_json) -> std::string {
  nlohmann::json request_body;
  request_body["model"] = config_.model_name;
  request_body["stream"] = false;
  request_body["messages"] = nlohmann::json::array();

  for (const auto &msg_json : messages_json) {
    try {
      request_body["messages"].push_back(nlohmann::json::parse(msg_json));
    } catch (...) {
      PLUGIN_WARN("llm_client", "Failed to parse message JSON, skipping");
    }
  }

  return request_body.dump();
}

} // namespace plugins::chat_llm
