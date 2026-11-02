#include "core/bot/bounded_file_reader.hpp"

#include "core/actor/blocking_executor.hpp"

#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace obcx::core {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto failure_message(FileReadFailure failure) -> const char * {
  switch (failure) {
  case FileReadFailure::InvalidRequest:
    return "invalid file read request";
  case FileReadFailure::Unavailable:
    return "file read unavailable";
  case FileReadFailure::OverLimit:
    return "file read exceeds byte limit";
  case FileReadFailure::Timeout:
    return "file read deadline expired";
  }
  return "file read failed";
}

void check_deadline(Clock::time_point deadline) {
  if (Clock::now() >= deadline) {
    throw FileReadError{FileReadFailure::Timeout};
  }
}

void validate_request(const FileReadRequest &request) {
  if (request.path.empty() || request.path.size() >= PATH_MAX ||
      request.path.find('\0') != std::string::npos ||
      request.maximum_bytes == 0 ||
      request.maximum_bytes > std::numeric_limits<std::size_t>::max() ||
      request.maximum_bytes > std::string{}.max_size()) {
    throw FileReadError{FileReadFailure::InvalidRequest};
  }
  check_deadline(request.deadline);
}

struct FileReadStop {
  std::atomic_bool cancelled{false};
};

void check_stop(const FileReadStop &stop, Clock::time_point deadline) {
  if (stop.cancelled.load(std::memory_order_acquire)) {
    throw boost::system::system_error{boost::asio::error::operation_aborted};
  }
  check_deadline(deadline);
}

class FileDescriptor {
public:
  explicit FileDescriptor(int descriptor) : descriptor_(descriptor) {}
  FileDescriptor(const FileDescriptor &) = delete;
  auto operator=(const FileDescriptor &) -> FileDescriptor & = delete;
  ~FileDescriptor() {
    // Never retry close: on Linux the descriptor has already been released,
    // including on EINTR. Only the blocking callable touches this descriptor.
    ::close(descriptor_);
  }
  [[nodiscard]] auto get() const noexcept -> int { return descriptor_; }

private:
  int descriptor_;
};

