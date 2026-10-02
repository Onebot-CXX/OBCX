#include "common/logger.hpp"
#include "tui/tui_sink.hpp"

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
std::once_flag Logger::initialization_;
std::mutex Logger::registry_mutex_;

void Logger::initialize(spdlog::level::level_enum level,
                        const std::string &log_file, bool use_tui) {
  std::call_once(initialization_, [&] {
    std::scoped_lock lock(registry_mutex_);
    try {
      std::vector<spdlog::sink_ptr> sinks;

      if (use_tui) {
        // TUI sink captures logs to memory for the TUI to render, replacing
        // direct console output.
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

      if (!log_file.empty()) {
        // 10 MiB per file, keep up to 5 rotated files.
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 1024 * 1024 * 10, 5);
        file_sink->set_level(level);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        sinks.push_back(file_sink);
      }

      default_logger_ =
          std::make_shared<spdlog::logger>("core", sinks.begin(), sinks.end());
      default_logger_->set_level(level);
      default_logger_->flush_on(spdlog::level::warn);

      spdlog::register_logger(default_logger_);
      spdlog::set_default_logger(default_logger_);

      // Do not re-enter initialize through a logging macro inside call_once.
      default_logger_->info("Logger initialized successfully");
    } catch (const spdlog::spdlog_ex &ex) {
      throw std::runtime_error(
          fmt::format("Logger initialization failed: {}", ex.what()));
    }
  });
}

auto Logger::get() -> std::shared_ptr<spdlog::logger> {
  initialize();
  return default_logger_;
}

auto Logger::get(const std::string &name) -> std::shared_ptr<spdlog::logger> {
  initialize();
  std::scoped_lock lock(registry_mutex_);
  auto logger = spdlog::get(name);
  if (!logger) {
    // Clone the default logger so the new named logger inherits sinks/format.
    logger = default_logger_->clone(name);
    spdlog::register_logger(logger);
  }
  return logger;
}

void Logger::set_level(spdlog::level::level_enum level) {
  std::scoped_lock lock(registry_mutex_);
  spdlog::set_level(level);

  spdlog::apply_all(
      [level](const std::shared_ptr<spdlog::logger> &logger) -> void {
        logger->set_level(level);
        for (auto &sink : logger->sinks()) {
          sink->set_level(level);
        }
      });
}

void Logger::flush() {
  std::scoped_lock lock(registry_mutex_);
  if (default_logger_) {
    default_logger_->flush();
  }
  spdlog::apply_all(
      [](const std::shared_ptr<spdlog::logger> &l) -> void { l->flush(); });
}

auto Logger::parse_level(const std::string &level_str)
    -> std::optional<spdlog::level::level_enum> {
  std::string lower_str = level_str;
  std::ranges::transform(
      lower_str, lower_str.begin(),
      [](unsigned char c) -> int { return std::tolower(c); });

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

  return default_level;
}

auto Logger::get_tui_sink() -> std::shared_ptr<tui_sink_mt> {
  std::scoped_lock lock(registry_mutex_);
  return tui_sink_;
}

} // namespace obcx::common
