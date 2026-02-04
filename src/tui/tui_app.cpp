#include "tui/tui_app.hpp"
#include "common/cli_handler.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace obcx::common {

namespace {

auto strip_ansi(const std::string &input) -> std::string {
  static const std::regex ansi_re("\033\\[[0-9;]*m");
  return std::regex_replace(input, ansi_re, "");
}

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
    return ftxui::Color::Red;
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

  std::string input_text;
  std::mutex console_mutex;

  // Set up output callback so CliHandler writes to our console pane
  cli_ctx_.output_cb = [this, &console_mutex](const std::string &msg) {
    std::lock_guard lock(console_mutex);
    console_lines_.push_back(msg);
  };

  CliHandler cli_handler(cli_ctx_);

  // Input component
  auto input_component = ftxui::Input(&input_text, "> ");

  // Wrap input to handle Enter key
  auto input_with_enter = ftxui::CatchEvent(
      input_component, [&](const ftxui::Event &event) -> bool {
        if (event == ftxui::Event::Return) {
          std::string cmd = input_text;
          input_text.clear();

          if (!cmd.empty()) {
            {
              std::lock_guard lock(console_mutex);
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
        // Ctrl+C
        if (event == ftxui::Event::Character('\x03')) {
          {
            std::lock_guard lock(console_mutex);
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
  std::thread refresh_thread([&screen, this]() {
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if (cli_ctx_.should_stop.load(std::memory_order_acquire)) {
        bool expected = false;
        if (shutdown_started_.compare_exchange_strong(expected, true)) {
          // Run shutdown callback in a separate thread so TUI keeps refreshing
          if (cli_ctx_.shutdown_cb) {
            std::thread shutdown_thread([this]() {
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

  // Renderer
  int log_pane_size = 0; // Will be set by ResizableSplit

  // Log pane: pure renderer, no event handling here
  auto log_pane = ftxui::Renderer([this] {
    auto lines = tui_sink_->get_lines();
    ftxui::Elements log_elements;
    log_elements.reserve(lines.size());
    for (const auto &line : lines) {
      log_elements.push_back(ftxui::text(strip_ansi(line.text)) |
                             level_decorator(line.level));
    }
    if (log_elements.empty()) {
      log_elements.push_back(ftxui::text("(no log output yet)") | ftxui::dim);
    }

    auto total = static_cast<int>(log_elements.size());
    log_scroll_offset_ = std::clamp(log_scroll_offset_, 0, total - 1);

    // Compute scroll position: 0 offset = bottom (1.0), max offset = top (0.0)
    float position = total > 1 ? 1.f - static_cast<float>(log_scroll_offset_) /
                                           static_cast<float>(total - 1)
                               : 1.f;
    position = std::clamp(position, 0.f, 1.f);

    return ftxui::vbox(std::move(log_elements)) |
           ftxui::focusPositionRelative(0.f, position) |
           ftxui::vscroll_indicator | ftxui::yframe | ftxui::yflex_grow;
  });

  // Console pane: pure renderer, delegates to input_with_enter
  auto console_pane = ftxui::Renderer(input_with_enter, [&] {
    ftxui::Elements console_elements;
    {
      std::lock_guard lock(console_mutex);
      console_elements.reserve(console_lines_.size());
      for (const auto &line : console_lines_) {
        console_elements.push_back(ftxui::text(line));
      }
    }
    console_elements.push_back(input_with_enter->Render());

    auto total = static_cast<int>(console_elements.size());
    console_scroll_offset_ =
        std::clamp(console_scroll_offset_, 0, std::max(0, total - 1));

    float position = total > 1
                         ? 1.f - static_cast<float>(console_scroll_offset_) /
                                     static_cast<float>(total - 1)
                         : 1.f;
    position = std::clamp(position, 0.f, 1.f);

    return ftxui::vbox(std::move(console_elements)) |
           ftxui::focusPositionRelative(0.f, position) |
           ftxui::vscroll_indicator | ftxui::yframe | ftxui::yflex_grow;
  });

  // Wrap panes with colored borders
  auto log_bordered = ftxui::Renderer(log_pane, [&log_pane] {
    return log_pane->Render() | ftxui::borderStyled(ftxui::Color::Blue) |
           ftxui::bold;
  });

  auto console_bordered = ftxui::Renderer(console_pane, [&console_pane] {
    return console_pane->Render() | ftxui::borderStyled(ftxui::Color::Green);
  });

  auto container =
      ftxui::ResizableSplitTop(log_bordered, console_bordered, &log_pane_size);

  // Set initial split ratio (70/30)
  bool size_initialized = false;

  auto inner = ftxui::Renderer(container, [&] {
    auto element = container->Render();
    if (!size_initialized) {
      log_pane_size = ftxui::Terminal::Size().dimy * 7 / 10;
      size_initialized = true;
    }
    return element;
  });

  // Root: handle all mouse events here using mouse.y vs log_pane_size
  // to route scroll and click events to the correct pane.
  auto root = ftxui::CatchEvent(inner, [&](ftxui::Event event) -> bool {
    if (!event.is_mouse()) {
      return false;
    }

    auto &mouse = event.mouse();
    bool in_log_pane = mouse.y < log_pane_size;

    // Scroll wheel → route to the correct pane
    if (mouse.button == ftxui::Mouse::WheelUp) {
      if (in_log_pane) {
        log_scroll_offset_ += 3;
      } else {
        console_scroll_offset_ += 3;
      }
      return true;
    }
    if (mouse.button == ftxui::Mouse::WheelDown) {
      if (in_log_pane) {
        log_scroll_offset_ -= 3;
        if (log_scroll_offset_ < 0) {
          log_scroll_offset_ = 0;
        }
      } else {
        console_scroll_offset_ -= 3;
        if (console_scroll_offset_ < 0) {
          console_scroll_offset_ = 0;
        }
      }
      return true;
    }

    // Click in console pane → focus the input
    if (!in_log_pane && mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      input_component->TakeFocus();
      return false; // let the event propagate to the input component
    }

    return false;
  });

  screen.Loop(root);

  // Ensure should_stop is set after TUI exits
  cli_ctx_.should_stop.store(true, std::memory_order_release);
  cli_ctx_.stop_cv.notify_one();

  // Cleanup
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
