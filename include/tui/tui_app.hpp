#pragma once

#include "common/cli_handler.hpp"
#include "tui/tui_layout.hpp"
#include "tui/tui_sink.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace obcx::common {

/**
 * \if CHINESE
 * @brief TUI应用程序，使用ftxui实现分屏日志/命令视图
 * \endif
 * \if ENGLISH
 * @brief TUI application using ftxui for split log/command view
 * \endif
 */
class TuiApp {
public:
  /**
   * \if CHINESE
   * @brief 构造TUI应用
   * @param tui_sink 用于获取日志行的TUI sink
   * @param cli_ctx CLI处理器上下文
   * \endif
   * \if ENGLISH
   * @brief Construct TUI application
   * @param tui_sink TUI sink for reading log lines
   * @param cli_ctx CLI handler context
   * \endif
   */
  // The process owns signal delivery; the TUI only requests an interrupt.
  using InterruptRequest = std::function<std::error_code()>;
  TuiApp(std::shared_ptr<tui_sink_mt> tui_sink, CliHandler::Context cli_ctx,
         InterruptRequest request_interrupt);

  /**
   * \if CHINESE
   * @brief 运行TUI主循环（阻塞直到退出）
   * \endif
   * \if ENGLISH
   * @brief Run TUI main loop (blocks until exit)
   * \endif
   */
  void run();

private:
  std::shared_ptr<tui_sink_mt> tui_sink_;
  CliHandler::Context cli_ctx_;
  InterruptRequest request_interrupt_;
  std::vector<std::string> console_lines_;
  std::atomic_bool running_{true};
  std::atomic_bool shutdown_started_{false};
  std::atomic_bool shutdown_complete_{false};
  tui_layout::WrappedLogCache log_cache_;
  tui_layout::LogViewport log_viewport_;
  tui_layout::SplitState split_state_;
  tui_layout::LayoutSnapshot layout_;
  tui_layout::ScrollbarMetrics log_scrollbar_;
  int log_visible_rows_{1};
  bool log_scrollbar_dragging_{false};
  int log_scrollbar_drag_start_y_{0};
  int log_scrollbar_drag_start_thumb_top_{0};
  int console_scroll_offset_{0};
};

} // namespace obcx::common
