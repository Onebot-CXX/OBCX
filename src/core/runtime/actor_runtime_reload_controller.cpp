#include "core/runtime/actor_runtime_reload_controller.hpp"
#include "core/runtime/process_configuration.hpp"

#include "common/logger.hpp"
#include "core/runtime/orchestrator.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <fmt/format.h>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace obcx::core {
namespace {

class ReloadSlot {
public:
  explicit ReloadSlot(std::atomic_bool &occupied) : occupied_(&occupied) {}
  ~ReloadSlot() {
    if (occupied_ != nullptr) {
      occupied_->store(false, std::memory_order_release);
    }
  }

  ReloadSlot(const ReloadSlot &) = delete;
  auto operator=(const ReloadSlot &) -> ReloadSlot & = delete;
  ReloadSlot(ReloadSlot &&other) noexcept
      : occupied_(std::exchange(other.occupied_, nullptr)) {}

private:
  std::atomic_bool *occupied_;
};

auto failed_reload(const RuntimeReloadStatus status,
                   const std::uint64_t attempt, const std::uint64_t previous,
                   const std::uint64_t active, std::string code,
                   std::string message) -> RuntimeReloadResult {
  RuntimeReloadResult result{
      .status = status,
      .attempt_id = attempt,
      .previous_generation_id = previous,
      .active_generation_id = active,
      .failure = RuntimeReloadFailure{.code = std::move(code),
                                      .message = std::move(message)}};
  if (result.failure->code == "reload_restart_required") {
    result.changed_domains = result.failure->message;
  }
  return result;
}

auto full_process_restart_required(const std::string_view code) -> bool {
  return code == "reload_restart_required" ||
         code == "reload_dependency_identity_conflict";
}

auto elapsed_ms(const std::chrono::steady_clock::time_point started)
    -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
}

auto describe_config_load_failure(
    const std::string &requested_path,
    const std::optional<common::ConfigLoadDiagnostic> &diagnostic)
    -> std::string {
  if (!diagnostic) {
    return "reload configuration could not be parsed: path=" + requested_path;
  }
  return "reload configuration could not be parsed: diagnostic_code=" +
         diagnostic->code + " path=" + diagnostic->path +
         " line=" + std::to_string(diagnostic->line) +
         " column=" + std::to_string(diagnostic->column);
}

auto shutdown_ingress_result() -> OrchestratorResult {
  OrchestratorResult result;
  result.failures.push_back(OrchestratorFailure{
      .failure = ActorFailure{.code = "reload_shutdown",
                              .message = "actor runtime is shutting down",
                              .retryable = true}});
  return result;
}

auto controller_release_pool() -> boost::asio::thread_pool & {
  static boost::asio::thread_pool pool{1};
  return pool;
}

void cancel_waiters(
    const std::vector<std::shared_ptr<boost::asio::steady_timer>> &waiters) {
  for (const auto &waiter : waiters) {
    try {
      boost::asio::post(waiter->get_executor(), [waiter] {
        // Setting an expired deadline is persistent: it wakes an already
        // registered wait and also makes a wait initiated after this handler
        // complete immediately. A bare cancel can be lost when the gate opens
        // between publishing the timer and initiating async_wait().
        waiter->expires_at(std::chrono::steady_clock::time_point::min());
      });
    } catch (...) {
      // Gate state is authoritative. If an executor is already unavailable,
      // its waiter cannot resume into a different generation.
    }
  }
}

} // namespace

auto runtime_reload_operator_summary(const RuntimeReloadResult &result)
    -> std::string {
  switch (result.status) {
  case RuntimeReloadStatus::Succeeded:
    return fmt::format(
        "========== ACTOR RELOAD SUCCEEDED | generation {} -> {} | "
        "attempt {} | total {} ms ==========",
        result.previous_generation_id, result.active_generation_id,
        result.attempt_id, result.total_ms);
  case RuntimeReloadStatus::Busy:
    return fmt::format(
        "========== ACTOR RELOAD BUSY | generation {} remains active | "
        "attempt {} ==========",
        result.active_generation_id, result.attempt_id);
  case RuntimeReloadStatus::Shutdown:
    return fmt::format(
        "========== ACTOR RELOAD CANCELLED | runtime is shutting down | "
        "attempt {} ==========",
        result.attempt_id);
  case RuntimeReloadStatus::Failed:
    break;
  }

  const auto code = result.failure ? std::string_view{result.failure->code}
                                   : std::string_view{"reload_failed"};
  return fmt::format(
      "========== ACTOR RELOAD FAILED | generation {} remains active | "
      "attempt {} | code={}{} ==========",
      result.active_generation_id, result.attempt_id, code,
      full_process_restart_required(code) ? " | FULL PROCESS RESTART REQUIRED"
                                          : "");
}

