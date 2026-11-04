#include "common/cli_handler.hpp"
#include "common/logger.hpp"

#include <array>
#include <cerrno>
#include <fmt/format.h>
#include <poll.h>
#include <spdlog/common.h>
#include <string_view>
#include <unistd.h>

namespace obcx::common {

CliHandler::CliHandler(Context ctx) : ctx_(std::move(ctx)) {
  register_default_handlers();
}

void CliHandler::register_default_handlers() {
  handlers_["exit"] = handle_exit;
  handlers_["quit"] = handle_exit;
  handlers_["\x03"] = handle_exit; // Ctrl+C
  handlers_["reload"] = handle_reload;
  handlers_["memory_stats"] = handle_memory_stats;
}

auto CliHandler::process_command(const std::string &line) -> bool {
  if (line.empty()) {
    return true;
  }

  // Check for log_level=xxx pattern
  constexpr std::string_view loglevel_prefix = "log_level=";
  if (line.starts_with(loglevel_prefix)) {
    std::string level_str = line.substr(loglevel_prefix.length());
    return handle_log_level(ctx_, level_str);
  }

  constexpr std::string_view memory_dump_prefix = "memory_dump=";
  if (line.starts_with(memory_dump_prefix)) {
    output(ctx_,
           ctx_.memory_dump_cb
               ? ctx_.memory_dump_cb(line.substr(memory_dump_prefix.size()))
               : "memory_profile_unavailable: no memory profiler attached");
    return true;
  }

  // Check for exact command match
  auto it = handlers_.find(line);
  if (it != handlers_.end()) {
    return it->second(ctx_, "");
  }

  // Unknown command - just ignore
  return true;
}

void CliHandler::run() {
  constexpr int kStopPollIntervalMs = 100;
  std::array<char, 4096> chunk{};
  std::string buffered_input;
  pollfd input{
      .fd = STDIN_FILENO,
      .events = POLLIN,
      .revents = 0,
  };

  const auto process_complete_lines = [&]() -> bool {
    for (;;) {
      const auto newline = buffered_input.find('\n');
      if (newline == std::string::npos) {
        return true;
      }
      auto line = buffered_input.substr(0, newline);
      buffered_input.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!process_command(line)) {
        return false;
      }
    }
  };

  while (!ctx_.should_stop.load(std::memory_order_acquire)) {
    input.revents = 0;
    const auto ready = ::poll(&input, 1, kStopPollIntervalMs);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      OBCX_WARN("CLI input polling failed; stopping command input");
      break;
    }
    if (ready == 0 || ctx_.should_stop.load(std::memory_order_acquire)) {
      continue;
    }

    if ((input.revents & (POLLIN | POLLHUP)) != 0) {
      const auto count = ::read(STDIN_FILENO, chunk.data(), chunk.size());
      if (count > 0) {
        buffered_input.append(chunk.data(), static_cast<std::size_t>(count));
        if (!process_complete_lines()) {
          return;
        }
        continue;
      }
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count < 0) {
        OBCX_WARN("CLI input read failed; stopping command input");
      } else if (!buffered_input.empty()) {
        static_cast<void>(process_command(buffered_input));
      }
      break;
    }

    if ((input.revents & (POLLERR | POLLNVAL)) != 0) {
      OBCX_WARN("CLI input became unavailable; stopping command input");
      break;
    }
  }
}

auto CliHandler::handle_exit(Context &ctx,
                             [[maybe_unused]] const std::string &args) -> bool {
  bool expected = false;
  if (ctx.should_stop.compare_exchange_strong(expected, true)) {
    OBCX_INFO("Received signal {}, shutting down gracefully...", 0);
    ctx.stop_cv.notify_one();
  }
  return false; // Stop the CLI loop
}

void CliHandler::output(Context &ctx, const std::string &msg) {
  if (ctx.output_cb) {
    ctx.output_cb(msg);
  } else {
    fmt::print("{}\n", msg);
  }
}

auto CliHandler::handle_log_level([[maybe_unused]] Context &ctx,
                                  const std::string &level_str) -> bool {
  auto level = Logger::parse_level(level_str);
  if (level.has_value()) {
    Logger::set_level(*level);
    output(ctx, fmt::format("Log level changed to: {}", level_str));
  } else {
    OBCX_WARN("Invalid log level: {}. Valid levels: trace, debug, info, warn, "
              "error, critical, off",
              level_str);
  }
  return true; // Continue the CLI loop
}

auto CliHandler::handle_reload(Context &ctx,
                               [[maybe_unused]] const std::string &args)
    -> bool {
  if (!ctx.reload_cb) {
    output(ctx, "reload_unavailable: actor runtime reload is unavailable");
    return true;
  }

  switch (ctx.reload_cb()) {
  case ReloadRequestStatus::Accepted:
    output(ctx, "ACTOR RELOAD STARTED: wait for the highlighted ACTOR RELOAD "
                "SUCCEEDED/FAILED result");
    break;
  case ReloadRequestStatus::Busy:
    output(ctx, "reload_busy: an actor runtime reload is already running");
    break;
  case ReloadRequestStatus::Unavailable:
    output(ctx, "reload_unavailable: actor runtime reload is unavailable");
    break;
  }
  return true;
}

auto CliHandler::handle_memory_stats(Context &ctx,
                                     [[maybe_unused]] const std::string &args)
    -> bool {
  output(ctx, ctx.memory_stats_cb
                  ? ctx.memory_stats_cb()
                  : "memory_profile_unavailable: no memory profiler attached");
  return true;
}

} // namespace obcx::common
