#include "common/Logger.hpp"
#include <spdlog/async.h>
#include <vector>

namespace obcx::common {

std::shared_ptr<spdlog::logger> Logger::default_logger_ = nullptr;
bool Logger::initialized_ = false;

void Logger::initialize(spdlog::level::level_enum level,
                        const std::string &log_file) {
  if (initialized_) {
    return;
  }

  try {
    std::vector<spdlog::sink_ptr> sinks;

    /*
     * \if CHINESE
     * 控制台输出
     * \endif
     * \if ENGLISH
     * Console output
     * \endif
     */
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(level);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    sinks.push_back(console_sink);

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

    OBCX_INFO("Logger initialized successfully");
  } catch (const spdlog::spdlog_ex &ex) {
    throw std::runtime_error("Logger initialization failed: " +
                             std::string(ex.what()));
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
  if (default_logger_) {
    default_logger_->set_level(level);
    spdlog::set_level(level);
  }
}

void Logger::flush() {
  if (default_logger_) {
    default_logger_->flush();
  }
  spdlog::apply_all(
      [](const std::shared_ptr<spdlog::logger> &l) { l->flush(); });
}

} // namespace obcx::common