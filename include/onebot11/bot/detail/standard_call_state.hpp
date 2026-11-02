#pragma once

#include "onebot11/bot/standard_common.hpp"
#include "onebot11/bot/standard_execution.hpp"

#include <boost/system/error_code.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace obcx::onebot11::bot::standard::detail {

struct NativeReplyDisposition;

using StandardDeadline = std::chrono::steady_clock::time_point;

enum class StandardTransportFailureKind {
  Cancelled,
  DeadlineExceeded,
  ConnectionRetired,
  TransportError,
  ResponseTooLarge,
  InvalidResponse
};

struct StandardTransportFailure {
  StandardTransportFailureKind kind;
  boost::system::error_code error;
};

struct StandardWireReply {
  std::string body;
  std::optional<long> http_status;
};

using StandardWireResult =
    std::variant<StandardWireReply, StandardTransportFailure>;

// Process-private, not an installed SDK surface. The caller retains ownership
// across suspensions and proves response completeness/association externally.
class StandardCallState final {
public:
  StandardCallState(const CallLimits &limits, StandardDeadline deadline);
  ~StandardCallState();
  StandardCallState(const StandardCallState &) = delete;
  auto operator=(const StandardCallState &) -> StandardCallState & = delete;
  StandardCallState(StandardCallState &&) = delete;
  auto operator=(StandardCallState &&) -> StandardCallState & = delete;

  [[nodiscard]] auto limits() const noexcept -> CallLimits;
  [[nodiscard]] auto deadline() const noexcept -> StandardDeadline;

  // Final write admission, after external manager/identity authority checks.
  // False changes neither evidence nor terminal state; no wakeup is implied.
  [[nodiscard]] auto try_commit_write() -> bool;
  [[nodiscard]] auto try_complete_reply(std::string_view body,
                                        std::optional<long> http_status)
      -> bool;
  [[nodiscard]] auto try_complete_reply(std::string &&body,
                                        std::optional<long> http_status)
      -> bool;
  [[nodiscard]] auto try_fail(StandardTransportFailure failure) -> bool;
  [[nodiscard]] auto try_fail_before_commit(StandardTransportFailure failure)
      -> bool;
  [[nodiscard]] auto terminal() const -> bool;
  [[nodiscard]] auto take_result() -> std::optional<StandardWireResult>;

  // Trusted seam after full bounded, duplicate-free parsing/classification;
  // a retained reply alone never establishes native-header evidence.
  void observe(const NativeReplyDisposition &disposition);
  [[nodiscard]] auto evidence() const -> ExecutionEvidence;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace obcx::onebot11::bot::standard::detail
