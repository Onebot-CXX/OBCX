#include <chat_llm/runtime.hpp>
#include <common/logger.hpp>

namespace plugins::chat_llm {

Runtime::Runtime(boost::asio::any_io_executor executor, RuntimeConfig config)
    : config_(std::move(config)), timer_(executor), proactive_timer_(executor) {
}

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
  proactive_timer_.async_wait([this,
                               task](const boost::system::error_code &ec) {
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

} // namespace plugins::chat_llm
