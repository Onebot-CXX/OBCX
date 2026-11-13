#include "common/control_fifo.hpp"
#include "common/logger.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace obcx::common {
namespace {

constexpr std::size_t kMaxCommandBytes = 4096;
constexpr int kPollSliceMs = 100;

[[noreturn]] void pipe_error(const std::string &message) {
  throw std::runtime_error(message);
}

auto errno_text() -> std::string {
  return std::generic_category().message(errno);
}

void set_cloexec(int fd) {
  if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    pipe_error("control pipe cloexec failed: " + errno_text());
  }
}

// Bytes that can sit in the pipe at once. A reply larger than the free space
// is dropped whole so a stalled reader cannot split a length-prefixed frame
// or stall the next command.
auto pipe_buffer_capacity(int fd) -> std::size_t {
#if defined(F_GETPIPE_SZ)
  const int size = ::fcntl(fd, F_GETPIPE_SZ);
  if (size > 0) {
    return static_cast<std::size_t>(size);
  }
  return 0;
#elif defined(__APPLE__)
  (void)fd;
  // Darwin pipe buffers are 16 KiB and do not implement F_GETPIPE_SZ.
  return 16 * 1024;
#else
  (void)fd;
  return 0;
#endif
}

auto reply_fits(int write_fd, int read_fd, std::size_t bytes) -> bool {
  const auto capacity = pipe_buffer_capacity(write_fd);
  if (capacity == 0 || bytes > capacity || read_fd < 0) {
    return false;
  }
  int queued = 0;
  if (::ioctl(read_fd, FIONREAD, &queued) != 0 || queued < 0) {
    return false;
  }
  const auto used = static_cast<std::size_t>(queued);
  return used <= capacity && capacity - used >= bytes;
}

} // namespace

ControlFifo::Endpoint::Endpoint(Endpoint &&other) noexcept {
  *this = std::move(other);
}

auto ControlFifo::Endpoint::operator=(Endpoint &&other) noexcept -> Endpoint & {
  if (this != &other) {
    primary = other.primary;
    hold = other.hold;
    path = std::move(other.path);
    device = other.device;
    inode = other.inode;
    owns_path = other.owns_path;
    other.primary = -1;
    other.hold = -1;
    other.device = 0;
    other.inode = 0;
    other.owns_path = false;
    other.path.clear();
  }
  return *this;
}

auto ControlFifo::make_fifo(const std::filesystem::path &path,
                            bool reader_is_primary) -> Endpoint {
  struct stat existing{};
  if (::lstat(path.c_str(), &existing) == 0) {
    pipe_error("control pipe already exists: " + path.string());
  }
  if (errno != ENOENT) {
    pipe_error("control pipe path check failed for " + path.string() + ": " +
               errno_text());
  }
  if (::mkfifo(path.c_str(), 0600) != 0) {
    pipe_error("mkfifo " + path.string() + " failed: " + errno_text());
  }

  struct stat created{};
  if (::lstat(path.c_str(), &created) != 0 || !S_ISFIFO(created.st_mode)) {
    pipe_error("control pipe identity check failed for " + path.string());
  }

  Endpoint endpoint;
  endpoint.path = path;
  endpoint.device = created.st_dev;
  endpoint.inode = created.st_ino;
  endpoint.owns_path = true;
  try {
    // Open the read end first. A nonblocking write to a FIFO with no reader
    // returns ENXIO on both Linux and macOS. Holding our own opposite end
    // keeps the request pipe from reporting EOF when the agent disconnects,
    // and lets the reply pipe accept writes before an agent is attached.
    // O_RDWR is not used: macOS does not provide the Linux FIFO behavior.
    const int reader =
        ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (reader < 0) {
      pipe_error("open " + path.string() + " for read failed: " + errno_text());
    }
    endpoint.primary = reader;
    if (::fchmod(reader, 0600) != 0) {
      pipe_error("fchmod " + path.string() + " failed: " + errno_text());
    }
    struct stat info{};
    if (::fstat(reader, &info) != 0 || info.st_dev != created.st_dev ||
        info.st_ino != created.st_ino || !S_ISFIFO(info.st_mode) ||
        (info.st_mode & 0777) != 0600 || info.st_uid != ::geteuid()) {
      pipe_error("control pipe " + path.string() +
                 " is not a private fifo owned by this user");
    }
    const int writer =
        ::open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (writer < 0) {
      pipe_error("open " + path.string() +
                 " for write failed: " + errno_text());
    }
    endpoint.hold = writer;
    struct stat writer_info{};
    if (::fstat(writer, &writer_info) != 0 ||
        writer_info.st_dev != created.st_dev ||
        writer_info.st_ino != created.st_ino) {
      pipe_error("control pipe writer identity mismatch for " + path.string());
    }
    if (!reader_is_primary) {
      std::swap(endpoint.primary, endpoint.hold);
    }
    return endpoint;
  } catch (...) {
    close_endpoint(endpoint);
    throw;
  }
}

