module;

import tui.sink;

#include "common/cli_handler.hpp"

export module tui.app;

export namespace obcx::tui
{
    /**
     * \if CHINESE
     * @brief TUI应用程序，使用ftxui实现分屏日志/命令视图
     * \endif
     * \if ENGLISH
     * @brief TUI application using ftxui for split log/command view
     * \endif
     */
    class TuiApp
    {
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
        TuiApp(std::shared_ptr<tui_sink_mt> tui_sink, CliHandler::Context cli_ctx);

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
        std::vector<std::string> console_lines_;
        std::atomic_bool running_{true};
        std::atomic_bool shutdown_started_{false};
        std::atomic_bool shutdown_complete_{false};
        int log_scroll_offset_{0};
        int console_scroll_offset_{0};
    };
} // namespace obcx::common
