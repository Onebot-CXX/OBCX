#include "tui/tui_layout.hpp"

#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace obcx::common::tui_layout {
namespace {

auto is_wrap_boundary(std::string_view glyph) -> bool {
  return glyph.size() == 1 &&
         std::isspace(static_cast<unsigned char>(glyph.front())) != 0;
}

auto wrap_line(std::string_view line, int width) -> std::vector<std::string> {
  const auto glyphs = source_glyphs(std::string(line));

  if (glyphs.empty()) {
    return {std::string(line)};
  }

  std::vector<std::string> rows;
  std::size_t start = 0;
  while (start < glyphs.size()) {
    int row_width = 0;
    std::size_t end = start;
    std::size_t last_boundary = start;

    while (end < glyphs.size()) {
      const int glyph_width = glyphs[end].width;
      if (end != start && row_width + glyph_width > width) {
        break;
      }

      row_width += glyph_width;
      ++end;
      const auto &glyph = glyphs[end - 1];
      if (is_wrap_boundary(line.substr(glyph.begin, glyph.end - glyph.begin))) {
        last_boundary = end;
      }

      // A glyph wider than the viewport is kept intact on its own row.
      if (end == start + 1 && row_width > width) {
        break;
      }
      if (row_width >= width) {
        break;
      }
    }

    if (end < glyphs.size() && last_boundary > start && last_boundary < end) {
      end = last_boundary;
    }

    // Keep the original contiguous slice, including source whitespace that
    // FTXUI may omit visually (e.g. tabs), rather than rebuilding glyph text.
    const auto first_byte = start == 0 ? 0 : glyphs[start].begin;
    const auto last_byte =
        end == glyphs.size() ? line.size() : glyphs[end].begin;
    rows.emplace_back(line.substr(first_byte, last_byte - first_byte));
    start = end;
  }

  return rows;
}

} // namespace

auto source_glyphs(const std::string &text) -> std::vector<SourceGlyph> {
  std::vector<SourceGlyph> result;
  std::size_t cursor = 0;
  for (const auto &glyph : ftxui::Utf8ToGlyphs(text)) {
    if (glyph.empty()) {
      continue; // FTXUI's second cell of a full-width glyph
    }
    std::size_t begin = std::string::npos;
    // FTXUI can join a combining suffix across omitted controls (e.g. a tab).
    // Project the glyph's ordered bytes back onto the original source rather
    // than assuming its rendered bytes form a contiguous source substring.
    for (const char byte : glyph) {
      const auto found = text.find(byte, cursor);
      if (found == std::string::npos) {
        return {}; // no trustworthy mapping: never fabricate byte positions
      }
      if (begin == std::string::npos) {
        begin = found;
      }
      cursor = found + 1;
    }
    const auto end = cursor;
    const int width = std::max(0, ftxui::string_width(glyph));
    if (width == 0 && !result.empty()) {
      // FTXUI can put a combining suffix in a wide glyph's placeholder cell.
      // Keep that suffix with the base when wrapping, highlighting and copying.
      result.back().end = end;
    } else {
      result.push_back({.begin = begin, .end = end, .width = width});
    }
  }
  return result;
}

auto wrap_text(const std::string &text, int width) -> std::vector<std::string> {
  width = std::max(width, 1);

  std::vector<std::string> rows;
  std::size_t start = 0;
  while (true) {
    const auto newline = text.find('\n', start);
    const auto line = std::string_view(text).substr(
        start,
        newline == std::string::npos ? std::string::npos : newline - start);
    auto wrapped = wrap_line(line, width);
    rows.insert(rows.end(), std::make_move_iterator(wrapped.begin()),
                std::make_move_iterator(wrapped.end()));

    if (newline == std::string::npos) {
      break;
    }
    start = newline + 1;
  }

  return rows;
}

SplitState::SplitState(double preferred_ratio, int minimum_pane_height)
    : preferred_ratio_(std::clamp(preferred_ratio, 0.0, 1.0)),
      minimum_pane_height_(std::max(0, minimum_pane_height)) {}