class ActorRuntimeReloadController::GateState {
public:
  mutable std::mutex mutex;
  bool open = true;
  bool shutting_down = false;
  std::vector<std::weak_ptr<boost::asio::steady_timer>> waiters;
};

ActorRuntimeReloadController::ActorRuntimeReloadController(
    std::shared_ptr<RuntimeGeneration> active_generation)
    : active_generation_(active_generation),
      gate_state_(std::make_shared<GateState>()),
      builder_(active_generation ? ProcessConfigAccess::catalog(
                                       *active_generation->config_snapshot())
                                 : nullptr) {
  // Construct the neutral release executor on the owning thread. The final
  // keepalive cannot be released on reload_pool_, because the controller
  // destructor joins that pool.
  static_cast<void>(controller_release_pool());
}

ActorRuntimeReloadController::~ActorRuntimeReloadController() {
  shutdown();
  reload_pool_.join();
}

auto ActorRuntimeReloadController::process(MessageEnvelope message)
    -> boost::asio::awaitable<OrchestratorResult> {
  auto executor = co_await boost::asio::this_coro::executor;

  for (;;) {
    std::shared_ptr<RuntimeGeneration> generation;
    RuntimeGeneration::RouteAdmission admission;
    std::shared_ptr<boost::asio::steady_timer> waiter;
    {
      std::scoped_lock lock(gate_state_->mutex);
      if (gate_state_->shutting_down) {
        co_return shutdown_ingress_result();
      }
      if (gate_state_->open) {
        generation = active_generation_.load(std::memory_order_acquire);
        if (!generation) {
          co_return shutdown_ingress_result();
        }
        // Admission is acquired while the gate lock is held, so closing the
        // gate cannot race past a route that already selected this generation.
        admission = generation->admit_route();
        if (!admission) {
          co_return shutdown_ingress_result();
        }
      } else {
        waiter = std::make_shared<boost::asio::steady_timer>(executor);
        waiter->expires_at(std::chrono::steady_clock::time_point::max());
        std::erase_if(gate_state_->waiters,
                      [](const auto &entry) { return entry.expired(); });
        gate_state_->waiters.push_back(waiter);
      }
    }

    if (generation) {
      co_return co_await generation->process(std::move(message),
                                             std::move(admission));
    }

    boost::system::error_code error;
    co_await waiter->async_wait(
        boost::asio::redirect_error(boost::asio::use_awaitable, error));
  }
}

auto ActorRuntimeReloadController::reload(
    RuntimeGenerationBuildRequest request,
    const std::chrono::milliseconds drain_timeout)
    -> boost::asio::awaitable<RuntimeReloadResult> {
  requests_.fetch_add(1, std::memory_order_relaxed);
  const auto attempt = next_attempt_id_.fetch_add(1, std::memory_order_relaxed);
  bool expected = false;
  if (!reload_in_progress_.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
    const auto active = active_generation();
    const auto active_id = active ? active->id() : 0;
    auto result = failed_reload(RuntimeReloadStatus::Busy, attempt, active_id,
                                active_id, "reload_busy",
                                "an actor runtime reload is already running");
    record_result(result);
    co_return result;
  }
  accepted_.fetch_add(1, std::memory_order_relaxed);
  ReloadSlot slot{reload_in_progress_};
  auto result =
      co_await reload_reserved(std::move(request), drain_timeout, attempt);
  record_result(result);
  co_return result;
}

