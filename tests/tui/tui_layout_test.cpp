#include "tui/tui_layout.hpp"
#include "tui/tui_sink.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/direction.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>
#include <spdlog/logger.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace obcx::common::tui_layout {
namespace {

TEST(TuiWrapTextTest, WrapsAsciiAtWhitespaceWithoutDroppingIt) {
  EXPECT_EQ(wrap_text("alpha beta", 6),
            (std::vector<std::string>{"alpha ", "beta"}));
}

TEST(TuiWrapTextTest, HardWrapsLongUnbrokenText) {
  EXPECT_EQ(wrap_text("abcdefgh", 3),
            (std::vector<std::string>{"abc", "def", "gh"}));
}

TEST(TuiWrapTextTest, PreservesWhitespaceAndExplicitNewlines) {
  EXPECT_EQ(wrap_text("ab cd\n ef\n", 3),
            (std::vector<std::string>{"ab ", "cd", " ef", ""}));
}

TEST(TuiWrapTextTest, KeepsCombiningGlyphsTogether) {
  EXPECT_EQ(wrap_text("āb", 1), (std::vector<std::string>{"ā", "b"}));
}

TEST(TuiWrapTextTest, UsesTerminalCellsForDoubleWidthGlyphs) {
  EXPECT_EQ(wrap_text("测试a", 2), (std::vector<std::string>{"测", "试", "a"}));
}

TEST(TuiWrapTextTest, KeepsGlyphWiderThanViewportOnItsOwnRow) {
  EXPECT_EQ(wrap_text("测", 1), (std::vector<std::string>{"测"}));
}

TEST(TuiSplitStateTest, UsesSeventyThirtyOnFirstFrame) {
  SplitState split;

  const auto layout = split.snapshot(80, 21);

  EXPECT_EQ(layout.log_pane_height, 14);
  EXPECT_EQ(layout.console_pane_height, 6);
  EXPECT_EQ(layout.separator_y, 14);
  EXPECT_EQ(layout.log_content_width, 77);
  EXPECT_EQ(layout.log_content_height, 12);
  EXPECT_EQ(layout.console_content_width, 77);
  EXPECT_EQ(layout.console_content_height, 4);
}

TEST(TuiSplitStateTest, PreservesInitialRatioWhenTerminalHeightChanges) {
  SplitState split;
  static_cast<void>(split.snapshot(80, 21));

  const auto layout = split.snapshot(80, 31);

  EXPECT_EQ(layout.log_pane_height, 21);
  EXPECT_EQ(layout.console_pane_height, 9);
}

TEST(TuiSplitStateTest, PreservesMouseSelectedRatioAcrossResize) {
  SplitState split;
  static_cast<void>(split.snapshot(80, 21));
  split.main_size() = 10;
  static_cast<void>(split.snapshot(80, 21));

  const auto layout = split.snapshot(80, 41);

  EXPECT_DOUBLE_EQ(split.preferred_ratio(), 0.5);
  EXPECT_EQ(layout.log_pane_height, 20);
  EXPECT_EQ(layout.console_pane_height, 20);
}

TEST(TuiSplitStateTest, ClampsBothNormalPanesToUsableMinimums) {
  SplitState split;
  static_cast<void>(split.snapshot(80, 11));

  split.main_size() = 0;
  auto layout = split.snapshot(80, 11);
  EXPECT_EQ(layout.log_pane_height, 3);
  EXPECT_EQ(layout.console_pane_height, 7);

  split.main_size() = 99;
  layout = split.snapshot(80, 11);
  EXPECT_EQ(layout.log_pane_height, 7);
  EXPECT_EQ(layout.console_pane_height, 3);
}

TEST(TuiSplitStateTest, ProducesInBoundsGeometryForTinyTerminals) {
  SplitState split;

  auto layout = split.snapshot(2, 4);
  EXPECT_GE(layout.log_pane_height, 0);
  EXPECT_GE(layout.console_pane_height, 0);
  EXPECT_EQ(layout.log_pane_height + layout.console_pane_height, 3);
  EXPECT_EQ(layout.log_content_width, 0);
  EXPECT_EQ(layout.console_content_width, 0);

  layout = split.snapshot(0, 0);
  EXPECT_EQ(layout.log_pane_height, 0);
  EXPECT_EQ(layout.console_pane_height, 0);
}

TEST(TuiSplitStateTest, KeyboardStepsRespectMinimumsAndUpdateRatio) {
  SplitState split;
  static_cast<void>(split.snapshot(80, 21));

  split.resize_by(-1, 21);
  auto layout = split.snapshot(80, 21);
  EXPECT_EQ(layout.log_pane_height, 13);
  EXPECT_DOUBLE_EQ(split.preferred_ratio(), 0.65);

  for (int index = 0; index < 30; ++index) {
    split.resize_by(-1, 21);
  }
  layout = split.snapshot(80, 21);
  EXPECT_EQ(layout.log_pane_height, 3);

  for (int index = 0; index < 30; ++index) {
    split.resize_by(1, 21);
  }
  layout = split.snapshot(80, 21);
  EXPECT_EQ(layout.log_pane_height, 17);
}

TEST(TuiSinkSnapshotTest, AssignsMonotonicSequencesAndReportsEviction) {
  auto sink = std::make_shared<tui_sink_mt>(2);
  sink->set_pattern("%v");
  spdlog::logger logger("tui-snapshot", sink);

  logger.info("one");
  logger.info("two");
  logger.info("three");

  const auto snapshot = sink->snapshot_from(0);
  ASSERT_EQ(snapshot.lines.size(), 2U);
  EXPECT_EQ(snapshot.version, 3U);
  EXPECT_EQ(snapshot.first_sequence, 1U);
  EXPECT_EQ(snapshot.next_sequence, 3U);
  EXPECT_EQ(snapshot.lines[0].sequence, 1U);
  EXPECT_EQ(snapshot.lines[0].stripped_text, "two");
  EXPECT_EQ(snapshot.lines[1].sequence, 2U);
  EXPECT_EQ(snapshot.lines[1].stripped_text, "three");
}

TEST(TuiSinkSnapshotTest, ReturnsAConsistentRangeFromRequestedSequence) {
  auto sink = std::make_shared<tui_sink_mt>(3);
  sink->set_pattern("%v");
  spdlog::logger logger("tui-snapshot-range", sink);
  logger.info("zero");
  logger.info("one");
  logger.info("two");

  auto snapshot = sink->snapshot_from(2);
  ASSERT_EQ(snapshot.lines.size(), 1U);
  EXPECT_EQ(snapshot.first_sequence, 0U);
  EXPECT_EQ(snapshot.next_sequence, 3U);
  EXPECT_EQ(snapshot.lines.front().sequence, 2U);
  EXPECT_EQ(snapshot.lines.front().stripped_text, "two");

  logger.info("three");
  snapshot = sink->snapshot_from(3);
  ASSERT_EQ(snapshot.lines.size(), 1U);
  EXPECT_EQ(snapshot.first_sequence, 1U);
  EXPECT_EQ(snapshot.next_sequence, 4U);
  EXPECT_EQ(snapshot.lines.front().sequence, 3U);
  EXPECT_EQ(snapshot.lines.front().stripped_text, "three");
}

auto test_log_line(uint64_t sequence, std::string text) -> LogLine {
  return LogLine{
      .sequence = sequence,
      .text = text,
      .stripped_text = std::move(text),
      .level = spdlog::level::info,
  };
}

TEST(WrappedLogCacheTest, InitializesAndExtractsVisualRows) {
  WrappedLogCache cache;
  const LogSnapshot snapshot{
      .version = 2,
      .first_sequence = 0,
      .next_sequence = 2,
      .lines = {test_log_line(0, "abcde"), test_log_line(1, "x")},
  };

  const auto result = cache.sync(snapshot, 3);

  EXPECT_TRUE(result.rebuilt);
  EXPECT_EQ(result.appended_rows, 3);
  EXPECT_EQ(cache.total_rows(), 3U);
  EXPECT_EQ(cache.next_sequence(), 2U);
  const auto rows = cache.rows_range(1, 2);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].sequence, 0U);
  EXPECT_EQ(rows[0].segment_index, 1U);
  EXPECT_EQ(rows[0].text, "de");
  EXPECT_EQ(rows[1].sequence, 1U);
  EXPECT_EQ(rows[1].text, "x");
}