ControlFifo::ControlFifo(CliHandler::Context context)
    : context_(std::move(context)) {}

auto ControlFifo::start(CliHandler::Context context)
    -> std::unique_ptr<ControlFifo> {
  auto fifo = std::unique_ptr<ControlFifo>(new ControlFifo(std::move(context)));
  try {
    fifo->open_pipes();
    fifo->thread_ = std::thread([raw = fifo.get()] { raw->run(); });
  } catch (...) {
    fifo->stop();
    throw;
  }
  return fifo;
}

void ControlFifo::open_pipes() {
  const auto pid = ::getpid();
  const auto directory = std::filesystem::path{"/tmp"};
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error) || error) {
    pipe_error("control pipes require /tmp");
  }
  int wake[2] = {-1, -1};
  if (::pipe(wake) != 0) {
    pipe_error("control wake pipe failed: " + errno_text());
  }
  wake_read_ = wake[0];
  wake_write_ = wake[1];
  try {
    set_cloexec(wake_read_);
    set_cloexec(wake_write_);
    request_ =
        make_fifo(directory / fmt::format("obcx-read-pipe-{}", pid), true);
    reply_ =
        make_fifo(directory / fmt::format("obcx-write-pipe-{}", pid), false);
  } catch (...) {
    close_endpoint(request_);
    close_endpoint(reply_);
    if (wake_read_ >= 0) {
      ::close(wake_read_);
      wake_read_ = -1;
    }
    if (wake_write_ >= 0) {
      ::close(wake_write_);
      wake_write_ = -1;
    }
    throw;
  }
  OBCX_INFO("Control pipes read={} write={}", request_.path.string(),
            reply_.path.string());
}

ControlFifo::~ControlFifo() { stop(); }

void ControlFifo::close_endpoint(Endpoint &endpoint) noexcept {
  if (endpoint.owns_path && !endpoint.path.empty()) {
    struct stat info{};
    if (::lstat(endpoint.path.c_str(), &info) == 0 && S_ISFIFO(info.st_mode) &&
        info.st_dev == endpoint.device && info.st_ino == endpoint.inode) {
      ::unlink(endpoint.path.c_str());
    }
  }
  endpoint.owns_path = false;
  endpoint.path.clear();
  endpoint.device = 0;
  endpoint.inode = 0;
  if (endpoint.primary >= 0) {
    ::close(endpoint.primary);
    endpoint.primary = -1;
  }
  if (endpoint.hold >= 0) {
    ::close(endpoint.hold);
    endpoint.hold = -1;
  }
}

void ControlFifo::stop() noexcept {
  stopping_.store(true, std::memory_order_release);
  if (wake_write_ >= 0) {
    const char byte = 1;
    // The worker also observes stopping_ on its poll timeout.
    [[maybe_unused]] const auto woke = ::write(wake_write_, &byte, 1);
  }
  // A request from the worker only sets the flag. The owning thread still
  // joins and releases the descriptors; skipping that terminates on the
  // joinable std::thread.
  if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
    return;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  close_endpoint(request_);
  close_endpoint(reply_);
  if (wake_read_ >= 0) {
    ::close(wake_read_);
    wake_read_ = -1;
  }
  if (wake_write_ >= 0) {
    ::close(wake_write_);
    wake_write_ = -1;
  }
}

