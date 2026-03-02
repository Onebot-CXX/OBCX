#include "common/logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unordered_map>
#include <vector>

namespace obcx::common {

std::shared_ptr<spdlog::logger> Logger::default_logger_ = nullptr;
std::shared_ptr<tui_sink_mt> Logger::tui_sink_ = nullptr;
bool Logger::initialized_ = false;

void Logger::initialize(spdlog::level::level_enum level,
                        const std::string &log_file, bool use_tui) {
  if (initialized_) {
    return;
  }

  // Initialize i18n messages
  I18nLogMessages::initialize();

  try {
    std::vector<spdlog::sink_ptr> sinks;

    if (use_tui) {
      /*
       * \if CHINESE
       * TUI sink - 捕获日志到内存供TUI渲染（替代控制台输出）
       * \endif
       * \if ENGLISH
       * TUI sink - captures logs to memory for TUI rendering (replaces console
       * output)
       * \endif
       */
      tui_sink_ = std::make_shared<tui_sink_mt>();
      tui_sink_->set_level(level);
      tui_sink_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
      sinks.push_back(tui_sink_);
    } else {
      auto stdout_sink =
          std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      stdout_sink->set_level(level);
      stdout_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
      sinks.push_back(stdout_sink);
    }

    /*
     * \if CHINESE
     * 文件输出 (如果指定了文件路径)
     * \endif
     * \if ENGLISH
     * File output (if file path is specified)
     * \endif
     */
    if (!log_file.empty()) {
      /*
       * \if CHINESE
       * 10MB, 5个文件
       * \endif
       * \if ENGLISH
       * 10MB, 5 files
       * \endif
       */
      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file, 1024 * 1024 * 10, 5);
      file_sink->set_level(level);
      file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
      sinks.push_back(file_sink);
    }

    /*
     * \if CHINESE
     * 创建默认日志器
     * \endif
     * \if ENGLISH
     * Create default logger
     * \endif
     */
    default_logger_ =
        std::make_shared<spdlog::logger>("obcx", sinks.begin(), sinks.end());
    default_logger_->set_level(level);
    default_logger_->flush_on(spdlog::level::warn);

    /*
     * \if CHINESE
     * 注册为默认日志器
     * \endif
     * \if ENGLISH
     * Register as default logger
     * \endif
     */
    spdlog::register_logger(default_logger_);
    spdlog::set_default_logger(default_logger_);

    initialized_ = true;

    OBCX_I18N_INFO(common::LogMessageKey::LOGGER_INIT_SUCCESS);
  } catch (const spdlog::spdlog_ex &ex) {
    throw std::runtime_error(common::I18nLogMessages::format_message(
        common::LogMessageKey::LOGGER_INIT_FAILED_EXCEPTION, ex.what()));
  }
}

auto Logger::get() -> std::shared_ptr<spdlog::logger> {
  if (!initialized_) {
    /*
     * \if CHINESE
     * 使用默认设置初始化
     * \endif
     * \if ENGLISH
     * Initialize with default settings
     * \endif
     */
    initialize();
  }
  return default_logger_;
}

auto Logger::get(const std::string &name) -> std::shared_ptr<spdlog::logger> {
  if (!initialized_) {
    /*
     * \if CHINESE
     * 使用默认设置初始化
     * \endif
     * \if ENGLISH
     * Initialize with default settings
     * \endif
     */
    initialize();
  }

  auto logger = spdlog::get(name);
  if (!logger) {
    /*
     * \if CHINESE
     * 创建新的日志器，使用与默认日志器相同的配置
     * \endif
     * \if ENGLISH
     * Create a new logger with the same configuration as the default logger
     * \endif
     */
    logger = default_logger_->clone(name);
    spdlog::register_logger(logger);
  }
  return logger;
}

void Logger::set_level(spdlog::level::level_enum level) {
  // Set global level
  spdlog::set_level(level);

  // Apply to all registered loggers and their sinks
  spdlog::apply_all([level](const std::shared_ptr<spdlog::logger> &logger) {
    logger->set_level(level);
    for (auto &sink : logger->sinks()) {
      sink->set_level(level);
    }
  });
}

void Logger::flush() {
  if (default_logger_) {
    default_logger_->flush();
  }
  spdlog::apply_all(
      [](const std::shared_ptr<spdlog::logger> &l) { l->flush(); });
}

auto Logger::parse_level(const std::string &level_str)
    -> std::optional<spdlog::level::level_enum> {
  // Convert to lowercase for case-insensitive comparison
  std::string lower_str = level_str;
  std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  static const std::unordered_map<std::string, spdlog::level::level_enum>
      level_map = {
          {"trace", spdlog::level::trace},
          {"debug", spdlog::level::debug},
          {"info", spdlog::level::info},
          {"warn", spdlog::level::warn},
          {"warning", spdlog::level::warn},
          {"error", spdlog::level::err},
          {"err", spdlog::level::err},
          {"critical", spdlog::level::critical},
          {"off", spdlog::level::off},
      };

  auto it = level_map.find(lower_str);
  if (it != level_map.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto Logger::get_level_from_env(const std::string &env_var,
                                spdlog::level::level_enum default_level)
    -> spdlog::level::level_enum {
  const char *env_value = std::getenv(env_var.c_str());
  if (env_value == nullptr || env_value[0] == '\0') {
    return default_level;
  }

  auto level = parse_level(env_value);
  if (level.has_value()) {
    return *level;
  }

  // Invalid level string, return default
  return default_level;
}

auto Logger::get_tui_sink() -> std::shared_ptr<tui_sink_mt> {
  return tui_sink_;
}

} // namespace obcx::common