auto ActorRuntimeReloadController::reload_reserved(
    RuntimeGenerationBuildRequest request,
    const std::chrono::milliseconds drain_timeout, const std::uint64_t attempt)
    -> boost::asio::awaitable<RuntimeReloadResult> {
  const auto total_started = std::chrono::steady_clock::now();

  auto active = active_generation();
  if (!active) {
    auto result =
        failed_reload(RuntimeReloadStatus::Shutdown, attempt, 0, 0,
                      "reload_shutdown", "actor runtime is shutting down");
    result.total_ms = elapsed_ms(total_started);
    co_return result;
  }
  OBCX_DEBUG("Actor runtime reload attempt={} active_generation={} "
             "phase=prepare",
             attempt, active->id());

  const auto preparation_started = std::chrono::steady_clock::now();
  if (!request.snapshot) {
    if (request.config_path.empty()) {
      auto result = failed_reload(
          RuntimeReloadStatus::Failed, attempt, active->id(), active->id(),
          "reload_parse_failed", "reload configuration path is missing");
      result.preparation_ms = elapsed_ms(preparation_started);
      result.total_ms = elapsed_ms(total_started);
      co_return result;
    }
    auto parsed = builder_.parse_config(request.config_path);
    if (!parsed) {
      auto result = failed_reload(
          RuntimeReloadStatus::Failed, attempt, active->id(), active->id(),
          "reload_parse_failed",
          describe_config_load_failure(request.config_path, parsed.diagnostic));
      result.preparation_ms = elapsed_ms(preparation_started);
      result.total_ms = elapsed_ms(total_started);
      co_return result;
    }
    request.snapshot = std::move(parsed.snapshot);
  }

  request.purpose = RuntimeGenerationBuildPurpose::ReloadCandidate;
  if (request.generation_id == 0) {
    request.generation_id = active->id() + 1;
  }
  request.active_process_owned_fingerprint =
      active->process_owned_fingerprint();
  request.active_process_owned_dependencies =
      active->process_owned_dependencies();
  request.db_manager = active->db_manager();
  request.bot_installation_directory = active->bot_installation_directory();
  request.bot_operation_client = active->bot_operation_client();
  request.blocking_executor = active->blocking_executor();
  request.require_registered_bots = true;

  auto built = builder_.build(std::move(request));
  const auto preparation_ms = elapsed_ms(preparation_started);
  if (!built.ready()) {
    const auto failure = built.failure.value_or(RuntimeGenerationBuildFailure{
        .code = "reload_actor_graph_invalid",
        .message = "candidate actor runtime is not configured"});
    auto result =
        failed_reload(RuntimeReloadStatus::Failed, attempt, active->id(),
                      active->id(), failure.code, failure.message);
    result.preparation_ms = preparation_ms;
    result.total_ms = elapsed_ms(total_started);
    co_return result;
  }
  OBCX_DEBUG("Actor runtime reload attempt={} candidate_generation={} "
             "phase=prepared duration_ms={}",
             attempt, built.generation->id(), preparation_ms);

  auto result =
      co_await cutover(std::move(built.generation), attempt, drain_timeout);
  result.preparation_ms = preparation_ms;
  result.total_ms = elapsed_ms(total_started);
  co_return result;
}

auto ActorRuntimeReloadController::reload_to(
    std::shared_ptr<RuntimeGeneration> candidate,
    const std::chrono::milliseconds drain_timeout)
    -> boost::asio::awaitable<RuntimeReloadResult> {
  requests_.fetch_add(1, std::memory_order_relaxed);
  const auto attempt = next_attempt_id_.fetch_add(1, std::memory_order_relaxed);
  bool expected = false;
  if (!reload_in_progress_.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
    const auto active = active_generation();
    const auto active_id = active ? active->id() : 0;
    auto result = failed_reload(RuntimeReloadStatus::Busy, attempt, active_id,
                                active_id, "reload_busy",
                                "an actor runtime reload is already running");
    record_result(result);
    co_return result;
  }
  accepted_.fetch_add(1, std::memory_order_relaxed);
  ReloadSlot slot{reload_in_progress_};
  auto result =
      co_await reload_to_reserved(std::move(candidate), drain_timeout, attempt);
  record_result(result);
  co_return result;
}

auto ActorRuntimeReloadController::reload_to_reserved(
    std::shared_ptr<RuntimeGeneration> candidate,
    const std::chrono::milliseconds drain_timeout, const std::uint64_t attempt)
    -> boost::asio::awaitable<RuntimeReloadResult> {
  const auto total_started = std::chrono::steady_clock::now();

  if (!candidate) {
    const auto active = active_generation();
    const auto active_id = active ? active->id() : 0;
    auto result = failed_reload(RuntimeReloadStatus::Failed, attempt, active_id,
                                active_id, "reload_candidate_invalid",
                                "candidate actor runtime is missing");
    result.total_ms = elapsed_ms(total_started);
    co_return result;
  }
  auto result = co_await cutover(std::move(candidate), attempt, drain_timeout);
  result.total_ms = elapsed_ms(total_started);
  co_return result;
}

