#pragma once

#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
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

private:
  std::atomic<bool> stopping_{false};
  RuntimeConfig config_;

  // Timer for periodic cleanup (TTL)
  boost::asio::steady_timer timer_;
};

} // namespace plugins::chat_llm
