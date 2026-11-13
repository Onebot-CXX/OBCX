#pragma once

#include "common/cli_handler.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <sys/types.h>
#include <thread>

namespace obcx::common {

// Two FIFOs under /tmp, named with this process id. An agent writes commands
// to obcx-read-pipe-<pid> and reads framed replies from obcx-write-pipe-<pid>.
class ControlFifo {
public:
  static auto start(CliHandler::Context context)
      -> std::unique_ptr<ControlFifo>;

  ControlFifo(const ControlFifo &) = delete;
  auto operator=(const ControlFifo &) -> ControlFifo & = delete;
  ControlFifo(ControlFifo &&) = delete;
  auto operator=(ControlFifo &&) -> ControlFifo & = delete;
  ~ControlFifo();

  void stop() noexcept;

private:
  struct Endpoint {
    int primary = -1;
    int hold = -1;
    std::filesystem::path path;
    dev_t device = 0;
    ino_t inode = 0;
    bool owns_path = false;

    Endpoint() = default;
    Endpoint(const Endpoint &) = delete;
    auto operator=(const Endpoint &) -> Endpoint & = delete;
    Endpoint(Endpoint &&other) noexcept;
    auto operator=(Endpoint &&other) noexcept -> Endpoint &;
  };

  explicit ControlFifo(CliHandler::Context context);

  static auto make_fifo(const std::filesystem::path &path,
                        bool reader_is_primary) -> Endpoint;
  void open_pipes();
  void run();
  static void close_endpoint(Endpoint &endpoint) noexcept;

  CliHandler::Context context_;
  Endpoint request_;
  Endpoint reply_;
  int wake_read_ = -1;
  int wake_write_ = -1;
  std::atomic_bool stopping_{false};
  std::thread thread_;
};

} // namespace obcx::common