TEST(WrappedLogCacheTest, StableWidthWrapsOnlyAppendedEntries) {
  WrappedLogCache cache;
  static_cast<void>(
      cache.sync(LogSnapshot{.version = 1,
                             .first_sequence = 0,
                             .next_sequence = 1,
                             .lines = {test_log_line(0, "abcde")}},
                 3));

  const auto result =
      cache.sync(LogSnapshot{.version = 2,
                             .first_sequence = 0,
                             .next_sequence = 2,
                             .lines = {test_log_line(1, "yyyy")}},
                 3);

  EXPECT_FALSE(result.rebuilt);
  EXPECT_EQ(result.appended_rows, 2);
  EXPECT_EQ(result.removed_rows, 0);
  EXPECT_EQ(cache.total_rows(), 4U);
  const auto first_rows = cache.rows_range(0, 2);
  ASSERT_EQ(first_rows.size(), 2U);
  EXPECT_EQ(first_rows[0].text, "abc");
  EXPECT_EQ(first_rows[1].text, "de");
}

TEST(WrappedLogCacheTest, RemovesEvictedEntriesFromTheFront) {
  WrappedLogCache cache;
  static_cast<void>(cache.sync(
      LogSnapshot{.version = 2,
                  .first_sequence = 0,
                  .next_sequence = 2,
                  .lines = {test_log_line(0, "abcde"), test_log_line(1, "x")}},
      3));

  const auto result = cache.sync(
      LogSnapshot{
          .version = 2, .first_sequence = 1, .next_sequence = 2, .lines = {}},
      3);

  EXPECT_FALSE(result.rebuilt);
  EXPECT_EQ(result.removed_rows, 2);
  EXPECT_EQ(cache.total_rows(), 1U);
  EXPECT_EQ(cache.rows_range(0, 1).front().sequence, 1U);
}

