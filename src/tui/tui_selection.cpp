#include "tui/tui_selection.hpp"
#include "tui/tui_layout.hpp"

#include <ftxui/component/screen_interactive.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <unistd.h>

namespace obcx::common::tui_selection {
namespace {

// Reject invalid/overlong UTF-8 and terminal controls before emitting anything.
// Tabs and hard newlines are plain source whitespace, not terminal sequences.
auto plain_utf8(std::string_view text) -> bool {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i++]);
    uint32_t codepoint = lead;
    unsigned remaining = 0;
    uint32_t minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      remaining = 1;
      codepoint = lead & 0x1f;
      minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      remaining = 2;
      codepoint = lead & 0x0f;
      minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      remaining = 3;
      codepoint = lead & 0x07;
      minimum = 0x10000;
    } else if (lead >= 0x80) {
      return false;
    }
    if (remaining > text.size() - i) {
      return false;
    }
    while (remaining > 0) {
      --remaining;
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) {
        return false;
      }
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if (codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        (codepoint < 0x20 && codepoint != '\n' && codepoint != '\t') ||
        (codepoint >= 0x7f && codepoint <= 0x9f)) {
      return false;
    }
  }
  return true;
}

auto base64(std::string_view text) -> std::string {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve((text.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < text.size(); i += 3) {
    const auto a = static_cast<unsigned char>(text[i]);
    const auto b =
        i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
    const auto c =
        i + 2 < text.size() ? static_cast<unsigned char>(text[i + 2]) : 0;
    result += alphabet[a >> 2];
    result += alphabet[((a & 3) << 4) | (b >> 4)];
    result += i + 1 < text.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
    result += i + 2 < text.size() ? alphabet[c & 63] : '=';
  }
  return result;
}

} // namespace

auto Selection::rows(Pane pane) -> std::deque<Row> & {
  return pane == Pane::Log ? log_rows_ : console_rows_;
}
auto Selection::rows(Pane pane) const -> const std::deque<Row> & {
  return pane == Pane::Log ? log_rows_ : console_rows_;
}
void Selection::begin_frame(Pane pane) { rows(pane).clear(); }

auto Selection::render_row(Pane pane, uint64_t entry, std::size_t source_begin,
                           const std::string &text) -> ftxui::Element {
  auto &row =
      rows(pane).emplace_back(Row{.start = {entry, source_begin},
                                  .source_end = source_begin + text.size(),
                                  .box = {}});
  auto element = ftxui::text(text);
  if (in(pane)) {
    const auto [begin, end] = range();
    if (entry >= begin.entry && entry <= end.entry) {
      const auto first = entry == begin.entry
                             ? std::clamp(begin.byte, source_begin,
                                          source_begin + text.size()) -
                                   source_begin
                             : 0;
      const auto last =
          entry == end.entry
              ? std::clamp(end.byte, source_begin, source_begin + text.size()) -
                    source_begin
              : text.size();
      if (last > first) {
        element = ftxui::hbox({
            ftxui::text(text.substr(0, first)),
            ftxui::text(text.substr(first, last - first)) |
                ftxui::bgcolor(ftxui::Color::Blue) |
                ftxui::color(ftxui::Color::White),
            ftxui::text(text.substr(last)),
        });
      } else if (text.empty() && Position{entry, source_begin} >= begin &&
                 Position{entry, source_begin} < end) {
        element = ftxui::text(" ") | ftxui::bgcolor(ftxui::Color::Blue);
      }
    }
  }
  return element | ftxui::reflect(row.box);
}

auto Selection::hit(Pane pane, int x, int y, int top, int bottom, bool clamp,
                    const SourceReader &source) const
    -> std::optional<GlyphSpan> {
  if (top > bottom) {
    return std::nullopt;
  }
  const Row *found = nullptr;
  int distance = std::numeric_limits<int>::max();
  for (const auto &row : rows(pane)) {
    if (row.box.y_min < top || row.box.y_min > bottom) {
      continue;
    }
    const int current = std::abs(y - row.box.y_min);
    if (current < distance) {
      distance = current;
      found = &row;
    }
  }
  if (!found || (!clamp && distance != 0)) {
    return std::nullopt;
  }
  const auto *value = source(found->start.entry);
  if (!value || found->source_end > value->size() ||
      found->start.byte > found->source_end) {
    return std::nullopt;
  }
  // Keep only source positions/boxes between frames, not a second text history.
  const auto text =
      value->substr(found->start.byte, found->source_end - found->start.byte);
  const int cell = std::max(0, x - found->box.x_min);
  int column = 0;
  for (const auto &glyph : tui_layout::source_glyphs(text)) {
    if (cell < column + glyph.width) {
      return GlyphSpan{{found->start.entry, found->start.byte + glyph.begin},
                       {found->start.entry, found->start.byte + glyph.end}};
    }
    column += glyph.width;
  }
  // Clicking to the right of the text is a source caret, not pane padding.
  const Position end{found->start.entry, found->source_end};
  return GlyphSpan{end, end};
}