auto SplitState::available_height(int terminal_height) const -> int {
  return std::max(0, terminal_height - 1);
}

auto SplitState::clamp_main_size(int requested, int available) const -> int {
  if (available <= 0) {
    return 0;
  }

  if (available >= minimum_pane_height_ * 2) {
    return std::clamp(requested, minimum_pane_height_,
                      available - minimum_pane_height_);
  }

  if (available >= 2) {
    return std::clamp(requested, 1, available - 1);
  }
  return std::clamp(requested, 0, available);
}

void SplitState::remember_user_size(int terminal_height) {
  const int available = available_height(terminal_height);
  main_size_ = clamp_main_size(main_size_, available);
  if (available > 0) {
    preferred_ratio_ = std::clamp(static_cast<double>(main_size_) /
                                      static_cast<double>(available),
                                  0.0, 1.0);
  }
  applied_main_size_ = main_size_;
  terminal_height_ = terminal_height;
  initialized_ = true;
}

auto SplitState::snapshot(int terminal_width, int terminal_height)
    -> LayoutSnapshot {
  terminal_width = std::max(0, terminal_width);
  terminal_height = std::max(0, terminal_height);
  const int available = available_height(terminal_height);

  const bool externally_modified =
      initialized_ && main_size_ != applied_main_size_;
  if (externally_modified) {
    remember_user_size(terminal_height);
  } else if (!initialized_ || terminal_height != terminal_height_) {
    const int requested =
        static_cast<int>(std::lround(preferred_ratio_ * available));
    main_size_ = clamp_main_size(requested, available);
    applied_main_size_ = main_size_;
    terminal_height_ = terminal_height;
    initialized_ = true;
  } else {
    main_size_ = clamp_main_size(main_size_, available);
    applied_main_size_ = main_size_;
  }

  const int console_height = std::max(0, available - main_size_);
  return LayoutSnapshot{
      .terminal_width = terminal_width,
      .terminal_height = terminal_height,
      .log_pane_height = main_size_,
      .console_pane_height = console_height,
      .separator_y = main_size_,
      .log_content_width = std::max(0, terminal_width - 3),
      .log_content_height = std::max(0, main_size_ - 2),
      .console_content_width = std::max(0, terminal_width - 3),
      .console_content_height = std::max(0, console_height - 2),
      .log_scrollbar_x = std::max(0, terminal_width - 2),
      .log_content_top = main_size_ > 0 ? 1 : 0,
  };
}

void SplitState::resize_by(int delta, int terminal_height) {
  if (!initialized_ || terminal_height != terminal_height_) {
    static_cast<void>(snapshot(0, terminal_height));
  }
  main_size_ += delta;
  remember_user_size(std::max(0, terminal_height));
}

auto SplitState::main_size() -> int & { return main_size_; }

auto SplitState::preferred_ratio() const -> double { return preferred_ratio_; }

auto WrappedLogCache::needs_rebuild(int width) const -> bool {
  return width_ != std::max(1, width);
}

auto WrappedLogCache::next_sequence() const -> uint64_t {
  return next_sequence_;
}

auto WrappedLogCache::total_rows() const -> std::size_t { return total_rows_; }

auto WrappedLogCache::width() const -> int { return width_; }

void WrappedLogCache::clear() {
  entries_.clear();
  total_rows_ = 0;
  next_sequence_ = 0;
}

auto wrap_source(const std::string &source, int width)
    -> std::vector<WrappedSourceRow> {
  std::vector<WrappedSourceRow> result;
  std::size_t offset = 0;
  for (auto &text : wrap_text(source, width)) {
    const auto begin = offset;
    offset += text.size();
    if (offset < source.size() && source[offset] == '\n') {
      ++offset;
    }
    result.push_back({.text = std::move(text), .source_begin = begin});
  }
  return result;
}

auto WrappedLogCache::source(uint64_t sequence) const -> const std::string * {
  const auto found = std::lower_bound(
      entries_.begin(), entries_.end(), sequence,
      [](const Entry &entry, uint64_t id) { return entry.sequence < id; });
  return found != entries_.end() && found->sequence == sequence ? &found->source
                                                                : nullptr;
}

