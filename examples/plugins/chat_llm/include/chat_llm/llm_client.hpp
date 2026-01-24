#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace plugins::chat_llm {

/**
 * @brief Response from LLM API
 */
struct LlmResponse {
  bool success = false;
  std::string content;
  std::string error_message;
  unsigned int status_code = 0;
  size_t response_size = 0;
};

/**
 * @brief Interface for LLM API clients
 */
class LlmClient {
public:
  virtual ~LlmClient() = default;

  /**
   * @brief Send chat completion request
   * @param messages OpenAI-formatted messages array
   * @return Response from LLM API
   */
  [[nodiscard]] virtual auto chat_completion(
      const std::vector<std::string> &messages_json) -> LlmResponse {
    throw std::runtime_error("chat_completion not implemented");
  }
};

/**
 * @brief OpenAI-compatible LLM client
 *
 * Uses HttpClient for HTTP requests.
 * Supports timeout and basic error handling.
 * Does NOT add vendor-specific fields by default.
 */
class OpenAiCompatClient : public LlmClient {
public:
  /**
   * @brief Configuration for OpenAI-compatible client
   */
  struct Config {
    std::string model_name;
    std::string api_key;
    std::string host;
    uint16_t port = 443;
    std::string path;
    bool use_ssl = true;
    std::chrono::milliseconds timeout = std::chrono::milliseconds{120000};
  };

  explicit OpenAiCompatClient(boost::asio::io_context &ioc, Config config);

  /**
   * @brief Send chat completion request (synchronous, called from thread pool)
   */
  [[nodiscard]] auto chat_completion(
      const std::vector<std::string> &messages_json) -> LlmResponse override;

  /**
   * @brief Set request timeout
   */
  void set_timeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }

private:
  boost::asio::io_context &ioc_;
  Config config_;
  std::chrono::milliseconds timeout_;

  /**
   * @brief Build JSON request body
   */
  [[nodiscard]] auto build_request_body(
      const std::vector<std::string> &messages_json) -> std::string;
};

} // namespace plugins::chat_llm
