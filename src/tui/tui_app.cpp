#include "tui/tui_app.hpp"
#include "common/cli_handler.hpp"
#include "tui/tui_selection.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/direction.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace obcx::common {
namespace ts = tui_selection;

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
               CliHandler::Context cli_ctx, InterruptRequest request_interrupt,
               BackgroundTask background)
    : tui_sink_(std::move(tui_sink)), cli_ctx_(std::move(cli_ctx)),
      request_interrupt_(std::move(request_interrupt)),
      background_task_(std::move(background)) {}

void TuiApp::run() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);

  std::string input_text;
  int input_cursor = 0;
  std::mutex console_mutex;
  ts::Selection selection;
  ftxui::CapturedMouse selection_capture;
  ftxui::CapturedMouse scrollbar_capture;
  ftxui::Box jump_to_newest_box;
  bool show_jump_to_newest = false;
  std::optional<ts::Position> console_pin;
  bool divider_dragging = false;
  bool interrupt_requested = false;
  std::string status;
  const char *tmux_environment = std::getenv("TMUX");
  const bool tmux = tmux_environment && *tmux_environment;

  auto clear_selection = [&]() -> void {
    selection.clear();
    selection_capture.reset();
    console_pin.reset();
    status.clear();
  };
  // Call with console_mutex held when using console sources.
  auto source = [&](uint64_t entry) -> const std::string * {
    if (selection.in(ts::Pane::Log)) {
      return log_cache_.source(entry);
    }
    return entry < console_lines_.size() ? &console_lines_[entry] : nullptr;
  };
  auto validate_selection = [&]() -> bool {
    if (!selection.active()) {
      return true;
    }
    bool retained = true;
    if (selection.in(ts::Pane::Log)) {
      // Metadata-only snapshot: cached text must not outlive its source's
      // retention authority between a render frame and the copy key event.
      const auto metadata =
          tui_sink_->snapshot_from(std::numeric_limits<uint64_t>::max());
      const auto [begin, end] = selection.range();
      retained = begin.entry >= metadata.first_sequence &&
                 end.entry < metadata.next_sequence;
    }
    std::scoped_lock lock(console_mutex);
    if (!retained || !selection.valid(source)) {
      clear_selection();
      status = ts::feedback(ts::CopyResult::Unavailable);
      return false;
    }
    return true;
  };

  auto hit = [&](ts::Pane pane, int x, int y, int top, int bottom,
                 bool clamp) -> std::optional<ts::GlyphSpan> {
    std::scoped_lock lock(console_mutex);
    return selection.hit(pane, x, y, top, bottom, clamp,
                         [&](uint64_t entry) -> const std::string * {
                           if (pane == ts::Pane::Log) {
                             return log_cache_.source(entry);
                           }
                           return entry < console_lines_.size()
                                      ? &console_lines_[entry]
                                      : nullptr;
                         });
  };

  cli_ctx_.output_cb = [this, &console_mutex](const std::string &msg) -> void {
    std::scoped_lock lock(console_mutex);
    console_lines_.push_back(strip_ansi(msg));
  };

  std::mutex callback_mu;
  std::thread background_thread;
  std::once_flag background_once;
  std::atomic_bool background_complete{false};
  struct JoinBackground {
    std::thread &thread;
    CliHandler::Context &ctx;
    ~JoinBackground() {
      ctx.should_stop.store(true, std::memory_order_release);
      ctx.stop_cv.notify_one();
      if (thread.joinable() && thread.get_id() != std::this_thread::get_id()) {
        thread.join();
      }
    }
  } join_background{background_thread, cli_ctx_};

  auto input_options = ftxui::InputOption::Default();
  input_options.cursor_position = &input_cursor;
  auto input_component = ftxui::Input(&input_text, "> ", input_options);

  auto input_with_enter = ftxui::CatchEvent(
      input_component, [&](const ftxui::Event &event) -> bool {
        if (event == ftxui::Event::Return) {
          std::string cmd = input_text;
          input_text.clear();
          input_cursor = 0;

          if (!cmd.empty()) {
            {
              std::scoped_lock lock(console_mutex);
              console_lines_.push_back(strip_ansi("> " + cmd));
            }
            auto command_ctx = [&] {
              std::scoped_lock lock(callback_mu);
              return cli_ctx_;
            }();
            // Exit only requests process stop. The TUI leaves after shutdown.
            command_ctx.shutdown_cb = nullptr;
            CliHandler command_handler(std::move(command_ctx));
            if (!command_handler.process_command(cmd)) {
              CliHandler::request_stop(cli_ctx_);
            }
          }
          return true;
        }
        return false;
      });

  // Background thread to refresh the TUI periodically for new log lines
  // Also monitors should_stop and runs shutdown callback before exiting
  std::thread refresh_thread([&screen, &background_complete, this]() -> void {
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if (cli_ctx_.should_stop.load(std::memory_order_acquire)) {
        if (background_task_) {
          // The worker owns runtime cleanup, including partial-start failures.
          shutdown_complete_.store(
              background_complete.load(std::memory_order_acquire),
              std::memory_order_release);
        } else {
          bool expected = false;
          if (shutdown_started_.compare_exchange_strong(expected, true)) {
            // Run shutdown callback in a separate thread so TUI keeps
            // refreshing
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
  auto log_pane = ftxui::Renderer([&]() -> ftxui::Element {
    const int content_width = std::max(1, layout_.log_content_width);
    const int visible_rows = std::max(1, layout_.log_content_height);
    const bool reflowing = log_cache_.needs_rebuild(content_width);
    const bool selection_resized =
        selection.in(ts::Pane::Log) && visible_rows != log_visible_rows_;
    const auto anchor =
        (reflowing || selection_resized) && !log_viewport_.following_tail()
            ? log_viewport_.capture_anchor(log_cache_, log_visible_rows_)
            : std::nullopt;
    log_visible_rows_ = visible_rows;
    const auto snapshot =
        tui_sink_->snapshot_from(reflowing ? 0 : log_cache_.next_sequence());
    const auto sync = log_cache_.sync(snapshot, content_width);
    log_viewport_.apply_sync(log_cache_, sync, log_visible_rows_, anchor);
    validate_selection();
    selection.begin_frame(ts::Pane::Log);

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
        log_elements.push_back(selection.render_row(ts::Pane::Log, row.sequence,
                                                    row.source_begin,
                                                    row.text) |
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

    auto log_content =
        ftxui::vbox(std::move(log_elements)) | ftxui::yflex_grow | ftxui::xflex;
    show_jump_to_newest = !log_viewport_.following_tail() &&
                          layout_.log_content_width > 0 &&
                          layout_.log_content_height > 0;
    if (show_jump_to_newest) {
      auto jump_bar =
          ftxui::text(" ↓ Jump to newest log · End ") |
          ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, content_width) |
          ftxui::color(ftxui::Color::White) |
          ftxui::bgcolor(ftxui::Color::Blue) | ftxui::bold |
          ftxui::reflect(jump_to_newest_box);
      // Overlay the last content row, like Pi's jump-to-latest indicator.
      // Keep viewport, scrollbar and source-selection geometry unchanged.
      log_content = ftxui::dbox({
          log_content,
          ftxui::vbox({ftxui::filler(), ftxui::hbox({ftxui::filler(), jump_bar,
                                                     ftxui::filler()})}),
      });
    }

    // Use terminal geometry, not the longest visible row, even when the
    // jump-to-newest overlay wraps the content in a dbox.
    log_content = log_content | ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
                                            layout_.log_content_width);

    return ftxui::hbox(
               {log_content, ftxui::vbox(std::move(scrollbar)) |
                                 ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1)}) |
           ftxui::yflex_grow;
  });

  // Console pane: pure renderer, delegates to input_with_enter
  auto console_pane =
      ftxui::Renderer(input_with_enter, [&]() -> ftxui::Element {
        selection.begin_frame(ts::Pane::Console);
        ftxui::Elements console_elements;
        {
          std::scoped_lock lock(console_mutex);
          for (std::size_t entry = 0; entry < console_lines_.size(); ++entry) {
            for (const auto &row : tui_layout::wrap_source(
                     console_lines_[entry],
                     std::max(1, layout_.console_content_width))) {
              console_elements.push_back(selection.render_row(
                  ts::Pane::Console, entry, row.source_begin, row.text));
            }
          }
        }
        console_elements.push_back(input_with_enter->Render());

        auto total = static_cast<int>(console_elements.size());
        console_scroll_offset_ =
            std::clamp(console_scroll_offset_, 0, std::max(0, total - 1));

        if (selection.in(ts::Pane::Console) && console_pin) {
          if (const auto row =
                  selection.row_index(ts::Pane::Console, *console_pin)) {
            const int focus =
                *row + std::max(0, layout_.console_content_height - 1) / 2;
            console_scroll_offset_ =
                std::clamp(total - 1 - focus, 0, std::max(0, total - 1));
          }
        }

        return ftxui::vbox(std::move(console_elements)) |
               ftxui::focusPosition(
                   0, std::max(0, total - 1 - console_scroll_offset_)) |
               ftxui::vscroll_indicator | ftxui::yframe | ftxui::yflex_grow;
      });

  auto log_bordered =
      ftxui::Renderer(log_pane, [&log_pane]() -> ftxui::Element {
        return log_pane->Render() | ftxui::borderStyled(ftxui::Color::Blue) |
               ftxui::bold;
      });

  auto console_bordered =
      ftxui::Renderer(console_pane, [&]() -> ftxui::Element {
        const auto title =
            status.empty()
                ? "Ctrl+C: copy/clear | Ctrl+D: exit if empty | Esc: cancel"
                : status;
        return ftxui::window(
            ftxui::text(title) | ftxui::color(ftxui::Color::Green) |
                ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN,
                            std::max(0, layout_.terminal_width - 4)),
            console_pane->Render());
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
    if (background_task_) {
      // A task posted during rendering runs after this frame is displayed.
      std::call_once(background_once, [&] {
        screen.Post([&, this] {
          background_thread =
              std::thread([this, &callback_mu, &background_complete] {
                background_task_(
                    [this, &callback_mu](
                        std::function<void(CliHandler::Context &)> change) {
                      std::scoped_lock lock(callback_mu);
                      change(cli_ctx_);
                    });
                background_complete.store(true, std::memory_order_release);
              });
        });
      });
    }
    const auto terminal = ftxui::Terminal::Size();
    layout_ = split_state_.snapshot(terminal.dimx, terminal.dimy);
    return container->Render();
  });

  // Root input handling consumes the same layout snapshot as rendering.
  auto root = ftxui::CatchEvent(inner, [&](ftxui::Event event) -> bool {
    auto scroll_log_to = [&](int offset) -> void {
      clear_selection();
      log_viewport_.scroll_to(offset, log_cache_.total_rows(),
                              log_visible_rows_);
    };

    auto scroll_log_by = [&](int delta) -> void {
      clear_selection();
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
      // These keys are consumed before the editor, selection fallback or any
      // FTXUI default handler can turn them into deletion or an interrupt.
      if (event == ftxui::Event::CtrlC) {
        if (selection.active()) {
          if (validate_selection()) {
            ts::CopyText copy;
            {
              std::scoped_lock lock(console_mutex);
              copy = selection.extract(source);
            }
            auto result = copy.result;
            if (result == ts::CopyResult::Ready) {
              result = ts::emit_clipboard_request(screen, copy.text, tmux);
            }
            status = ts::feedback(result);
            selection.release();
            selection_capture.reset();
          }
        } else {
          input_text.clear();
          input_cursor = 0;
          status = "Input cleared; Ctrl+D on empty input requests shutdown.";
        }
        return true;
      }
      if (event == ftxui::Event::CtrlD) {
        if (input_text.empty() && !interrupt_requested) {
          if (!request_interrupt_) {
            status = "SIGINT request unavailable; shutdown not requested.";
          } else if (const auto error = request_interrupt_()) {
            status = "SIGINT request failed (error " +
                     std::to_string(error.value()) +
                     "); shutdown not requested.";
          } else {
            interrupt_requested = true;
            status = "SIGINT submitted; waiting for graceful shutdown.";
          }
        }
        return true;
      }
      if (event == ftxui::Event::Escape && selection.active()) {
        clear_selection();
        return true;
      }
      if (event == ftxui::Event::ArrowUpCtrl) {
        clear_selection();
        split_state_.resize_by(-1, layout_.terminal_height);
        return true;
      }
      if (event == ftxui::Event::ArrowDownCtrl) {
        clear_selection();
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

    if (divider_dragging) {
      if (mouse.motion == ftxui::Mouse::Released) {
        divider_dragging = false;
      }
      return false; // the existing ResizableSplit owns its mouse capture
    }

    if (log_scrollbar_dragging_) {
      if (mouse.button == ftxui::Mouse::Left &&
          mouse.motion == ftxui::Mouse::Moved) {
        int delta_y = mouse.y - log_scrollbar_drag_start_y_;
        scroll_log_to(log_offset_from_thumb_top(
            log_scrollbar_drag_start_thumb_top_ + delta_y));
        return true;
      }
      if (mouse.motion == ftxui::Mouse::Released) {
        log_scrollbar_dragging_ = false;
        scrollbar_capture.reset();
        return true;
      }
    }

    if (mouse.button == ftxui::Mouse::WheelUp) {
      clear_selection();
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
      clear_selection();
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
      clear_selection();
      scrollbar_capture = screen.CaptureMouse();
      if (!scrollbar_capture) {
        return false;
      }
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

    if (selection.dragging() && (mouse.motion == ftxui::Mouse::Moved ||
                                 mouse.motion == ftxui::Mouse::Released)) {
      const auto pane = selection.pane();
      const int top = pane == ts::Pane::Log ? layout_.log_content_top
                                            : layout_.separator_y + 2;
      const int bottom = pane == ts::Pane::Log
                             ? top + layout_.log_content_height - 1
                             : layout_.terminal_height - 2;
      const int width = pane == ts::Pane::Log ? layout_.log_content_width
                                              : layout_.console_content_width;
      if (validate_selection()) {
        if (const auto span =
                hit(pane, std::clamp(mouse.x, 1, std::max(1, width)), mouse.y,
                    top, bottom, true)) {
          selection.update(*span);
        }
        if (mouse.motion == ftxui::Mouse::Released) {
          selection.release();
          selection_capture.reset();
        }
      }
      return true; // drag/update/release never copies or changes panes
    }

    if (mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      if (show_jump_to_newest && in_log_pane && mouse.x >= 1 &&
          mouse.x <= layout_.log_content_width &&
          mouse.y >= layout_.log_content_top &&
          mouse.y < layout_.log_content_top + layout_.log_content_height &&
          jump_to_newest_box.Contain(mouse.x, mouse.y)) {
        scroll_log_to(0);
        return true;
      }
      clear_selection();
      if (mouse.y == layout_.separator_y) {
        divider_dragging = true;
        return false;
      }
      const auto pane = in_log_pane ? ts::Pane::Log : ts::Pane::Console;
      const int top =
          in_log_pane ? layout_.log_content_top : layout_.separator_y + 2;
      const int bottom = in_log_pane ? top + layout_.log_content_height - 1
                                     : layout_.terminal_height - 2;
      const int width = in_log_pane ? layout_.log_content_width
                                    : layout_.console_content_width;
      if ((in_log_pane || in_console_pane) && mouse.x >= 1 &&
          mouse.x <= width && mouse.y >= top && mouse.y <= bottom) {
        if (const auto span = hit(pane, mouse.x, mouse.y, top, bottom, false)) {
          selection_capture = screen.CaptureMouse();
          if (!selection_capture) {
            return false;
          }
          selection.start(pane, *span);
          status =
              "Output selected; Ctrl+C copies, Esc cancels (no auto-copy).";
          if (pane == ts::Pane::Log) {
            log_viewport_.pause_following();
          } else {
            console_pin = selection.first_visible(pane, top, bottom);
          }
          return true;
        }
        if (in_console_pane) {
          input_component->TakeFocus();
          input_with_enter->OnEvent(event);
          // Preserve editor hit testing/capture, but suppress global DOM
          // selection if this was empty console padding rather than input.
          return true;
        }
      }
      return true; // no coordinate selection of pane chrome/padding
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
  if (!background_task_ && cli_ctx_.shutdown_cb) {
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
