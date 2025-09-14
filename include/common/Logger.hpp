#pragma once

#include <memory>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>

/*
 * \if CHINESE
 * 如果启用调试追溯，则包含 fmt 相关的头文件
 * \endif
 * \if ENGLISH
 * Include fmt-related headers if debug tracing is enabled
 * \endif
 */
#ifdef OBCX_DEBUG_TRACE
#include <fmt/color.h>
#include <fmt/format.h>
#endif

namespace obcx::common {

/**
 * \if CHINESE
 * @brief 日志管理器，提供统一的日志接口
 * \endif
 * \if ENGLISH
 * @brief Logger manager, providing a unified logging interface.
 * \endif
 */
class Logger {
public:
  /**
   * \if CHINESE
   * @brief 初始化日志系统
   * @param level 日志级别
   * @param log_file 日志文件路径 (可选)
   * \endif
   * \if ENGLISH
   * @brief Initializes the logging system.
   * @param level The logging level.
   * @param log_file The path to the log file (optional).
   * \endif
   */
  static void initialize(spdlog::level::level_enum level = spdlog::level::info,
                         const std::string &log_file = "");

  /**
   * \if CHINESE
   * @brief 获取默认日志器
   * \endif
   * \if ENGLISH
   * @brief Gets the default logger.
   * \endif
   */
  static auto get() -> std::shared_ptr<spdlog::logger>;

  /**
   * \if CHINESE
   * @brief 获取指定名称的日志器
   * \endif
   * \if ENGLISH
   * @brief Gets a logger with a specific name.
   * \endif
   */
  static auto get(const std::string &name) -> std::shared_ptr<spdlog::logger>;

  /**
   * \if CHINESE
   * @brief 设置日志级别
   * \endif
   * \if ENGLISH
   * @brief Sets the logging level.
   * \endif
   */
  static void set_level(spdlog::level::level_enum level);

  /**
   * \if CHINESE
   * @brief 刷新所有日志器
   * \endif
   * \if ENGLISH
   * @brief Flushes all loggers.
   * \endif
   */
  static void flush();

private:
  static std::shared_ptr<spdlog::logger> default_logger_;
  static bool initialized_;
};

/*
 * \if CHINESE
 * 便利宏定义
 * \endif
 * \if ENGLISH
 * Convenience macro definitions
 * \endif
 */
#ifdef OBCX_DEBUG_TRACE
/*
 * \if CHINESE
 * 辅助宏，用于实现带有位置信息的日志记录
 * \endif
 * \if ENGLISH
 * Helper macro for logging with location information
 * \endif
 */
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
/*
 * \if CHINESE
 * 正常模式下的日志宏
 * \endif
 * \if ENGLISH
 * Logging macros for normal mode
 * \endif
 */
#define OBCX_TRACE(...) obcx::common::Logger::get()->trace(__VA_ARGS__)

#define OBCX_DEBUG(...) obcx::common::Logger::get()->debug(__VA_ARGS__)
#define OBCX_INFO(...) obcx::common::Logger::get()->info(__VA_ARGS__)
#define OBCX_WARN(...) obcx::common::Logger::get()->warn(__VA_ARGS__)
#define OBCX_ERROR(...) obcx::common::Logger::get()->error(__VA_ARGS__)
#define OBCX_CRITICAL(...) obcx::common::Logger::get()->critical(__VA_ARGS__)
#endif

} // namespace obcx::common
