#ifndef OBCX_INCLUDE_COMMON_LOGGER_HPP_
#define OBCX_INCLUDE_COMMON_LOGGER_HPP_

#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

#ifdef OBCX_DEBUG_TRACE
#include <fmt/color.h>
#include <fmt/format.h>
#endif

namespace obcx::common {

class tui_sink_mt;

/**
 * @brief Logger manager, providing a unified logging interface.
 */
class Logger {
public:
  /**
   * @brief Initializes the logging system.
   * @param level The logging level.
   * @param log_file The path to the log file (optional).
   * @param use_tui Whether to attach the terminal UI sink.
   */
  static void initialize(spdlog::level::level_enum level = spdlog::level::info,
                         const std::string &log_file = "", bool use_tui = true);

  /// @brief Gets the default logger.
  static auto get() -> std::shared_ptr<spdlog::logger>;

  /// @brief Gets a logger with a specific name.
  static auto get(const std::string &name) -> std::shared_ptr<spdlog::logger>;

  /// @brief Sets the logging level.
  static void set_level(spdlog::level::level_enum level);

  /**
   * @brief Parse log level from string
   * @param level_str Log level string (trace, debug, info, warn, error,
   * critical, off)
   * @return Log level if parsing succeeds, std::nullopt otherwise
   */
  static auto parse_level(const std::string &level_str)
      -> std::optional<spdlog::level::level_enum>;

  /**
   * @brief Get log level from environment variable
   * @param env_var Environment variable name (default: OBCX_LOG_LEVEL)
   * @param default_level Default log level (default: info)
   * @return Log level
   */
  static auto get_level_from_env(
      const std::string &env_var = "OBCX_LOG_LEVEL",
      spdlog::level::level_enum default_level = spdlog::level::info)
      -> spdlog::level::level_enum;

  /// @brief Flushes all loggers.
  static void flush();

  /**
   * @brief Get the TUI sink instance
   * @return shared_ptr to TUI sink, nullptr if not initialized
   */
  static auto get_tui_sink() -> std::shared_ptr<tui_sink_mt>;

private:
  static std::shared_ptr<spdlog::logger> default_logger_;
  static std::shared_ptr<tui_sink_mt> tui_sink_;
  static bool initialized_;
};

#ifdef OBCX_DEBUG_TRACE
#define OBCX_LOG_IMPL(__level, __fmt_str, ...)                                 \
  do {                                                                         \
    if (obcx::common::Logger::get()->should_log(spdlog::level::__level)) {     \
      obcx::common::Logger::get()->log(                                        \
          spdlog::level::__level,                                              \
          fmt::format("{} " __fmt_str,                                         \
                      fmt::styled(fmt::format("[{}:{}]", __FILE__, __LINE__),  \
                                  fmt::fg(fmt::color::dark_orange)),           \
                      ##__VA_ARGS__));                                         \
    }                                                                          \
  } while (false)

#define OBCX_TRACE(__fmt, ...) OBCX_LOG_IMPL(trace, __fmt, ##__VA_ARGS__)
#define OBCX_DEBUG(__fmt, ...) OBCX_LOG_IMPL(debug, __fmt, ##__VA_ARGS__)
#define OBCX_INFO(__fmt, ...) OBCX_LOG_IMPL(info, __fmt, ##__VA_ARGS__)
#define OBCX_WARN(__fmt, ...) OBCX_LOG_IMPL(warn, __fmt, ##__VA_ARGS__)
#define OBCX_ERROR(__fmt, ...) OBCX_LOG_IMPL(err, __fmt, ##__VA_ARGS__)
#define OBCX_CRITICAL(__fmt, ...) OBCX_LOG_IMPL(critical, __fmt, ##__VA_ARGS__)
#else
#define OBCX_TRACE(...) obcx::common::Logger::get()->trace(__VA_ARGS__)
#define OBCX_DEBUG(...) obcx::common::Logger::get()->debug(__VA_ARGS__)
#define OBCX_INFO(...) obcx::common::Logger::get()->info(__VA_ARGS__)
#define OBCX_WARN(...) obcx::common::Logger::get()->warn(__VA_ARGS__)
#define OBCX_ERROR(...) obcx::common::Logger::get()->error(__VA_ARGS__)
#define OBCX_CRITICAL(...) obcx::common::Logger::get()->critical(__VA_ARGS__)
#endif

} // namespace obcx::common

#endif // OBCX_INCLUDE_COMMON_LOGGER_HPP_