TEST(WrappedLogCacheTest, RebuildsAllRetainedEntriesWhenWidthChanges) {
  WrappedLogCache cache;
  static_cast<void>(cache.sync(
      LogSnapshot{.version = 2,
                  .first_sequence = 0,
                  .next_sequence = 2,
                  .lines = {test_log_line(0, "abcde"), test_log_line(1, "xy")}},
      3));

  const auto result = cache.sync(
      LogSnapshot{.version = 2,
                  .first_sequence = 0,
                  .next_sequence = 2,
                  .lines = {test_log_line(0, "abcde"), test_log_line(1, "xy")}},
      2);

  EXPECT_TRUE(result.rebuilt);
  EXPECT_EQ(cache.width(), 2);
  EXPECT_EQ(cache.total_rows(), 4U);
  const auto rows = cache.rows_range(0, 4);
  ASSERT_EQ(rows.size(), 4U);
  EXPECT_EQ(rows[0].text, "ab");
  EXPECT_EQ(rows[1].text, "cd");
  EXPECT_EQ(rows[2].text, "e");
  EXPECT_EQ(rows[3].text, "xy");
}

TEST(WrappedLogCacheTest, LocatesRowsByStableLogicalAnchor) {
  WrappedLogCache cache;
  static_cast<void>(cache.sync(LogSnapshot{.version = 2,
                                           .first_sequence = 4,
                                           .next_sequence = 6,
                                           .lines = {test_log_line(4, "abcd"),
                                                     test_log_line(5, "efgh")}},
                               2));

  const auto anchor = cache.anchor_at(2);
  ASSERT_TRUE(anchor.has_value());
  EXPECT_EQ(anchor->sequence, 5U);
  EXPECT_EQ(anchor->segment_index, 0U);
  EXPECT_EQ(
      cache.row_index(WrappedLogAnchor{.sequence = 5, .segment_index = 1}),
      std::optional<std::size_t>{3});
  EXPECT_FALSE(cache.anchor_at(4).has_value());
}

TEST(LogViewportTest, FollowsNewWrappedRowsAtTail) {
  WrappedLogCache cache;
  LogViewport viewport;
  auto result = cache.sync(LogSnapshot{.version = 1,
                                       .first_sequence = 0,
                                       .next_sequence = 1,
                                       .lines = {test_log_line(0, "abcd")}},
                           2);
  viewport.apply_sync(cache, result, 2, std::nullopt);
  EXPECT_EQ(viewport.offset(), 0);
  EXPECT_TRUE(viewport.following_tail());

  result = cache.sync(LogSnapshot{.version = 2,
                                  .first_sequence = 0,
                                  .next_sequence = 2,
                                  .lines = {test_log_line(1, "efgh")}},
                      2);
  viewport.apply_sync(cache, result, 2, std::nullopt);
  EXPECT_EQ(viewport.offset(), 0);
  EXPECT_TRUE(viewport.following_tail());
}