auto ActorRuntimeReloadController::start_reload(
    RuntimeGenerationBuildRequest request,
    const std::chrono::milliseconds drain_timeout, ReloadCompletion completion)
    -> RuntimeReloadStartStatus {
  requests_.fetch_add(1, std::memory_order_relaxed);
  const auto attempt = next_attempt_id_.fetch_add(1, std::memory_order_relaxed);
  const auto active = active_generation();
  if (!active) {
    auto result =
        failed_reload(RuntimeReloadStatus::Shutdown, attempt, 0, 0,
                      "reload_shutdown", "actor runtime is shutting down");
    record_result(result);
    return RuntimeReloadStartStatus::Shutdown;
  }

  bool expected = false;
  if (!reload_in_progress_.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
    auto result = failed_reload(RuntimeReloadStatus::Busy, attempt,
                                active->id(), active->id(), "reload_busy",
                                "an actor runtime reload is already running");
    record_result(result);
    return RuntimeReloadStartStatus::Busy;
  }
  accepted_.fetch_add(1, std::memory_order_relaxed);

  auto self = shared_from_this();
  boost::asio::co_spawn(
      reload_pool_,
      run_started_reload(std::move(request), drain_timeout, attempt,
                         std::move(completion)),
      [self = std::move(self)](std::exception_ptr exception) mutable {
        if (exception) {
          try {
            std::rethrow_exception(exception);
          } catch (const std::exception &error) {
            OBCX_ERROR("Actor runtime reload worker failed: {}", error.what());
          } catch (...) {
            OBCX_ERROR("Actor runtime reload worker failed with an unknown "
                       "exception");
          }
        }
        // Releasing the final controller owner on its own reload worker would
        // make the destructor join that same thread. Hand the keepalive to a
        // neutral executor so teardown is always safe.
        boost::asio::post(controller_release_pool(),
                          [keepalive = std::move(self)] {});
      });
  return RuntimeReloadStartStatus::Accepted;
}

auto ActorRuntimeReloadController::run_started_reload(
    RuntimeGenerationBuildRequest request,
    const std::chrono::milliseconds drain_timeout, const std::uint64_t attempt,
    ReloadCompletion completion) -> boost::asio::awaitable<void> {
  ReloadSlot slot{reload_in_progress_};
  RuntimeReloadResult result;
  try {
    result =
        co_await reload_reserved(std::move(request), drain_timeout, attempt);
  } catch (const std::exception &error) {
    const auto active = active_generation();
    const auto active_id = active ? active->id() : 0;
    result = failed_reload(RuntimeReloadStatus::Failed, attempt, active_id,
                           active_id, "reload_internal_error",
                           "actor runtime reload failed unexpectedly");
    OBCX_ERROR("Actor runtime reload attempt={} internal error: {}", attempt,
               error.what());
  } catch (...) {
    const auto active = active_generation();
    const auto active_id = active ? active->id() : 0;
    result = failed_reload(RuntimeReloadStatus::Failed, attempt, active_id,
                           active_id, "reload_internal_error",
                           "actor runtime reload failed unexpectedly");
    OBCX_ERROR("Actor runtime reload attempt={} unknown internal error",
               attempt);
  }
  record_result(result);
  if (completion) {
    try {
      completion(result);
    } catch (...) {
      OBCX_ERROR("Actor runtime reload attempt={} completion callback failed",
                 attempt);
    }
  }
  co_return;
}

