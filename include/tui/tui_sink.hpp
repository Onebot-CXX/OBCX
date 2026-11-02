#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/sinks/base_sink.h>

namespace obcx::common {

struct LogLine {
  uint64_t sequence{0};
  std::string text;
  std::string stripped_text; // ANSI 已去除的缓存
  spdlog::level::level_enum level;
};

struct LogSnapshot {
  uint64_t version{0};
  uint64_t first_sequence{0};
  uint64_t next_sequence{0};
  std::vector<LogLine> lines;
};

/// Plain display/source text: remove ANSI CSI, OSC and control strings, not
/// only SGR colors. Unterminated sequences fail closed; UTF-8 bytes, spaces,
/// tabs and explicit newlines outside sequences are preserved.
inline auto strip_ansi(const std::string &input) -> std::string {
  enum class State { Text, Escape, Intermediate, Csi, String, StringEscape };
  auto state = State::Text;
  bool string_accepts_bel = false;
  std::string result;
  result.reserve(input.size());
  for (const unsigned char ch : input) {
    switch (state) {
    case State::Text:
      if (ch == 0x1b) {
        state = State::Escape;
      } else if (ch == '\n' || ch == '\t' || (ch >= 0x20 && ch != 0x7f)) {
        result.push_back(static_cast<char>(ch));
      }
      break;
    case State::Escape:
      if (ch == '[') {
        state = State::Csi;
      } else if (ch == ']' || ch == 'P' || ch == 'X' || ch == '^' ||
                 ch == '_') {
        string_accepts_bel = ch == ']';
        state = State::String;
      } else if (ch >= 0x20 && ch <= 0x2f) {
        state = State::Intermediate;
      } else if (ch >= 0x30 && ch <= 0x7e) {
        state = State::Text;
      }
      break;
    case State::Intermediate:
      if (ch >= 0x30 && ch <= 0x7e) {
        state = State::Text;
      }
      break;
    case State::Csi:
      if (ch >= 0x40 && ch <= 0x7e) {
        state = State::Text;
      }
      break;
    case State::String:
      if (ch == 0x07 && string_accepts_bel) {
        state = State::Text;
      } else if (ch == 0x1b) {
        state = State::StringEscape;
      }
      break;
    case State::StringEscape:
      state = ch == '\\' || (ch == 0x07 && string_accepts_bel) ? State::Text
              : ch == 0x1b ? State::StringEscape
                           : State::String;
      break;
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
    std::scoped_lock lock(lines_mutex_);
    return {lines_.begin(), lines_.end()};
  }

  auto get_lines_from(std::size_t offset) -> std::vector<LogLine> {
    std::scoped_lock lock(lines_mutex_);
    if (offset >= lines_.size()) {
      return {};
    }
    return {lines_.begin() + static_cast<std::ptrdiff_t>(offset), lines_.end()};
  }

  /// 获取指定范围的行 [offset, offset+count)
  auto get_lines_range(std::size_t offset, std::size_t count)
      -> std::vector<LogLine> {
    std::scoped_lock lock(lines_mutex_);
    auto begin = std::min(offset, lines_.size());
    auto end = std::min(begin + count, lines_.size());
    return {lines_.begin() + static_cast<std::ptrdiff_t>(begin),
            lines_.begin() + static_cast<std::ptrdiff_t>(end)};
  }

  auto line_count() -> std::size_t {
    std::scoped_lock lock(lines_mutex_);
    return lines_.size();
  }

  /// Return retained metadata and entries whose sequence is at least `from`.
  auto snapshot_from(uint64_t from) -> LogSnapshot {
    std::scoped_lock lock(lines_mutex_);
    const uint64_t first_sequence =
        lines_.empty() ? next_sequence_ : lines_.front().sequence;
    const uint64_t begin_sequence = std::max(from, first_sequence);
    const auto begin_offset = static_cast<std::size_t>(
        std::min(begin_sequence, next_sequence_) - first_sequence);
    const auto begin =
        lines_.begin() + static_cast<std::ptrdiff_t>(begin_offset);
    return LogSnapshot{
        .version = version_.load(std::memory_order_relaxed),
        .first_sequence = first_sequence,
        .next_sequence = next_sequence_,
        .lines = {begin, lines_.end()},
    };
  }

  /// 版本号，每次写入递增
  [[nodiscard]] auto version() const -> uint64_t {
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

    std::scoped_lock lock(lines_mutex_);
    lines_.push_back(LogLine{.sequence = next_sequence_++,
                             .text = std::move(line),
                             .stripped_text = std::move(stripped),
                             .level = msg.level});
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
  uint64_t next_sequence_{0};
  std::atomic<uint64_t> version_{0};
};

class tui_sink_mt final : public tui_sink<std::mutex> {
public:
  using tui_sink<std::mutex>::tui_sink;
};

} // namespace obcx::common
