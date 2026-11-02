#pragma once

#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace obcx::core {

class BlockingExecutor;

struct FileReadRequest {
  std::string path;
  std::uint64_t maximum_bytes;
  std::chrono::steady_clock::time_point deadline;
};

enum class FileReadFailure { InvalidRequest, Unavailable, OverLimit, Timeout };

class FileReadError final : public std::runtime_error {
public:
  explicit FileReadError(FileReadFailure failure);
  [[nodiscard]] auto failure() const noexcept -> FileReadFailure;

private:
  FileReadFailure failure_;
};

/**
 * Process-only byte acquisition using the exact caller-supplied path. This is
 * not path authorization; ordinary symlinks are followed. Paths must be
 * nonempty, NUL-free and shorter than PATH_MAX; only regular descriptors are
 * read.
 *
 * Cancellation/deadlines are cooperative. Accepted work (including close)
 * drains before completion, even when a kernel/filesystem call cannot be
 * interrupted. Keep the calling executor runnable through completion and
 * serialize upstream cancellation-signal use as required by Asio. Internal
 * cancellation registration, emission and completion run on a private strand.
 * The process owner, not this facade, controls the injected executor's
 * admission and shutdown.
 */
class BoundedFileReader {
public:
  explicit BoundedFileReader(std::shared_ptr<BlockingExecutor> executor);

  // Non-coroutine: validates and captures ownership before returning an
  // awaitable.
  [[nodiscard]] auto read(FileReadRequest request) const
      -> boost::asio::awaitable<std::string>;

private:
  static auto read_owned(std::shared_ptr<BlockingExecutor> executor,
                         FileReadRequest request)
      -> boost::asio::awaitable<std::string>;

  std::shared_ptr<BlockingExecutor> executor_;
};

} // namespace obcx::core
