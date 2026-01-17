#pragma once

#include <memory>
#include <optional>
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

/*
 * \if CHINESE
 * 包含国际化日志消息头文件
 * \endif
 * \if ENGLISH
 * Include i18n log messages header
 * \endif
 */
#include "common/i18n_log_messages.hpp" // NOLINT

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
   * @brief 从字符串解析日志级别
   * @param level_str 日志级别字符串 (trace, debug, info, warn, error, critical,
   * off)
   * @return 解析成功返回日志级别，失败返回std::nullopt
   * \endif
   * \if ENGLISH
   * @brief Parse log level from string
   * @param level_str Log level string (trace, debug, info, warn, error,
   * critical, off)
   * @return Log level if parsing succeeds, std::nullopt otherwise
   * \endif
   */
  static auto parse_level(const std::string &level_str)
      -> std::optional<spdlog::level::level_enum>;

  /**
   * \if CHINESE
   * @brief 从环境变量获取日志级别
   * @param env_var 环境变量名称 (默认: OBCX_LOG_LEVEL)
   * @param default_level 默认日志级别 (默认: info)
   * @return 日志级别
   * \endif
   * \if ENGLISH
   * @brief Get log level from environment variable
   * @param env_var Environment variable name (default: OBCX_LOG_LEVEL)
   * @param default_level Default log level (default: info)
   * @return Log level
   * \endif
   */
  static auto get_level_from_env(
      const std::string &env_var = "OBCX_LOG_LEVEL",
      spdlog::level::level_enum default_level = spdlog::level::info)
      -> spdlog::level::level_enum;

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

/*
 * \if CHINESE
 * 国际化日志宏定义
 * \endif
 * \if ENGLISH
 * Internationalized logging macro definitions
 * \endif
 */