TEST(LogViewportTest, AppendedRowsDoNotMoveInspectedContent) {
  WrappedLogCache cache;
  LogViewport viewport;
  auto result = cache.sync(
      LogSnapshot{.version = 3,
                  .first_sequence = 0,
                  .next_sequence = 3,
                  .lines = {test_log_line(0, "aa"), test_log_line(1, "bb"),
                            test_log_line(2, "cc")}},
      2);
  viewport.apply_sync(cache, result, 2, std::nullopt);
  viewport.scroll_to(1, cache.total_rows(), 2);
  const auto anchor = viewport.capture_anchor(cache, 2);

  result = cache.sync(LogSnapshot{.version = 4,
                                  .first_sequence = 0,
                                  .next_sequence = 4,
                                  .lines = {test_log_line(3, "dddd")}},
                      2);
  viewport.apply_sync(cache, result, 2, std::nullopt);

  EXPECT_EQ(viewport.offset(), 3);
  EXPECT_EQ(viewport.capture_anchor(cache, 2), anchor);
  EXPECT_FALSE(viewport.following_tail());
}

TEST(LogViewportTest, ScrollOperationsClampToVisualRowBounds) {
  LogViewport viewport;

  viewport.scroll_by(100, 8, 3);
  EXPECT_EQ(viewport.offset(), 5);
  viewport.scroll_by(-2, 8, 3);
  EXPECT_EQ(viewport.offset(), 3);
  viewport.scroll_to(-10, 8, 3);
  EXPECT_EQ(viewport.offset(), 0);
  EXPECT_TRUE(viewport.following_tail());
}

TEST(LogViewportTest, RestoresLogicalAnchorAfterWidthReflow) {
  WrappedLogCache cache;
  LogViewport viewport;
  auto result = cache.sync(LogSnapshot{.version = 2,
                                       .first_sequence = 0,
                                       .next_sequence = 2,
                                       .lines = {test_log_line(0, "abcdefgh"),
                                                 test_log_line(1, "ijkl")}},
                           4);
  viewport.apply_sync(cache, result, 2, std::nullopt);
  viewport.scroll_to(1, cache.total_rows(), 2);
  const auto anchor = viewport.capture_anchor(cache, 2);
  ASSERT_TRUE(anchor.has_value());

  result = cache.sync(LogSnapshot{.version = 2,
                                  .first_sequence = 0,
                                  .next_sequence = 2,
                                  .lines = {test_log_line(0, "abcdefgh"),
                                            test_log_line(1, "ijkl")}},
                      2);
  viewport.apply_sync(cache, result, 2, anchor);

  const auto restored = viewport.capture_anchor(cache, 2);
  EXPECT_EQ(restored, anchor);
  EXPECT_EQ(viewport.offset(), 4);
}

TEST(LogViewportTest, ClampsWhenReflowAnchorWasEvicted) {
  WrappedLogCache cache;
  LogViewport viewport;
  auto result = cache.sync(LogSnapshot{.version = 2,
                                       .first_sequence = 0,
                                       .next_sequence = 2,
                                       .lines = {test_log_line(0, "aaaa"),
                                                 test_log_line(1, "bbbb")}},
                           2);
  viewport.apply_sync(cache, result, 1, std::nullopt);
  viewport.scroll_to(3, cache.total_rows(), 1);
  const auto anchor = viewport.capture_anchor(cache, 1);
  ASSERT_TRUE(anchor.has_value());
  EXPECT_EQ(anchor->sequence, 0U);

  result = cache.sync(LogSnapshot{.version = 2,
                                  .first_sequence = 1,
                                  .next_sequence = 2,
                                  .lines = {test_log_line(1, "bbbb")}},
                      1);
  viewport.apply_sync(cache, result, 1, anchor);

  EXPECT_EQ(viewport.offset(), 3);
  EXPECT_EQ(viewport.capture_anchor(cache, 1)->sequence, 1U);
}

TEST(TuiScrollbarTest, RepresentsWrappedVisualRowsFromTopToBottom) {
  auto metrics = calculate_scrollbar(9, 3, 0);
  EXPECT_EQ(metrics.track_height, 3);
  EXPECT_EQ(metrics.thumb_height, 1);
  EXPECT_EQ(metrics.thumb_top, 2);

  metrics = calculate_scrollbar(9, 3, 6);
  EXPECT_EQ(metrics.thumb_top, 0);

  metrics = calculate_scrollbar(2, 3, 0);
  EXPECT_EQ(metrics.thumb_height, 3);
  EXPECT_EQ(metrics.thumb_top, 0);
}

