#include <chat_llm/runtime.hpp>

namespace plugins::chat_llm {

Runtime::Runtime(boost::asio::any_io_executor executor, RuntimeConfig config)
    : config_(std::move(config)), timer_(executor) {}

void Runtime::stop() {
  stopping_.store(true);
  timer_.cancel();
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

} // namespace plugins::chat_llm
