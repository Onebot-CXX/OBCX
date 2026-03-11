#pragma once

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace plugins::chat_llm {

struct RuntimeConfig {
  bool collect_enabled = false;
  std::vector<std::string> collect_allowed_groups;
  int history_ttl_days = 1;
  int history_limit = 10;
  int max_reply_chars = 500;
  std::chrono::milliseconds llm_watchdog{120000};
  std::chrono::milliseconds cleanup_interval{3600000}; // 1 hour

  // Proactive chat settings
  bool proactive_enabled = false;
  std::chrono::milliseconds proactive_interval{300000}; // 5 minutes default
  std::vector<std::string> proactive_groups; // groups to proactively chat in


};

/**
 * @brief Shared runtime state for background tasks
 * Ensures safe lifecycle management during hot reload/unload
 */
class Runtime {
public:
  Runtime(boost::asio::any_io_executor executor, RuntimeConfig config);

  void stop();

  [[nodiscard]] auto is_stopping() const -> bool { return stopping_.load(); }
  [[nodiscard]] auto get_config() const -> const RuntimeConfig & {
    return config_;
  }
  [[nodiscard]] auto get_executor() -> boost::asio::any_io_executor {
    return timer_.get_executor();
  }

  void schedule_cleanup_task(std::function<void()> task);

  /**
   * @brief Schedule a periodic proactive chat task
   * @param task The task to run periodically (should check history and
   *             optionally send a message)
   */
  void schedule_proactive_task(std::function<void()> task);

private:
  std::atomic<bool> stopping_{false};
  RuntimeConfig config_;

  // Timer for periodic cleanup (TTL)
  boost::asio::steady_timer timer_;

  // Timer for periodic proactive chat
  boost::asio::steady_timer proactive_timer_;
};

} // namespace plugins::chat_llm