auto left_mouse(ftxui::Mouse::Motion motion, int x, int y) -> ftxui::Event {
  ftxui::Mouse mouse;
  mouse.button = ftxui::Mouse::Left;
  mouse.motion = motion;
  mouse.shift = false;
  mouse.meta = false;
  mouse.control = false;
  mouse.x = x;
  mouse.y = y;
  return ftxui::Event::Mouse("", mouse);
}

TEST(TuiFixedScreenTest, WrappedRowsRenderWithoutClipping) {
  const auto rows = wrap_text("abcdefgh", 4);
  ftxui::Elements elements;
  for (const auto &row : rows) {
    elements.push_back(ftxui::text(row));
  }

  auto screen = ftxui::Screen(4, 2);
  ftxui::Render(screen, ftxui::vbox(std::move(elements)));

  EXPECT_EQ(screen.ToString(), "abcd\r\nefgh");
}

TEST(TuiFixedScreenTest, WidthChangeReflowsEveryVisibleGlyph) {
  WrappedLogCache cache;
  const LogSnapshot snapshot{
      .version = 1,
      .first_sequence = 0,
      .next_sequence = 1,
      .lines = {test_log_line(0, "abcdefgh")},
  };
  static_cast<void>(cache.sync(snapshot, 4));
  EXPECT_EQ(cache.rows_range(0, 2)[1].text, "efgh");

  static_cast<void>(cache.sync(snapshot, 2));
  const auto rows = cache.rows_range(0, 4);
  ftxui::Elements elements;
  for (const auto &row : rows) {
    elements.push_back(ftxui::text(row.text));
  }
  auto screen = ftxui::Screen(2, 4);
  ftxui::Render(screen, ftxui::vbox(std::move(elements)));

  EXPECT_EQ(screen.ToString(), "ab\r\ncd\r\nef\r\ngh");
}

TEST(TuiFixedScreenTest, DividerSupportsMouseKeyboardAndTerminalResize) {
  SplitState split;
  static_cast<void>(split.snapshot(20, 10));
  auto log = ftxui::Renderer([] { return ftxui::text("log"); });
  auto console = ftxui::Renderer([] { return ftxui::text("cli"); });
  auto divider = ftxui::ResizableSplit({
      .main = log,
      .back = console,
      .direction = ftxui::Direction::Up,
      .main_size = &split.main_size(),
      .separator_func = [] { return ftxui::separatorHeavy(); },
  });
  auto component =
      ftxui::CatchEvent(divider, [&](const ftxui::Event &event) -> bool {
        if (event == ftxui::Event::ArrowUpCtrl) {
          split.resize_by(-1, 10);
          return true;
        }
        if (event == ftxui::Event::ArrowDownCtrl) {
          split.resize_by(1, 10);
          return true;
        }
        return false;
      });

  auto screen = ftxui::Screen(20, 10);
  ftxui::Render(screen, component->Render());
  EXPECT_NE(screen.ToString().find("━━━━━━━━━━━━━━━━━━━━"), std::string::npos);

  ASSERT_TRUE(component->OnEvent(
      left_mouse(ftxui::Mouse::Pressed, 1, split.main_size())));
  ASSERT_TRUE(component->OnEvent(left_mouse(ftxui::Mouse::Moved, 1, 4)));
  ASSERT_TRUE(component->OnEvent(left_mouse(ftxui::Mouse::Released, 1, 4)));
  auto layout = split.snapshot(20, 10);
  EXPECT_EQ(layout.log_pane_height, 4);
  EXPECT_EQ(layout.console_pane_height, 5);

  ASSERT_TRUE(component->OnEvent(ftxui::Event::ArrowUpCtrl));
  layout = split.snapshot(20, 10);
  EXPECT_EQ(layout.log_pane_height, 3);

  split.main_size() = 0;
  layout = split.snapshot(20, 10);
  EXPECT_EQ(layout.log_pane_height, 3);

  split.main_size() = 4;
  static_cast<void>(split.snapshot(20, 10));
  layout = split.snapshot(20, 19);
  EXPECT_EQ(layout.log_pane_height, 8);
  EXPECT_EQ(layout.console_pane_height, 10);
}

} // namespace
} // namespace obcx::common::tui_layout