void WrappedLogCache::append(const LogLine &line) {
  auto rows = wrap_source(line.stripped_text, width_);
  total_rows_ += rows.size();
  entries_.push_back(Entry{
      .sequence = line.sequence,
      .source = line.stripped_text,
      .rows = std::move(rows),
      .level = line.level,
  });
  next_sequence_ = line.sequence + 1;
}

auto WrappedLogCache::sync(const LogSnapshot &snapshot, int width)
    -> WrappedLogSyncResult {
  const int normalized_width = std::max(1, width);
  WrappedLogSyncResult result;

  auto rebuild = [&]() -> void {
    result.removed_rows += static_cast<int>(total_rows_);
    clear();
    width_ = normalized_width;
    next_sequence_ = snapshot.first_sequence;
    result.rebuilt = true;
  };

  if (needs_rebuild(normalized_width)) {
    rebuild();
  } else {
    while (!entries_.empty() &&
           entries_.front().sequence < snapshot.first_sequence) {
      const auto removed = entries_.front().rows.size();
      total_rows_ -= removed;
      result.removed_rows += static_cast<int>(removed);
      entries_.pop_front();
    }

    const bool sequence_reset = next_sequence_ > snapshot.next_sequence;
    const bool missing_retained_range =
        next_sequence_ < snapshot.first_sequence ||
        (!snapshot.lines.empty() &&
         snapshot.lines.front().sequence > next_sequence_);
    if (sequence_reset || missing_retained_range) {
      rebuild();
    }
  }

  for (const auto &line : snapshot.lines) {
    if (!result.rebuilt && line.sequence < next_sequence_) {
      continue;
    }
    if (line.sequence < next_sequence_) {
      continue;
    }
    if (line.sequence != next_sequence_) {
      // A non-contiguous snapshot cannot preserve the existing index.
      rebuild();
    }
    const auto before = total_rows_;
    append(line);
    result.appended_rows += static_cast<int>(total_rows_ - before);
  }

  next_sequence_ = std::max(next_sequence_, snapshot.next_sequence);
  return result;
}

auto WrappedLogCache::rows_range(std::size_t offset, std::size_t count) const
    -> std::vector<WrappedLogRow> {
  std::vector<WrappedLogRow> rows;
  if (offset >= total_rows_ || count == 0) {
    return rows;
  }
  rows.reserve(std::min(count, total_rows_ - offset));

  std::size_t entry_offset = 0;
  for (const auto &entry : entries_) {
    const auto entry_end = entry_offset + entry.rows.size();
    if (entry_end <= offset) {
      entry_offset = entry_end;
      continue;
    }

    const auto segment_start =
        offset > entry_offset ? offset - entry_offset : std::size_t{0};
    for (auto segment = segment_start;
         segment < entry.rows.size() && rows.size() < count; ++segment) {
      rows.push_back(WrappedLogRow{
          .sequence = entry.sequence,
          .segment_index = segment,
          .source_begin = entry.rows[segment].source_begin,
          .text = entry.rows[segment].text,
          .level = entry.level,
      });
    }
    if (rows.size() == count) {
      break;
    }
    entry_offset = entry_end;
  }

  return rows;
}

auto WrappedLogCache::anchor_at(std::size_t row) const
    -> std::optional<WrappedLogAnchor> {
  if (row >= total_rows_) {
    return std::nullopt;
  }

  std::size_t offset = 0;
  for (const auto &entry : entries_) {
    if (row < offset + entry.rows.size()) {
      return WrappedLogAnchor{
          .sequence = entry.sequence,
          .source_offset = entry.rows[row - offset].source_begin,
      };
    }
    offset += entry.rows.size();
  }
  return std::nullopt;
}

