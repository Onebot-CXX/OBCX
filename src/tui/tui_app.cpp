#include "tui/tui_app.hpp"
#include "common/cli_handler.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/direction.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace obcx::common {

namespace {

auto level_color(spdlog::level::level_enum level) -> ftxui::Color {
  switch (level) {
  case spdlog::level::trace:
    return ftxui::Color::GrayDark;
  case spdlog::level::debug:
    return ftxui::Color::Cyan;
  case spdlog::level::info:
    return ftxui::Color::Green;
  case spdlog::level::warn:
    return ftxui::Color::Yellow;
  case spdlog::level::err:
  case spdlog::level::critical:
    return ftxui::Color::Red;
  default:
    return ftxui::Color::White;
  }
}

auto level_decorator(spdlog::level::level_enum level) -> ftxui::Decorator {
  if (level == spdlog::level::critical) {
    return ftxui::color(ftxui::Color::Red) | ftxui::bold;
  }
  return ftxui::color(level_color(level));
}

} // namespace

TuiApp::TuiApp(std::shared_ptr<tui_sink_mt> tui_sink,
               CliHandler::Context cli_ctx)
    : tui_sink_(std::move(tui_sink)), cli_ctx_(std::move(cli_ctx)) {}

void TuiApp::run() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);

  std::string input_text;
  std::mutex console_mutex;

  cli_ctx_.output_cb = [this, &console_mutex](const std::string &msg) -> void {
    std::scoped_lock lock(console_mutex);
    console_lines_.push_back(msg);
  };

  CliHandler cli_handler(cli_ctx_);

  auto input_component = ftxui::Input(&input_text, "> ");

  auto input_with_enter = ftxui::CatchEvent(
      input_component, [&](const ftxui::Event &event) -> bool {
        if (event == ftxui::Event::Return) {
          std::string cmd = input_text;
          input_text.clear();

          if (!cmd.empty()) {
            {
              std::scoped_lock lock(console_mutex);
              console_lines_.push_back("> " + cmd);
            }
            if (!cli_handler.process_command(cmd)) {
              // Don't exit immediately — signal shutdown and let it complete
              cli_ctx_.should_stop.store(true, std::memory_order_release);
              cli_ctx_.stop_cv.notify_one();
            }
          }
          return true;
        }
        if (event == ftxui::Event::CtrlC) {
          {
            std::scoped_lock lock(console_mutex);
            console_lines_.emplace_back("Exiting...");
          }
          cli_handler.process_command("exit");
          cli_ctx_.should_stop.store(true, std::memory_order_release);
          cli_ctx_.stop_cv.notify_one();
          return true;
        }
        return false;
      });

  // Background thread to refresh the TUI periodically for new log lines
  // Also monitors should_stop and runs shutdown callback before exiting
  std::thread refresh_thread([&screen, this]() -> void {
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if (cli_ctx_.should_stop.load(std::memory_order_acquire)) {
        bool expected = false;
        if (shutdown_started_.compare_exchange_strong(expected, true)) {
          // Run shutdown callback in a separate thread so TUI keeps refreshing
          if (cli_ctx_.shutdown_cb) {
            std::thread shutdown_thread([this]() -> void {
              cli_ctx_.shutdown_cb();
              shutdown_complete_.store(true, std::memory_order_release);
            });
            shutdown_thread.detach();
          } else {
            shutdown_complete_.store(true, std::memory_order_release);
          }
        }
      }

      if (shutdown_complete_.load(std::memory_order_acquire)) {
        // Give one last refresh so final log lines appear
        screen.PostEvent(ftxui::Event::Custom);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        running_ = false;
        screen.Exit();
        return;
      }

      screen.PostEvent(ftxui::Event::Custom);
    }
  });

  const auto initial_terminal = ftxui::Terminal::Size();
  layout_ = split_state_.snapshot(initial_terminal.dimx, initial_terminal.dimy);

  // Virtualized log pane: wrap retained entries, but only materialize rows in
  // the current viewport.
  auto log_pane = ftxui::Renderer([this]() -> ftxui::Element {
    const int content_width = std::max(1, layout_.log_content_width);
    log_visible_rows_ = std::max(1, layout_.log_content_height);

    const bool reflowing = log_cache_.needs_rebuild(content_width);
    const auto anchor =
        reflowing && !log_viewport_.following_tail()
            ? log_viewport_.capture_anchor(log_cache_, log_visible_rows_)
            : std::nullopt;
    const auto snapshot =
        tui_sink_->snapshot_from(reflowing ? 0 : log_cache_.next_sequence());
    const auto sync = log_cache_.sync(snapshot, content_width);
    log_viewport_.apply_sync(log_cache_, sync, log_visible_rows_, anchor);

    const auto total = log_cache_.total_rows();
    const auto end = total - static_cast<std::size_t>(log_viewport_.offset());
    const auto start = end > static_cast<std::size_t>(log_visible_rows_)
                           ? end - static_cast<std::size_t>(log_visible_rows_)
                           : std::size_t{0};
    const auto rows = log_cache_.rows_range(start, end - start);

    ftxui::Elements log_elements;
    log_elements.reserve(static_cast<std::size_t>(log_visible_rows_));
    if (total == 0) {
      log_elements.push_back(ftxui::text("(no log output yet)") | ftxui::dim);
    } else {
      for (const auto &row : rows) {
        log_elements.push_back(ftxui::text(row.text) |
                               level_decorator(row.level));
      }
    }
    while (static_cast<int>(log_elements.size()) < log_visible_rows_) {
      log_elements.push_back(ftxui::text(""));
    }

    log_scrollbar_ = tui_layout::calculate_scrollbar(total, log_visible_rows_,
                                                     log_viewport_.offset());
    ftxui::Elements scrollbar;
    scrollbar.reserve(static_cast<std::size_t>(log_visible_rows_));
    for (int row = 0; row < log_visible_rows_; ++row) {
      const bool in_thumb =
          row >= log_scrollbar_.thumb_top &&
          row < log_scrollbar_.thumb_top + log_scrollbar_.thumb_height;
      scrollbar.push_back(ftxui::text(" ") |
                          ftxui::bgcolor(in_thumb ? ftxui::Color::GrayLight
                                                  : ftxui::Color::GrayDark));
    }

    return ftxui::hbox({ftxui::vbox(std::move(log_elements)) |
                            ftxui::yflex_grow | ftxui::xflex,
                        ftxui::vbox(std::move(scrollbar)) |
                            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1)}) |
           ftxui::yflex_grow;
  });

  // Console pane: pure renderer, delegates to input_with_enter
  auto console_pane =
      ftxui::Renderer(input_with_enter, [&]() -> ftxui::Element {
        ftxui::Elements console_elements;
        {
          std::scoped_lock lock(console_mutex);
          for (const auto &line : console_lines_) {
            for (const auto &row : tui_layout::wrap_text(
                     line, std::max(1, layout_.console_content_width))) {
              console_elements.push_back(ftxui::text(row));
            }
          }
        }
        console_elements.push_back(input_with_enter->Render());

        auto total = static_cast<int>(console_elements.size());
        console_scroll_offset_ =
            std::clamp(console_scroll_offset_, 0, std::max(0, total - 1));

        float position =
            total > 1 ? 1.f - static_cast<float>(console_scroll_offset_) /
                                  static_cast<float>(total - 1)
                      : 1.f;
        position = std::clamp(position, 0.f, 1.f);

        return ftxui::vbox(std::move(console_elements)) |
               ftxui::focusPositionRelative(0.f, position) |
               ftxui::vscroll_indicator | ftxui::yframe | ftxui::yflex_grow;
      });

  auto log_bordered =
      ftxui::Renderer(log_pane, [&log_pane]() -> ftxui::Element {
        return log_pane->Render() | ftxui::borderStyled(ftxui::Color::Blue) |
               ftxui::bold;
      });

  auto console_bordered =
      ftxui::Renderer(console_pane, [&console_pane]() -> ftxui::Element {
        return console_pane->Render() |
               ftxui::borderStyled(ftxui::Color::Green);
      });

  auto container = ftxui::ResizableSplit({
      .main = log_bordered,
      .back = console_bordered,
      .direction = ftxui::Direction::Up,
      .main_size = &split_state_.main_size(),
      .separator_func = []() -> ftxui::Element {
        return ftxui::separatorHeavy() | ftxui::color(ftxui::Color::GrayLight);
      },
  });

  // Update all geometry before child renderers consume it. SplitState observes
  // FTXUI mouse changes and translates absolute rows back into a ratio.
  auto inner = ftxui::Renderer(container, [&]() -> ftxui::Element {
    const auto terminal = ftxui::Terminal::Size();
    layout_ = split_state_.snapshot(terminal.dimx, terminal.dimy);
    return container->Render();
  });

  // Root input handling consumes the same layout snapshot as rendering.
  auto root = ftxui::CatchEvent(inner, [&](ftxui::Event event) -> bool {
    auto scroll_log_to = [this](int offset) -> void {
      log_viewport_.scroll_to(offset, log_cache_.total_rows(),
                              log_visible_rows_);
    };

    auto scroll_log_by = [this](int delta) -> void {
      log_viewport_.scroll_by(delta, log_cache_.total_rows(),
                              log_visible_rows_);
    };

    auto log_offset_from_thumb_top = [this](int thumb_top) -> int {
      const int max_offset = tui_layout::max_scroll_offset(
          log_cache_.total_rows(), log_visible_rows_);
      const int movable =
          log_scrollbar_.track_height - log_scrollbar_.thumb_height;
      if (max_offset <= 0 || movable <= 0) {
        return 0;
      }
      thumb_top = std::clamp(thumb_top, 0, movable);
      const double progress_from_top = static_cast<double>(thumb_top) / movable;
      return static_cast<int>(
          std::lround((1.0 - progress_from_top) * max_offset));
    };

    if (!event.is_mouse()) {
      if (event == ftxui::Event::ArrowUpCtrl) {
        split_state_.resize_by(-1, layout_.terminal_height);
        return true;
      }
      if (event == ftxui::Event::ArrowDownCtrl) {
        split_state_.resize_by(1, layout_.terminal_height);
        return true;
      }
      if (event == ftxui::Event::PageUp) {
        scroll_log_by(std::max(1, log_visible_rows_ - 1));
        return true;
      }
      if (event == ftxui::Event::PageDown) {
        scroll_log_by(-std::max(1, log_visible_rows_ - 1));
        return true;
      }
      if (event == ftxui::Event::Home) {
        scroll_log_to(tui_layout::max_scroll_offset(log_cache_.total_rows(),
                                                    log_visible_rows_));
        return true;
      }
      if (event == ftxui::Event::End) {
        scroll_log_to(0);
        return true;
      }
      return false;
    }

    auto &mouse = event.mouse();
    const bool in_log_pane = mouse.y >= 0 && mouse.y < layout_.log_pane_height;
    const bool in_console_pane =
        mouse.y > layout_.separator_y && mouse.y < layout_.terminal_height;
    const bool on_log_scrollbar =
        in_log_pane && mouse.x == layout_.log_scrollbar_x &&
        mouse.y >= layout_.log_content_top &&
        mouse.y < layout_.log_content_top + log_scrollbar_.track_height;

    if (log_scrollbar_dragging_) {
      if (mouse.button == ftxui::Mouse::Left &&
          mouse.motion == ftxui::Mouse::Moved) {
        int delta_y = mouse.y - log_scrollbar_drag_start_y_;
        scroll_log_to(log_offset_from_thumb_top(
            log_scrollbar_drag_start_thumb_top_ + delta_y));
        return true;
      }
      if (mouse.button == ftxui::Mouse::Left &&
          mouse.motion == ftxui::Mouse::Released) {
        log_scrollbar_dragging_ = false;
        return true;
      }
    }

    if (mouse.button == ftxui::Mouse::WheelUp) {
      if (in_log_pane) {
        scroll_log_by(3);
      } else if (in_console_pane) {
        console_scroll_offset_ += 3;
      } else {
        return false;
      }
      return true;
    }
    if (mouse.button == ftxui::Mouse::WheelDown) {
      if (in_log_pane) {
        scroll_log_by(-3);
      } else if (in_console_pane) {
        console_scroll_offset_ = std::max(0, console_scroll_offset_ - 3);
      } else {
        return false;
      }
      return true;
    }

    if (on_log_scrollbar && mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      int track_y = std::clamp(mouse.y - layout_.log_content_top, 0,
                               std::max(0, log_scrollbar_.track_height - 1));
      if (track_y >= log_scrollbar_.thumb_top &&
          track_y < log_scrollbar_.thumb_top + log_scrollbar_.thumb_height) {
        log_scrollbar_drag_start_y_ = mouse.y;
        log_scrollbar_drag_start_thumb_top_ = log_scrollbar_.thumb_top;
      } else {
        int movable = std::max(0, log_scrollbar_.track_height -
                                      log_scrollbar_.thumb_height);
        int thumb_top =
            std::clamp(track_y - log_scrollbar_.thumb_height / 2, 0, movable);
        scroll_log_to(log_offset_from_thumb_top(thumb_top));
        log_scrollbar_drag_start_y_ = mouse.y;
        log_scrollbar_drag_start_thumb_top_ = thumb_top;
      }
      log_scrollbar_dragging_ = true;
      return true;
    }

    if (in_console_pane && mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      input_component->TakeFocus();
      return false; // let the event propagate to the input component
    }

    return false;
  });

  screen.Loop(root);

  cli_ctx_.should_stop.store(true, std::memory_order_release);
  cli_ctx_.stop_cv.notify_one();

  running_ = false;
  if (refresh_thread.joinable()) {
    refresh_thread.join();
  }

  // If shutdown didn't start (e.g., SIGINT interrupted screen.Loop()
  // before the refresh thread could start the callback), run it now.
  if (cli_ctx_.shutdown_cb) {
    bool expected = false;
    if (shutdown_started_.compare_exchange_strong(expected, true)) {
      cli_ctx_.shutdown_cb();
    } else {
      // Shutdown was started by the refresh thread but may not be done yet.
      // Wait for it to complete.
      while (!shutdown_complete_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  }
}

} // namespace obcx::common