#define OBCX_I18N_LOG_IMPL(__level, __key, ...)                                \
  do {                                                                         \
    if (obcx::common::Logger::get()->should_log(spdlog::level::__level)) {     \
      std::string __msg =                                                      \
          obcx::common::I18nLogMessages::format_message(__key, ##__VA_ARGS__); \
      obcx::common::Logger::get()->log(spdlog::level::__level, __msg);         \
    }                                                                          \
  } while (false)

#define OBCX_I18N_TRACE(__key, ...)                                            \
  OBCX_I18N_LOG_IMPL(trace, __key, ##__VA_ARGS__)
#define OBCX_I18N_DEBUG_TRACE(__key, ...)                                      \
  OBCX_I18N_LOG_IMPL(debug, __key, ##__VA_ARGS__)
#define OBCX_I18N_INFO(__key, ...)                                             \
  OBCX_I18N_LOG_IMPL(info, __key, ##__VA_ARGS__)
#define OBCX_I18N_WARN(__key, ...)                                             \
  OBCX_I18N_LOG_IMPL(warn, __key, ##__VA_ARGS__)
#define OBCX_I18N_ERROR(__key, ...)                                            \
  OBCX_I18N_LOG_IMPL(err, __key, ##__VA_ARGS__)
#define OBCX_I18N_CRITICAL(__key, ...)                                         \
  OBCX_I18N_LOG_IMPL(critical, __key, ##__VA_ARGS__)

} // namespace obcx::common

/*
 * \if CHINESE
 * Plugin专用日志宏定义
 * 这些宏允许plugin使用自己的logger名称，而不是默认的"obcx"
 * 使用方式：PLUGIN_INFO(get_name(), "message")
 * \endif
 * \if ENGLISH
 * Plugin-specific logging macro definitions
 * These macros allow plugins to use their own logger name instead of the
 * default "obcx" Usage: PLUGIN_INFO(get_name(), "message")
 * \endif
 */

#ifdef OBCX_DEBUG_TRACE
/*
 * \if CHINESE
 * 带位置信息的Plugin日志宏
 * \endif
 * \if ENGLISH
 * Plugin logging macros with location information
 * \endif
 */
#define PLUGIN_LOG_IMPL(__plugin_name, __level, __fmt_str, ...)                \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger && __logger->should_log(spdlog::level::__level)) {            \
      __logger->log(                                                           \
          spdlog::level::__level,                                              \
          fmt::format("{} " __fmt_str,                                         \
                      fmt::styled(fmt::format("[{}:{}]", __FILE__, __LINE__),  \
                                  fmt::fg(fmt::color::dark_orange)),           \
                      ##__VA_ARGS__));                                         \
    }                                                                          \
  } while (false)

#define PLUGIN_TRACE(__plugin_name, __fmt, ...)                                \
  PLUGIN_LOG_IMPL(__plugin_name, trace, __fmt, ##__VA_ARGS__)
#define PLUGIN_DEBUG(__plugin_name, __fmt, ...)                                \
  PLUGIN_LOG_IMPL(__plugin_name, debug, __fmt, ##__VA_ARGS__)
#define PLUGIN_INFO(__plugin_name, __fmt, ...)                                 \
  PLUGIN_LOG_IMPL(__plugin_name, info, __fmt, ##__VA_ARGS__)
#define PLUGIN_WARN(__plugin_name, __fmt, ...)                                 \
  PLUGIN_LOG_IMPL(__plugin_name, warn, __fmt, ##__VA_ARGS__)
#define PLUGIN_ERROR(__plugin_name, __fmt, ...)                                \
  PLUGIN_LOG_IMPL(__plugin_name, err, __fmt, ##__VA_ARGS__)
#define PLUGIN_CRITICAL(__plugin_name, __fmt, ...)                             \
  PLUGIN_LOG_IMPL(__plugin_name, critical, __fmt, ##__VA_ARGS__)

#else
/*
 * \if CHINESE
 * 正常模式下的Plugin日志宏
 * \endif
 * \if ENGLISH
 * Plugin logging macros for normal mode
 * \endif
 */
#define PLUGIN_TRACE(__plugin_name, ...)                                       \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->trace(__VA_ARGS__);                                            \
  } while (false)

#define PLUGIN_DEBUG(__plugin_name, ...)                                       \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->debug(__VA_ARGS__);                                            \
  } while (false)

#define PLUGIN_INFO(__plugin_name, ...)                                        \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->info(__VA_ARGS__);                                             \
  } while (false)

#define PLUGIN_WARN(__plugin_name, ...)                                        \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->warn(__VA_ARGS__);                                             \
  } while (false)

#define PLUGIN_ERROR(__plugin_name, ...)                                       \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->error(__VA_ARGS__);                                            \
  } while (false)

#define PLUGIN_CRITICAL(__plugin_name, ...)                                    \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger)                                                              \
      __logger->critical(__VA_ARGS__);                                         \
  } while (false)

#endif

/*
 * \if CHINESE
 * Plugin专用国际化日志宏定义
 * 使用方式：PLUGIN_I18N_INFO(get_name(), LogMessageKey::XXX, args...)
 * \endif
 * \if ENGLISH
 * Plugin-specific internationalized logging macro definitions
 * Usage: PLUGIN_I18N_INFO(get_name(), LogMessageKey::XXX, args...)
 * \endif
 */
#define PLUGIN_I18N_LOG_IMPL(__plugin_name, __level, __key, ...)               \
  do {                                                                         \
    auto __logger = obcx::common::Logger::get(__plugin_name);                  \
    if (__logger && __logger->should_log(spdlog::level::__level)) {            \
      std::string __msg =                                                      \
          obcx::common::I18nLogMessages::format_message(__key, ##__VA_ARGS__); \
      __logger->log(spdlog::level::__level, __msg);                            \
    }                                                                          \
  } while (false)

#define PLUGIN_I18N_TRACE(__plugin_name, __key, ...)                           \
  PLUGIN_I18N_LOG_IMPL(__plugin_name, trace, __key, ##__VA_ARGS__)
#define PLUGIN_I18N_DEBUG_TRACE(__plugin_name, __key, ...)                     \
  PLUGIN_I18N_LOG_IMPL(__plugin_name, debug, __key, ##__VA_ARGS__)
#define PLUGIN_I18N_INFO(__plugin_name, __key, ...)                            \
  PLUGIN_I18N_LOG_IMPL(__plugin_name, info, __key, ##__VA_ARGS__)
#define PLUGIN_I18N_WARN(__plugin_name, __key, ...)                            \
  PLUGIN_I18N_LOG_IMPL(__plugin_name, warn, __key, ##__VA_ARGS__)
#define PLUGIN_I18N_ERROR(__plugin_name, __key, ...)                           \
  PLUGIN_I18N_LOG_IMPL(__plugin_name, err, __key, ##__VA_ARGS__)
#define PLUGIN_I18N_CRITICAL(__plugin_name, __key, ...)                        \
  PLUGIN_I18N_LOG_IMPL(__plugin_name, critical, __key, ##__VA_ARGS__)
