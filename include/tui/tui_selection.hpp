#pragma once

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace obcx::common::tui_selection {

// A resource bound, not an application configuration default.
inline constexpr std::size_t clipboard_byte_limit = 64 * 1024;

enum class Pane { Log, Console };
struct Position {
  uint64_t entry;
  std::size_t byte;
  auto operator<=>(const Position &) const = default;
};
struct GlyphSpan {
  Position begin;
  Position end;
};

enum class CopyResult {
  Ready,
  Empty,
  Unavailable,
  TooLarge,
  InvalidText,
  NotInteractive,
  WriteFailed,
  Emitted,
};
struct CopyText {
  CopyResult result;
  std::string text;
};

// UI-thread-owned mapping only; source text remains in the existing histories.
// FTXUI reflect boxes and glyph widths provide hit testing and DOM decorators
// provide highlighting. Coordinate selection is never clipboard authority.
class Selection {
public:
  using SourceReader = std::function<const std::string *(uint64_t)>;

  void begin_frame(Pane pane);
  auto render_row(Pane pane, uint64_t entry, std::size_t source_begin,
                  const std::string &text) -> ftxui::Element;
  [[nodiscard]] auto hit(Pane pane, int x, int y, int top, int bottom,
                         bool clamp, const SourceReader &source) const
      -> std::optional<GlyphSpan>;
  [[nodiscard]] auto first_visible(Pane pane, int top, int bottom) const
      -> std::optional<Position>;
  [[nodiscard]] auto row_index(Pane pane, Position point) const
      -> std::optional<int>;

  void start(Pane pane, GlyphSpan span);
  void update(GlyphSpan span);
  void release();
  void clear();
  [[nodiscard]] auto active() const -> bool;
  [[nodiscard]] auto in(Pane pane) const -> bool;
  [[nodiscard]] auto dragging() const -> bool;
  [[nodiscard]] auto pane() const -> Pane;
  [[nodiscard]] auto range() const -> std::pair<Position, Position>;
  [[nodiscard]] auto valid(const SourceReader &source) const -> bool;
  [[nodiscard]] auto extract(const SourceReader &source) const -> CopyText;

private:
  struct Row {
    Position start;
    std::size_t source_end;
    ftxui::Box box;
  };
  auto rows(Pane pane) -> std::deque<Row> &;
  [[nodiscard]] auto rows(Pane pane) const -> const std::deque<Row> &;
  std::deque<Row> log_rows_;
  std::deque<Row> console_rows_;
  struct State {
    Pane pane;
    GlyphSpan anchor;
    GlyphSpan tip;
    bool dragging;
  };
  std::optional<State> state_;
};

// Call only from the owning screen's event loop, between render frames.
// An emitted request is not an acknowledgment from the terminal clipboard.
[[nodiscard]] auto emit_clipboard_request(ftxui::ScreenInteractive &owner,
                                          std::string_view text, bool tmux)
    -> CopyResult;
[[nodiscard]] auto feedback(CopyResult result) -> const char *;

} // namespace obcx::common::tui_selection
