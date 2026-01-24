#include <chat_llm/runtime.hpp>

namespace plugins::chat_llm {

Runtime::Runtime(boost::asio::any_io_executor executor, RuntimeConfig config)
    : config_(std::move(config)), timer_(executor) {}

void Runtime::stop() {
  bool expected = false;
  if (stopping_.compare_exchange_strong(expected, true)) {
    timer_.cancel();
  }
}

void Runtime::schedule_cleanup_task(std::function<void()> task) {
  if (is_stopping()) {
    return;
  }

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

} // namespace plugins::chat_llm
