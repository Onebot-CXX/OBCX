#include <chat_llm/runtime.hpp>
#include <common/logger.hpp>

namespace plugins::chat_llm {

Runtime::Runtime(boost::asio::any_io_executor executor, RuntimeConfig config)
    : config_(std::move(config)), timer_(executor), proactive_timer_(executor) {}

void Runtime::stop() {
  stopping_.store(true);
  timer_.cancel();
  proactive_timer_.cancel();
}

void Runtime::schedule_cleanup_task(std::function<void()> task) {
  timer_.expires_after(config_.cleanup_interval);
  timer_.async_wait([this, task](const boost::system::error_code &ec) {
    if (ec == boost::asio::error::operation_aborted) {
      return;
    }
    task();
    if (!is_stopping()) {
      schedule_cleanup_task(task);
    }
  });
}

void Runtime::schedule_proactive_task(std::function<void()> task) {
  PLUGIN_INFO("runtime", "Proactive timer scheduled (interval={}ms)",
              config_.proactive_interval.count());
  proactive_timer_.expires_after(config_.proactive_interval);
  proactive_timer_.async_wait([this, task](const boost::system::error_code &ec) {
    if (ec == boost::asio::error::operation_aborted) {
      PLUGIN_INFO("runtime", "Proactive timer cancelled (operation_aborted)");
      return;
    }
    PLUGIN_INFO("runtime", "Proactive timer fired");
    task();
    if (!is_stopping()) {
      schedule_proactive_task(task);
    }
  });
}

auto Runtime::get_last_proactive_ts(const std::string &group_id) -> int64_t {
  std::lock_guard<std::mutex> lock(proactive_ts_mutex_);
  auto it = last_proactive_ts_.find(group_id);
  return (it != last_proactive_ts_.end()) ? it->second : 0;
}

void Runtime::set_last_proactive_ts(const std::string &group_id, int64_t ts) {
  std::lock_guard<std::mutex> lock(proactive_ts_mutex_);
  last_proactive_ts_[group_id] = ts;
}

} // namespace plugins::chat_llm