auto ActorRuntimeReloadController::cutover(
    std::shared_ptr<RuntimeGeneration> candidate,
    const std::uint64_t attempt_id,
    const std::chrono::milliseconds drain_timeout)
    -> boost::asio::awaitable<RuntimeReloadResult> {
  auto previous = active_generation();
  if (!previous) {
    candidate->shutdown();
    co_return failed_reload(RuntimeReloadStatus::Shutdown, attempt_id, 0, 0,
                            "reload_shutdown",
                            "actor runtime is shutting down");
  }
  const auto previous_id = previous->id();
  if (candidate->process_owned_fingerprint() !=
      previous->process_owned_fingerprint()) {
    const auto changed = common::describe_process_owned_changes(
        previous->process_owned_fingerprint(),
        candidate->process_owned_fingerprint());
    candidate->shutdown();
    co_return failed_reload(RuntimeReloadStatus::Failed, attempt_id,
                            previous_id, previous_id, "reload_restart_required",
                            changed);
  }

  {
    std::scoped_lock lock(gate_state_->mutex);
    if (gate_state_->shutting_down ||
        active_generation_.load(std::memory_order_acquire) != previous) {
      candidate->shutdown();
      co_return failed_reload(RuntimeReloadStatus::Shutdown, attempt_id,
                              previous_id, previous_id, "reload_shutdown",
                              "actor runtime is shutting down");
    }
    gate_state_->open = false;
  }
  OBCX_DEBUG("Actor runtime reload attempt={} active_generation={} "
             "candidate_generation={} phase=drain",
             attempt_id, previous_id, candidate->id());

  const auto drain_started = std::chrono::steady_clock::now();
  previous->invalidate_actor_background_work();
  const auto drained = co_await previous->async_wait_for_drain(
      std::chrono::steady_clock::now() + drain_timeout);
  const auto drain_ms = elapsed_ms(drain_started);
  if (!drained) {
    std::vector<std::shared_ptr<boost::asio::steady_timer>> waiters;
    {
      std::scoped_lock lock(gate_state_->mutex);
      if (!gate_state_->shutting_down &&
          active_generation_.load(std::memory_order_acquire) == previous) {
        previous->activate_actor_background_work();
        gate_state_->open = true;
        for (auto &entry : gate_state_->waiters) {
          if (auto waiter = entry.lock()) {
            waiters.push_back(std::move(waiter));
          }
        }
        gate_state_->waiters.clear();
      }
    }
    cancel_waiters(waiters);
    candidate->shutdown();
    const auto active = active_generation();
    const auto active_id = active ? active->id() : 0;
    auto result = failed_reload(
        RuntimeReloadStatus::Failed, attempt_id, previous_id, active_id,
        "reload_drain_timeout",
        "active actor routes did not drain before the reload deadline");
    result.drain_ms = drain_ms;
    co_return result;
  }

  {
    std::scoped_lock lock(gate_state_->mutex);
    if (gate_state_->shutting_down ||
        active_generation_.load(std::memory_order_acquire) != previous) {
      candidate->shutdown();
      auto result =
          failed_reload(RuntimeReloadStatus::Shutdown, attempt_id, previous_id,
                        0, "reload_shutdown", "actor runtime is shutting down");
      result.drain_ms = drain_ms;
      co_return result;
    }
    active_generation_.store(candidate, std::memory_order_release);
  }

  previous->shutdown();
  candidate->activate_command_catalogs();

  std::vector<std::shared_ptr<boost::asio::steady_timer>> waiters;
  {
    std::scoped_lock lock(gate_state_->mutex);
    if (gate_state_->shutting_down ||
        active_generation_.load(std::memory_order_acquire) != candidate) {
      auto result =
          failed_reload(RuntimeReloadStatus::Shutdown, attempt_id, previous_id,
                        0, "reload_shutdown", "actor runtime is shutting down");
      result.drain_ms = drain_ms;
      co_return result;
    }
    candidate->activate_actor_background_work();
    gate_state_->open = true;
    for (auto &entry : gate_state_->waiters) {
      if (auto waiter = entry.lock()) {
        waiters.push_back(std::move(waiter));
      }
    }
    gate_state_->waiters.clear();
  }
  cancel_waiters(waiters);

  co_return RuntimeReloadResult{
      .status = RuntimeReloadStatus::Succeeded,
      .attempt_id = attempt_id,
      .previous_generation_id = previous_id,
      .active_generation_id = candidate->id(),
      .drain_ms = drain_ms,
  };
}

void ActorRuntimeReloadController::activate_command_catalogs() {
  std::scoped_lock lock(gate_state_->mutex);
  if (!gate_state_->shutting_down) {
    if (const auto active = active_generation()) {
      active->activate_actor_background_work();
      active->activate_command_catalogs();
    }
  }
}