auto WrappedLogCache::row_index(const WrappedLogAnchor &anchor) const
    -> std::optional<std::size_t> {
  std::size_t offset = 0;
  for (const auto &entry : entries_) {
    if (entry.sequence == anchor.sequence) {
      if (entry.rows.empty()) {
        return offset;
      }
      auto row = std::upper_bound(
          entry.rows.begin(), entry.rows.end(), anchor.source_offset,
          [](std::size_t position, const WrappedSourceRow &candidate) {
            return position < candidate.source_begin;
          });
      if (row != entry.rows.begin()) {
        --row;
      }
      return offset + static_cast<std::size_t>(row - entry.rows.begin());
    }
    offset += entry.rows.size();
  }
  return std::nullopt;
}

auto max_scroll_offset(std::size_t total_rows, int visible_rows) -> int {
  const auto visible = static_cast<std::size_t>(std::max(0, visible_rows));
  if (total_rows <= visible) {
    return 0;
  }
  return static_cast<int>(total_rows - visible);
}

auto calculate_scrollbar(std::size_t total_rows, int visible_rows,
                         int scroll_offset) -> ScrollbarMetrics {
  const int track_height = std::max(0, visible_rows);
  if (track_height == 0) {
    return {};
  }

  const int max_offset = max_scroll_offset(total_rows, track_height);
  if (max_offset == 0) {
    return ScrollbarMetrics{
        .track_height = track_height,
        .thumb_top = 0,
        .thumb_height = track_height,
    };
  }

  const auto squared_track =
      static_cast<int64_t>(track_height) * static_cast<int64_t>(track_height);
  const int thumb_height = std::clamp(
      static_cast<int>(squared_track / static_cast<int64_t>(total_rows)), 1,
      track_height);
  const int movable = track_height - thumb_height;
  scroll_offset = std::clamp(scroll_offset, 0, max_offset);
  const double progress_from_top =
      1.0 - static_cast<double>(scroll_offset) / max_offset;
  const int thumb_top = std::clamp(
      static_cast<int>(std::lround(progress_from_top * movable)), 0, movable);
  return ScrollbarMetrics{
      .track_height = track_height,
      .thumb_top = thumb_top,
      .thumb_height = thumb_height,
  };
}

auto LogViewport::offset() const -> int { return offset_; }

auto LogViewport::following_tail() const -> bool { return following_tail_; }

void LogViewport::pause_following() { following_tail_ = false; }

void LogViewport::scroll_to(int offset, std::size_t total_rows,
                            int visible_rows) {
  offset_ = std::clamp(offset, 0, max_scroll_offset(total_rows, visible_rows));
  following_tail_ = offset_ == 0;
}

void LogViewport::scroll_by(int delta, std::size_t total_rows,
                            int visible_rows) {
  scroll_to(offset_ + delta, total_rows, visible_rows);
}

auto LogViewport::capture_anchor(const WrappedLogCache &cache,
                                 int visible_rows) const
    -> std::optional<WrappedLogAnchor> {
  if (cache.total_rows() == 0) {
    return std::nullopt;
  }

  const int total = static_cast<int>(cache.total_rows());
  const int visible = std::max(1, visible_rows);
  const int end = std::max(0, total - offset_);
  const auto start = static_cast<std::size_t>(std::max(0, end - visible));
  return cache.anchor_at(std::min(start, cache.total_rows() - 1));
}

void LogViewport::apply_sync(
    const WrappedLogCache &cache, const WrappedLogSyncResult &result,
    int visible_rows, const std::optional<WrappedLogAnchor> &reflow_anchor) {
  if (following_tail_) {
    offset_ = 0;
  } else if (reflow_anchor.has_value()) {
    if (const auto row = cache.row_index(*reflow_anchor); row.has_value()) {
      offset_ = static_cast<int>(cache.total_rows()) -
                std::max(1, visible_rows) - static_cast<int>(*row);
    } else if (!result.rebuilt) {
      offset_ += result.appended_rows;
    }
  } else if (!result.rebuilt) {
    offset_ += result.appended_rows;
  }

  offset_ = std::clamp(offset_, 0,
                       max_scroll_offset(cache.total_rows(), visible_rows));
  // Only explicit navigation (scroll_to) resumes tail-follow. A pinned view
  // must stay pinned even when it temporarily coincides with the tail.
}

} // namespace obcx::common::tui_layout
