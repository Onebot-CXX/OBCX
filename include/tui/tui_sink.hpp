#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/base_sink.h>

namespace obcx::common {

struct LogLine {
  std::string text;
  std::string stripped_text; // ANSI 已去除的缓存
  spdlog::level::level_enum level;
};

/// 高性能 ANSI 转义序列去除（手写状态机，无正则）
inline auto strip_ansi(const std::string &input) -> std::string {
  std::string result;
  result.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\033' && i + 1 < input.size() && input[i + 1] == '[') {
      i += 2; // 跳过 ESC[
      while (i < input.size() && input[i] != 'm') {
        ++i;
      }
      // 'm' 会被 loop increment 跳过
    } else {
      result.push_back(input[i]);
    }
  }
  return result;
}

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

  /// 获取指定范围的行 [offset, offset+count)
  auto get_lines_range(std::size_t offset, std::size_t count)
      -> std::vector<LogLine> {
    std::lock_guard lock(lines_mutex_);
    auto begin = std::min(offset, lines_.size());
    auto end = std::min(begin + count, lines_.size());
    return {lines_.begin() + static_cast<std::ptrdiff_t>(begin),
            lines_.begin() + static_cast<std::ptrdiff_t>(end)};
  }

  auto line_count() -> std::size_t {
    std::lock_guard lock(lines_mutex_);
    return lines_.size();
  }

  /// 版本号，每次写入递增
  auto version() const -> uint64_t {
    return version_.load(std::memory_order_relaxed);
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

    // 预计算 stripped 文本
    std::string stripped = strip_ansi(line);

    std::lock_guard lock(lines_mutex_);
    lines_.push_back(LogLine{std::move(line), std::move(stripped), msg.level});
    while (lines_.size() > max_lines_) {
      lines_.pop_front();
    }
    version_.fetch_add(1, std::memory_order_relaxed);
  }

  void flush_() override {}

private:
  std::size_t max_lines_;
  std::deque<LogLine> lines_;
  std::mutex lines_mutex_;
  std::atomic<uint64_t> version_{0};
};

using tui_sink_mt = tui_sink<std::mutex>;

} // namespace obcx::common