auto Selection::first_visible(Pane pane, int top, int bottom) const
    -> std::optional<Position> {
  for (const auto &row : rows(pane)) {
    if (row.box.y_min >= top && row.box.y_min <= bottom) {
      return row.start;
    }
  }
  return std::nullopt;
}

auto Selection::row_index(Pane pane, Position point) const
    -> std::optional<int> {
  std::optional<int> result;
  int index = 0;
  for (const auto &row : rows(pane)) {
    if (row.start.entry == point.entry && row.start.byte <= point.byte) {
      result = index;
    }
    ++index;
  }
  return result;
}

void Selection::start(Pane pane, GlyphSpan span) {
  state_ = State{pane, span, span, true};
}
void Selection::update(GlyphSpan span) {
  if (state_) {
    state_->tip = span;
  }
}
void Selection::release() {
  if (state_) {
    state_->dragging = false;
  }
}
void Selection::clear() { state_.reset(); }
auto Selection::active() const -> bool { return state_.has_value(); }
auto Selection::in(Pane pane) const -> bool {
  return state_ && state_->pane == pane;
}
auto Selection::dragging() const -> bool { return state_ && state_->dragging; }
auto Selection::pane() const -> Pane { return state_->pane; }
auto Selection::range() const -> std::pair<Position, Position> {
  return {std::min(state_->anchor.begin, state_->tip.begin),
          std::max(state_->anchor.end, state_->tip.end)};
}
auto Selection::valid(const SourceReader &source) const -> bool {
  if (!state_) {
    return false;
  }
  const auto [begin, end] = range();
  const auto *first = source(begin.entry);
  const auto *last = source(end.entry);
  return first && last && begin.byte <= first->size() &&
         end.byte <= last->size();
}
auto Selection::extract(const SourceReader &source) const -> CopyText {
  if (!valid(source)) {
    return {CopyResult::Unavailable, {}};
  }
  const auto [begin, end] = range();
  std::string text;
  for (auto entry = begin.entry;; ++entry) {
    const auto *value = source(entry);
    if (!value) {
      return {CopyResult::Unavailable, {}};
    }
    const auto first = entry == begin.entry ? begin.byte : 0;
    const auto last = entry == end.entry ? end.byte : value->size();
    const auto count = last - first;
    if (count > clipboard_byte_limit - text.size()) {
      return {CopyResult::TooLarge, {}};
    }
    text.append(*value, first, count);
    if (entry == end.entry) {
      break;
    }
    if (text.size() == clipboard_byte_limit) {
      return {CopyResult::TooLarge, {}};
    }
    text += '\n'; // logical entry boundary, never a soft-wrap separator
  }
  return {text.empty() ? CopyResult::Empty : CopyResult::Ready,
          std::move(text)};
}

auto emit_clipboard_request(ftxui::ScreenInteractive &owner,
                            std::string_view text, bool tmux) -> CopyResult {
  if (text.empty()) {
    return CopyResult::Empty;
  }
  if (text.size() > clipboard_byte_limit) {
    return CopyResult::TooLarge;
  }
  if (!plain_utf8(text)) {
    return CopyResult::InvalidText;
  }
  if (ftxui::ScreenInteractive::Active() != &owner ||
      ::isatty(STDIN_FILENO) != 1 || ::isatty(STDOUT_FILENO) != 1) {
    return CopyResult::NotInteractive;
  }
  const auto osc = std::string("\033]52;c;") + base64(text) + '\a';
  std::string request;
  if (tmux) {
    request = "\033Ptmux;";
    for (const char ch : osc) {
      if (ch == '\033') {
        request += '\033';
      }
      request += ch;
    }
    request += "\033\\";
  } else {
    request = osc;
  }
  // This call is made by the root event handler, not a renderer/background
  // callback. FTXUI frame writes and this complete request share stdout and
  // the same loop thread. Never restore terminal hooks to copy.
  try {
    std::cout.write(request.data(),
                    static_cast<std::streamsize>(request.size()));
    std::cout.flush();
    return std::cout.good() ? CopyResult::Emitted : CopyResult::WriteFailed;
  } catch (const std::ios_base::failure &) {
    return CopyResult::WriteFailed;
  }
}

auto feedback(CopyResult result) -> const char * {
  switch (result) {
  case CopyResult::Emitted:
    return "Copy request emitted; client clipboard not verified (Esc cancels).";
  case CopyResult::TooLarge:
    return "Copy rejected: selection exceeds 64 KiB; select less text.";
  case CopyResult::Unavailable:
    return "Selection expired: source no longer retained; select again.";
  case CopyResult::InvalidText:
    return "Copy rejected: source contains invalid UTF-8 or terminal controls.";
  case CopyResult::NotInteractive:
    return "Copy unavailable: no interactive TUI terminal output.";
  case CopyResult::WriteFailed:
    return "Copy output failed; client clipboard state is unknown.";
  case CopyResult::Empty:
    return "Selection contains no text; drag across output to select.";
  case CopyResult::Ready:
    return "Selection ready; Ctrl+C copies, Esc cancels.";
  }
  return "Copy unavailable.";
}

} // namespace obcx::common::tui_selection
