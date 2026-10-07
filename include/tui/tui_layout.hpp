#pragma once

#include "tui/tui_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace obcx::common::tui_layout {

/// Wrap UTF-8 text to terminal-cell width without splitting glyphs.
[[nodiscard]] auto wrap_text(const std::string &text, int width)
    -> std::vector<std::string>;

struct LayoutSnapshot {
  int terminal_width{0};
  int terminal_height{0};
  int log_pane_height{0};
  int console_pane_height{0};
  int separator_y{0};
  int log_content_width{0};
  int log_content_height{0};
  int console_content_width{0};
  int console_content_height{0};
  int log_scrollbar_x{0};
  int log_content_top{0};
};

/// Keeps FTXUI's absolute divider row synchronized with a preferred ratio.
class SplitState {
public:
  explicit SplitState(double preferred_ratio = 0.7,
                      int minimum_pane_height = 3);

  /// Update geometry and observe any divider change made by FTXUI.
  [[nodiscard]] auto snapshot(int terminal_width, int terminal_height)
      -> LayoutSnapshot;

  /// Move the divider by terminal rows and record it as a user preference.
  void resize_by(int delta, int terminal_height);

  /// Mutable storage passed to FTXUI's ResizableSplit.
  [[nodiscard]] auto main_size() -> int &;
  [[nodiscard]] auto preferred_ratio() const -> double;

private:
  [[nodiscard]] auto available_height(int terminal_height) const -> int;
  [[nodiscard]] auto clamp_main_size(int requested, int available) const -> int;
  void remember_user_size(int terminal_height);

  double preferred_ratio_{0.7};
  int minimum_pane_height_{3};
  int main_size_{0};
  int applied_main_size_{0};
  int terminal_height_{-1};
  bool initialized_{false};
};

struct WrappedLogRow {
  uint64_t sequence{0};
  std::size_t segment_index{0};
  std::string text;
  spdlog::level::level_enum level{spdlog::level::info};
};

struct WrappedLogAnchor {
  uint64_t sequence{0};
  std::size_t segment_index{0};

  auto operator==(const WrappedLogAnchor &) const -> bool = default;
};

struct WrappedLogSyncResult {
  int appended_rows{0};
  int removed_rows{0};
  bool rebuilt{false};
};

/// Incremental logical-entry to visual-row index for the retained log.
class WrappedLogCache {
public:
  [[nodiscard]] auto needs_rebuild(int width) const -> bool;
  [[nodiscard]] auto next_sequence() const -> uint64_t;
  [[nodiscard]] auto total_rows() const -> std::size_t;
  [[nodiscard]] auto width() const -> int;

  auto sync(const LogSnapshot &snapshot, int width) -> WrappedLogSyncResult;
  [[nodiscard]] auto rows_range(std::size_t offset, std::size_t count) const
      -> std::vector<WrappedLogRow>;
  [[nodiscard]] auto anchor_at(std::size_t row) const
      -> std::optional<WrappedLogAnchor>;
  [[nodiscard]] auto row_index(const WrappedLogAnchor &anchor) const
      -> std::optional<std::size_t>;

private:
  struct Entry {
    uint64_t sequence{0};
    std::vector<std::string> rows;
    spdlog::level::level_enum level{spdlog::level::info};
  };

  void clear();
  void append(const LogLine &line);

  std::deque<Entry> entries_;
  std::size_t total_rows_{0};
  uint64_t next_sequence_{0};
  int width_{0};
};

struct ScrollbarMetrics {
  int track_height{0};
  int thumb_top{0};
  int thumb_height{0};
};

[[nodiscard]] auto max_scroll_offset(std::size_t total_rows, int visible_rows)
    -> int;
[[nodiscard]] auto calculate_scrollbar(std::size_t total_rows, int visible_rows,
                                       int scroll_offset) -> ScrollbarMetrics;

class LogViewport {
public:
  [[nodiscard]] auto offset() const -> int;
  [[nodiscard]] auto following_tail() const -> bool;

  void scroll_to(int offset, std::size_t total_rows, int visible_rows);
  void scroll_by(int delta, std::size_t total_rows, int visible_rows);
  [[nodiscard]] auto capture_anchor(const WrappedLogCache &cache,
                                    int visible_rows) const
      -> std::optional<WrappedLogAnchor>;
  void apply_sync(const WrappedLogCache &cache,
                  const WrappedLogSyncResult &result, int visible_rows,
                  const std::optional<WrappedLogAnchor> &reflow_anchor);

private:
  int offset_{0};
  bool following_tail_{true};
};

} // namespace obcx::common::tui_layout
