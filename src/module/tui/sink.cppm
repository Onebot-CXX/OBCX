module;

import std;


#include <spdlog/sinks/base_sink.h>
export module tui.sink;
export namespace obcx::tui {

struct LogLine {
  std::string text;
  spdlog::level::level_enum level;
};

/**
 * \if CHINESE
 * @brief 自定义spdlog sink，将日志消息存储在环形缓冲区中供TUI渲染
 * \endif
 * \if ENGLISH
 * @brief Custom spdlog sink that stores log messages in a ring buffer for TUI
 * rendering
 * \endif
 */
template <typename Mutex>
class tui_sink : public spdlog::sinks::base_sink<Mutex> {
public:
  explicit tui_sink(std::size_t max_lines = 5000) : max_lines_(max_lines) {}

  auto get_lines() -> std::vector<LogLine> {
    std::lock_guard lock(lines_mutex_);
    return {lines_.begin(), lines_.end()};
  }

  auto get_lines_from(std::size_t offset) -> std::vector<LogLine> {
    std::lock_guard lock(lines_mutex_);
    if (offset >= lines_.size()) {
      return {};
    }
    return {lines_.begin() + static_cast<std::ptrdiff_t>(offset), lines_.end()};
  }

  auto line_count() -> std::size_t {
    std::lock_guard lock(lines_mutex_);
    return lines_.size();
  }

protected:
  void sink_it_(const spdlog::details::log_msg &msg) override {
    spdlog::memory_buf_t formatted;
    spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
    std::string line = fmt::to_string(formatted);

    // Remove trailing newline if present
    if (!line.empty() && line.back() == '\n') {
      line.pop_back();
    }

    std::lock_guard lock(lines_mutex_);
    lines_.push_back(LogLine{std::move(line), msg.level});
    while (lines_.size() > max_lines_) {
      lines_.pop_front();
    }
  }

  void flush_() override {}

private:
  std::size_t max_lines_;
  std::deque<LogLine> lines_;
  std::mutex lines_mutex_;
};

using tui_sink_mt = tui_sink<std::mutex>;

} // namespace obcx::common