[[nodiscard]] auto read_file(const FileReadRequest &request,
                             const FileReadStop &stop) -> std::string {
  check_stop(stop, request.deadline);
  // O_NONBLOCK prevents waiting for a FIFO peer. It does not make regular-file
  // kernel I/O interruptible. No canonicalization or no-follow policy is added.
  const auto descriptor =
      ::open(request.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (descriptor < 0) {
    check_stop(stop, request.deadline);
    throw FileReadError{FileReadFailure::Unavailable};
  }
  const FileDescriptor file{descriptor};
  check_stop(stop, request.deadline);
  struct stat status{};
  if (::fstat(file.get(), &status) != 0 || !S_ISREG(status.st_mode)) {
    check_stop(stop, request.deadline);
    throw FileReadError{FileReadFailure::Unavailable};
  }

  std::string bytes;
  std::array<char, 64 * 1024> chunk;
  const auto maximum_bytes = static_cast<std::size_t>(request.maximum_bytes);
  for (;;) {
    check_stop(stop, request.deadline);
    const auto remaining = maximum_bytes - bytes.size();
    // At the limit, a single-byte EOF probe is outside the retained payload.
    // Never add one to maximum_bytes or append the probe to the result.
    const auto count =
        remaining == 0 ? std::size_t{1} : std::min(remaining, chunk.size());
    const auto received = ::read(file.get(), chunk.data(), count);
    const auto read_errno = errno;
    check_stop(stop, request.deadline);
    if (received < 0) {
      if (read_errno == EINTR) {
        continue;
      }
      throw FileReadError{FileReadFailure::Unavailable};
    }
    if (received == 0) {
      return bytes;
    }
    if (remaining == 0) {
      throw FileReadError{FileReadFailure::OverLimit};
    }
    // Grow from actual bytes only, never from stat size or the request limit.
    bytes.append(chunk.data(), static_cast<std::size_t>(received));
  }
}

// Invoked only inside the private co_spawn strand. The slot belongs to that
// child coroutine, not the upstream caller. co_spawn forwards cancellation onto
// this strand; BlockingExecutor posts completion to the same associated strand.
// Clear before invoking the awaitable handler, which may destroy its slot
// owner.
template <typename CompletionToken>
auto async_read_file(std::shared_ptr<BlockingExecutor> executor,
                     FileReadRequest request,
                     std::shared_ptr<FileReadStop> stop,
                     CompletionToken &&token) {
  return boost::asio::async_initiate<
      CompletionToken, void(std::exception_ptr, std::optional<std::string>)>(
      [executor = std::move(executor), request = std::move(request),
       stop = std::move(stop)](auto &&handler) mutable {
        using Handler = std::decay_t<decltype(handler)>;
        // make_shared allocates before moving the handler. A nonthrowing move
        // leaves either the original or shared handler usable on every failure.
        static_assert(std::is_nothrow_move_constructible_v<Handler>);
        std::shared_ptr<Handler> owned_handler;
        boost::asio::cancellation_slot slot;
        std::exception_ptr initiation_exception;
        try {
          slot = boost::asio::get_associated_cancellation_slot(handler);
          owned_handler = std::make_shared<Handler>(
              std::forward<decltype(handler)>(handler));
          const auto completion_executor =
              boost::asio::get_associated_executor(*owned_handler);
          if (slot.is_connected()) {
            slot.assign([stop](boost::asio::cancellation_type_t type) {
              if (type != boost::asio::cancellation_type::none) {
                stop->cancelled.store(true, std::memory_order_release);
              }
            });
          }
          executor->async_run(
              [request = std::move(request), stop] {
                return read_file(request, *stop);
              },
              boost::asio::bind_executor(
                  completion_executor,
                  [owned_handler, slot,
                   executor](std::exception_ptr exception,
                             std::optional<std::string> result) mutable {
                    if (slot.is_connected()) {
                      slot.clear();
                    }
                    (*owned_handler)(std::move(exception), std::move(result));
                  }));
        } catch (...) {
          initiation_exception = std::current_exception();
        }
        if (initiation_exception) {
          // Initiation runs after use_awaitable detaches coroutine ownership.
          // Deliver pre-admission failure instead of abandoning that coroutine.
          // Resume outside the setup catch so resumed execution cannot cause a
          // second completion. No slot access is permitted after invocation.
          if (slot.is_connected()) {
            slot.clear();
          }
          if (owned_handler) {
            (*owned_handler)(std::move(initiation_exception), std::nullopt);
          } else {
            handler(std::move(initiation_exception), std::nullopt);
          }
        }
      },
      token);
}

auto read_serialized(std::shared_ptr<BlockingExecutor> executor,
                     FileReadRequest request)
    -> boost::asio::awaitable<std::string> {
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
    throw boost::system::system_error{boost::asio::error::operation_aborted};
  }
  validate_request(request);
  auto stop = std::make_shared<FileReadStop>();
  const auto deadline = request.deadline;
  std::optional<std::string> result;
  std::exception_ptr exception;
  try {
    result = co_await async_read_file(executor, std::move(request), stop,
                                      boost::asio::use_awaitable);
  } catch (...) {
    exception = std::current_exception();
  }
  // No race-and-return: even cancellation/errors arrive here only after the
  // accepted callable has returned and its descriptor has been closed.
  check_stop(*stop, deadline);
  if (exception) {
    try {
      std::rethrow_exception(exception);
    } catch (const BlockingExecutorStopped &) {
      throw FileReadError{FileReadFailure::Unavailable};
    }
  }
  if (!result) {
    throw FileReadError{FileReadFailure::Unavailable};
  }
  co_return std::move(*result);
}

} // namespace

FileReadError::FileReadError(FileReadFailure failure)
    : std::runtime_error(failure_message(failure)), failure_(failure) {}

auto FileReadError::failure() const noexcept -> FileReadFailure {
  return failure_;
}

BoundedFileReader::BoundedFileReader(std::shared_ptr<BlockingExecutor> executor)
    : executor_(std::move(executor)) {
  if (!executor_) {
    throw FileReadError{FileReadFailure::InvalidRequest};
  }
}

auto BoundedFileReader::read(FileReadRequest request) const
    -> boost::asio::awaitable<std::string> {
  validate_request(request);
  return read_owned(executor_, std::move(request));
}

auto BoundedFileReader::read_owned(std::shared_ptr<BlockingExecutor> executor,
                                   FileReadRequest request)
    -> boost::asio::awaitable<std::string> {
  validate_request(request);
  const auto deadline = request.deadline;
  const auto caller_executor = co_await boost::asio::this_coro::executor;
  auto bytes = co_await boost::asio::co_spawn(
      boost::asio::make_strand(caller_executor),
      read_serialized(std::move(executor), std::move(request)),
      boost::asio::use_awaitable);
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
    throw boost::system::system_error{boost::asio::error::operation_aborted};
  }
  check_deadline(deadline);
  co_return bytes;
}

} // namespace obcx::core
