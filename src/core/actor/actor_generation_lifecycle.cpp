#include "core/actor/actor_generation_lifecycle.hpp"
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <common/logger.hpp>

namespace obcx::core {
namespace {
namespace asio = boost::asio;
using Lifecycle = ActorGenerationLifecycle;
// Member order is intentional: erased actor closures are destroyed before
// the lease. The control block, Asio wrapper and their destructors live in
// core.
template <class Callback> struct OwnedCallback {
  Lifecycle::WorkLease lease;
  Callback callback;
};
struct SpawnState {
  Lifecycle::WorkLease lease;
  std::function<void(std::exception_ptr)> completion;
  asio::awaitable<void> work;
  asio::cancellation_slot cancellation;
  // If terminal scheduling fails, retain the drain pin rather than permitting
  // DSO unload under a still-unwinding actor awaitable handler. Reload must
  // fail its drain deadline; this catastrophic scheduling failure is not a
  // retry.
  std::shared_ptr<SpawnState> failed_retirement_pin;
  std::exception_ptr terminal_error;
};
void report_failure(const char *message) noexcept {
  try {
    OBCX_ERROR("{}", message);
  } catch (...) {
    // Logging failure must not unwind an Asio handler or defeat its drain pin.
  }
}
void finish(const std::shared_ptr<SpawnState> &state,
            std::exception_ptr error) noexcept {
  try {
    state->completion(std::move(error));
  } catch (...) {
    report_failure("Actor background completion threw");
  }
  // No actor-defined exception may outlive the outer route lease.
}
} // namespace

auto ActorGenerationLifecycle::make_serial_executor(
    asio::any_io_executor executor) -> asio::any_io_executor {
  return asio::make_strand(std::move(executor));
}
void ActorGenerationLifecycle::spawn_owned(
    asio::any_io_executor executor, WorkLease lease, asio::awaitable<void> work,
    std::function<void(std::exception_ptr)> completion,
    asio::cancellation_slot cancellation) {
  std::shared_ptr<SpawnState> state;
  try {
    if (!lease || !completion ||
        !executor.target<asio::strand<asio::any_io_executor>>())
      throw std::invalid_argument(
          "owned spawn requires lease, serial executor and completion");
    state = std::make_shared<SpawnState>(
        std::move(lease), std::move(completion), std::move(work), cancellation);
    // Slot installation and emission share the same serial executor. A stop
    // arriving before installation is also fenced by the actor's token.
    asio::post(executor, [state, executor] {
      try {
        asio::co_spawn(executor, std::move(state->work),
                       asio::bind_cancellation_slot(
                           state->cancellation,
                           [state, executor](std::exception_ptr error) {
                             try {
                               // Never release the last pin in an inline
                               // co_spawn completion under an actor-compiled
                               // awaitable pump.
                               state->terminal_error = std::move(error);
                               asio::post(executor, [state] {
                                 finish(state,
                                        std::move(state->terminal_error));
                               });
                             } catch (...) {
                               state->failed_retirement_pin = state;
                               report_failure("Actor terminal retirement post "
                                              "failed; generation pinned");
                             }
                           }));
      } catch (...) {
        // Already on the core-owned serial initiation handler. If terminal
        // posting failed, its pin deliberately remains even after reporting.
        finish(state, std::current_exception());
      }
    });
  } catch (...) {
    // Allocation/initiation failures must destroy DSO objects before a caller
    // that moved its sole lease here can lose that pin.
    work = {};
    completion = {};
    throw;
  }
}
void ActorGenerationLifecycle::wait_owned(
    asio::steady_timer &timer, WorkLease lease,
    std::function<void(boost::system::error_code)> completion) {
  try {
    if (!lease || !completion)
      throw std::invalid_argument("owned timer requires lease and completion");
    auto state = std::make_shared<OwnedCallback<decltype(completion)>>(
        lease, std::move(completion));
    timer.async_wait([state](const boost::system::error_code &error) {
      try {
        state->callback(error);
      } catch (...) {
        report_failure("Actor timer callback threw");
      }
    });
  } catch (...) {
    // Function-parameter destruction order cannot protect a DSO closure when
    // allocation/initiation fails before it reaches OwnedCallback.
    completion = {};
    throw;
  }
}
void ActorGenerationLifecycle::post_owned(asio::any_io_executor executor,
                                          WorkLease lease,
                                          std::function<void()> completion) {
  try {
    if (!lease || !completion)
      throw std::invalid_argument("owned post requires lease and completion");
    auto state = std::make_shared<OwnedCallback<decltype(completion)>>(
        lease, std::move(completion));
    asio::post(executor, [state] {
      try {
        state->callback();
      } catch (...) {
        report_failure("Actor posted callback threw");
      }
    });
  } catch (...) {
    completion = {};
    throw;
  }
}
} // namespace obcx::core
