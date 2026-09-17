#ifndef OBCX_INCLUDE_CORE_ACTOR_RUNTIME_RELOAD_CONTROLLER_HPP_
#define OBCX_INCLUDE_CORE_ACTOR_RUNTIME_RELOAD_CONTROLLER_HPP_

#include "core/runtime/runtime_generation.hpp"

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace obcx::core {

enum class RuntimeReloadStatus {
  Succeeded,
  Failed,
  Busy,
  Shutdown,
};

struct RuntimeReloadFailure {
  std::string code;
  std::string message;
};

struct RuntimeReloadResult {
  RuntimeReloadStatus status = RuntimeReloadStatus::Failed;
  std::uint64_t attempt_id = 0;
  std::uint64_t previous_generation_id = 0;
  std::uint64_t active_generation_id = 0;
  std::uint64_t preparation_ms = 0;
  std::uint64_t drain_ms = 0;
  std::uint64_t total_ms = 0;
  std::string changed_domains;
  std::optional<RuntimeReloadFailure> failure;

  [[nodiscard]] auto succeeded() const noexcept -> bool {
    return status == RuntimeReloadStatus::Succeeded;
  }
};

[[nodiscard]] auto runtime_reload_operator_summary(
    const RuntimeReloadResult &result) -> std::string;

enum class RuntimeReloadStartStatus {
  Accepted,
  Busy,
  Shutdown,
};

struct RuntimeReloadMetrics {
  std::uint64_t requests = 0;
  std::uint64_t accepted = 0;
  std::uint64_t succeeded = 0;
  std::uint64_t failed = 0;
  std::uint64_t busy = 0;
  std::uint64_t drain_timeouts = 0;
};

class ActorRuntimeReloadController final
    : public std::enable_shared_from_this<ActorRuntimeReloadController> {
public:
  using ReloadCompletion =
      std::function<void(const RuntimeReloadResult &result)>;

  explicit ActorRuntimeReloadController(
      std::shared_ptr<RuntimeGeneration> active_generation);
  ~ActorRuntimeReloadController();

  ActorRuntimeReloadController(const ActorRuntimeReloadController &) = delete;
  auto operator=(const ActorRuntimeReloadController &)
      -> ActorRuntimeReloadController & = delete;

  auto process(MessageEnvelope message)
      -> boost::asio::awaitable<OrchestratorResult>;

  auto reload(RuntimeGenerationBuildRequest request,
              std::chrono::milliseconds drain_timeout)
      -> boost::asio::awaitable<RuntimeReloadResult>;
  auto reload_to(std::shared_ptr<RuntimeGeneration> candidate,
                 std::chrono::milliseconds drain_timeout)
      -> boost::asio::awaitable<RuntimeReloadResult>;
  auto start_reload(RuntimeGenerationBuildRequest request,
                    std::chrono::milliseconds drain_timeout,
                    ReloadCompletion completion = {})
      -> RuntimeReloadStartStatus;

  void activate_command_catalogs();
  void begin_shutdown();
  void shutdown();

  [[nodiscard]] auto active_generation() const noexcept
      -> std::shared_ptr<RuntimeGeneration>;
  [[nodiscard]] auto gate_open() const noexcept -> bool;
  [[nodiscard]] auto reload_in_progress() const noexcept -> bool;
  [[nodiscard]] auto metrics() const noexcept -> RuntimeReloadMetrics;

private:
  class GateState;

  auto reload_reserved(RuntimeGenerationBuildRequest request,
                       std::chrono::milliseconds drain_timeout,
                       std::uint64_t attempt_id)
      -> boost::asio::awaitable<RuntimeReloadResult>;
  auto reload_to_reserved(std::shared_ptr<RuntimeGeneration> candidate,
                          std::chrono::milliseconds drain_timeout,
                          std::uint64_t attempt_id)
      -> boost::asio::awaitable<RuntimeReloadResult>;
  auto run_started_reload(RuntimeGenerationBuildRequest request,
                          std::chrono::milliseconds drain_timeout,
                          std::uint64_t attempt_id, ReloadCompletion completion)
      -> boost::asio::awaitable<void>;

  auto cutover(std::shared_ptr<RuntimeGeneration> candidate,
               std::uint64_t attempt_id,
               std::chrono::milliseconds drain_timeout)
      -> boost::asio::awaitable<RuntimeReloadResult>;
  void record_result(const RuntimeReloadResult &result) noexcept;

  std::atomic<std::shared_ptr<RuntimeGeneration>> active_generation_;
  std::shared_ptr<GateState> gate_state_;
  std::atomic_bool reload_in_progress_ = false;
  std::atomic_uint64_t next_attempt_id_ = 1;
  std::atomic_uint64_t requests_ = 0;
  std::atomic_uint64_t accepted_ = 0;
  std::atomic_uint64_t succeeded_ = 0;
  std::atomic_uint64_t failed_ = 0;
  std::atomic_uint64_t busy_ = 0;
  std::atomic_uint64_t drain_timeouts_ = 0;
  RuntimeGenerationBuilder builder_;
  boost::asio::thread_pool reload_pool_{1};
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_ACTOR_RUNTIME_RELOAD_CONTROLLER_HPP_