void ActorRuntimeReloadController::begin_shutdown() {
  std::vector<std::shared_ptr<boost::asio::steady_timer>> waiters;
  {
    std::scoped_lock lock(gate_state_->mutex);
    if (gate_state_->shutting_down) {
      return;
    }
    gate_state_->shutting_down = true;
    gate_state_->open = false;
    for (auto &entry : gate_state_->waiters) {
      if (auto waiter = entry.lock()) {
        waiters.push_back(std::move(waiter));
      }
    }
    gate_state_->waiters.clear();
  }
  if (const auto active = active_generation()) {
    active->invalidate_actor_background_work();
  }
  cancel_waiters(waiters);
}

void ActorRuntimeReloadController::shutdown() {
  begin_shutdown();
  auto active = active_generation_.exchange(nullptr, std::memory_order_acq_rel);
  if (active) {
    active->shutdown();
  }
}

auto ActorRuntimeReloadController::active_generation() const noexcept
    -> std::shared_ptr<RuntimeGeneration> {
  return active_generation_.load(std::memory_order_acquire);
}

auto ActorRuntimeReloadController::gate_open() const noexcept -> bool {
  std::scoped_lock lock(gate_state_->mutex);
  return gate_state_->open && !gate_state_->shutting_down;
}

auto ActorRuntimeReloadController::reload_in_progress() const noexcept -> bool {
  return reload_in_progress_.load(std::memory_order_acquire);
}

auto ActorRuntimeReloadController::metrics() const noexcept
    -> RuntimeReloadMetrics {
  return {
      .requests = requests_.load(std::memory_order_relaxed),
      .accepted = accepted_.load(std::memory_order_relaxed),
      .succeeded = succeeded_.load(std::memory_order_relaxed),
      .failed = failed_.load(std::memory_order_relaxed),
      .busy = busy_.load(std::memory_order_relaxed),
      .drain_timeouts = drain_timeouts_.load(std::memory_order_relaxed),
  };
}

void ActorRuntimeReloadController::record_result(
    const RuntimeReloadResult &result) noexcept {
  try {
    const auto operator_summary = runtime_reload_operator_summary(result);
    if (result.status == RuntimeReloadStatus::Succeeded) {
      succeeded_.fetch_add(1, std::memory_order_relaxed);
      OBCX_INFO("{}", operator_summary);
      OBCX_DEBUG("Actor reload timing detail: attempt={} prepare_ms={} "
                 "drain_ms={} total_ms={}",
                 result.attempt_id, result.preparation_ms, result.drain_ms,
                 result.total_ms);
      return;
    }
    if (result.status == RuntimeReloadStatus::Busy) {
      busy_.fetch_add(1, std::memory_order_relaxed);
      OBCX_WARN("{}", operator_summary);
      return;
    }

    failed_.fetch_add(1, std::memory_order_relaxed);
    if (result.failure && result.failure->code == "reload_drain_timeout") {
      drain_timeouts_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto code = result.failure ? result.failure->code : "reload_failed";
    const auto reason = result.failure
                            ? result.failure->message
                            : "actor runtime reload failed without a reason";
    if (result.status == RuntimeReloadStatus::Shutdown) {
      OBCX_WARN("{}", operator_summary);
      OBCX_DEBUG("Actor reload cancellation detail: previous_generation={} "
                 "active_generation={} code={} prepare_ms={} drain_ms={} "
                 "total_ms={} reason={}",
                 result.previous_generation_id, result.active_generation_id,
                 code, result.preparation_ms, result.drain_ms, result.total_ms,
                 reason);
      return;
    }

    OBCX_ERROR("{}", operator_summary);
    OBCX_ERROR("Actor reload failure detail: previous_generation={} "
               "active_generation={} code={} changed_domains={} action={} "
               "prepare_ms={} drain_ms={} total_ms={} reason={}",
               result.previous_generation_id, result.active_generation_id, code,
               result.changed_domains.empty() ? "none" : result.changed_domains,
               full_process_restart_required(code)
                   ? "full process restart required"
                   : "inspect failure and retry",
               result.preparation_ms, result.drain_ms, result.total_ms, reason);
  } catch (...) {
    // Metrics and reload state are already committed; logging must never turn
    // a completed transaction into an exception.
  }
}

} // namespace obcx::core