void ControlFifo::run() {
  std::string buffer;
  bool discarding = false;
  std::string *reply_target = nullptr;
  context_.output_cb = [&reply_target](const std::string &message) {
    if (reply_target == nullptr) {
      return;
    }
    if (!reply_target->empty()) {
      reply_target->push_back('\n');
    }
    reply_target->append(message);
  };
  context_.shutdown_cb = nullptr;
  CliHandler handler(context_);

  const auto write_frame = [&](std::string_view body) {
    const auto frame = fmt::format("status {}\n{}", body.size(), body);
    if (!reply_fits(reply_.primary, reply_.hold, frame.size())) {
      OBCX_WARN("Control reply dropped");
      return;
    }
    std::size_t written = 0;
    while (written < frame.size()) {
      const auto count = ::write(reply_.primary, frame.data() + written,
                                 frame.size() - written);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        OBCX_WARN("Control reply dropped");
        return;
      }
      if (count == 0) {
        OBCX_WARN("Control reply dropped");
        return;
      }
      written += static_cast<std::size_t>(count);
    }
  };

  const auto respond = [&](std::string body) {
    if (body.empty() || body.back() != '\n') {
      body.push_back('\n');
    }
    write_frame(body);
  };

  const auto handle_line = [&](std::string line) -> bool {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      return true;
    }
    std::string body;
    reply_target = &body;
    CliHandler::CommandDisposition disposition =
        CliHandler::CommandDisposition::Continue;
    try {
      disposition = handler.dispatch(line);
    } catch (const std::exception &error) {
      OBCX_ERROR("Control command failed: {}", error.what());
      body = "command_failed";
    }
    reply_target = nullptr;
    if (body == "command_failed") {
      respond(std::move(body));
      return true;
    }
    if (disposition == CliHandler::CommandDisposition::Unknown) {
      body = "unknown_command";
    } else if (body.empty()) {
      body =
          disposition == CliHandler::CommandDisposition::Stop ? "exit" : "ok";
    }
    respond(std::move(body));
    if (disposition == CliHandler::CommandDisposition::Stop) {
      CliHandler::request_stop(context_);
      return false;
    }
    return true;
  };

  const auto absorb = [&](std::string_view bytes) {
    buffer.append(bytes);
    for (;;) {
      if (discarding) {
        const auto newline = buffer.find('\n');
        if (newline == std::string::npos) {
          buffer.clear();
          return;
        }
        buffer.erase(0, newline + 1);
        discarding = false;
        continue;
      }
      const auto newline = buffer.find('\n');
      if (newline == std::string::npos) {
        if (buffer.size() > kMaxCommandBytes) {
          buffer.clear();
          discarding = true;
          respond("command_too_long");
        }
        return;
      }
      if (newline > kMaxCommandBytes) {
        buffer.erase(0, newline + 1);
        respond("command_too_long");
        continue;
      }
      auto line = buffer.substr(0, newline);
      buffer.erase(0, newline + 1);
      if (!handle_line(std::move(line)) ||
          stopping_.load(std::memory_order_acquire)) {
        return;
      }
    }
  };

  std::string chunk(4096, '\0');
  while (!stopping_.load(std::memory_order_acquire) &&
         !context_.should_stop.load(std::memory_order_acquire)) {
    pollfd waits[2] = {
        {.fd = request_.primary, .events = POLLIN, .revents = 0},
        {.fd = wake_read_, .events = POLLIN, .revents = 0},
    };
    const auto ready = ::poll(waits, 2, kPollSliceMs);
    if (stopping_.load(std::memory_order_acquire) ||
        context_.should_stop.load(std::memory_order_acquire)) {
      break;
    }
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      OBCX_WARN("Control pipe polling failed");
      break;
    }
    if (ready == 0) {
      continue;
    }
    if ((waits[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
      break;
    }
    if ((waits[0].revents & (POLLERR | POLLNVAL)) != 0) {
      OBCX_WARN("Control request pipe became unavailable");
      break;
    }
    if ((waits[0].revents & POLLIN) == 0) {
      continue;
    }
    const auto count = ::read(request_.primary, chunk.data(), chunk.size());
    if (count < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      OBCX_WARN("Control request pipe read failed");
      break;
    }
    if (count == 0) {
      continue;
    }
    absorb(std::string_view{chunk.data(), static_cast<std::size_t>(count)});
  }
}

} // namespace obcx::common
