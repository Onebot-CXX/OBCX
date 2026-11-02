#include "standard_actor_events.hpp"

#include "common/logger.hpp"
#include "core/actor/actor.hpp"
#include "core/actor/actor_generation_lifecycle.hpp"
#include "core/bot/bot_installation_directory.hpp"
#include <algorithm>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/prefer.hpp>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace obcx::onebot11::bot::standard {
namespace detail {
namespace {
using Lifecycle = core::ActorGenerationLifecycle;
using Signal = boost::asio::cancellation_signal;
// Constructed only by core-owned contributors, never from an actor callback.
using ActorEventSourceResolver =
    std::function<std::shared_ptr<StandardEventCapabilityV1>()>;

void delivery_failure() noexcept {
  try {
    OBCX_ERROR("Standard actor event delivery failed");
  } catch (...) {
  }
}

// Created only during activation and owned by lifecycle-rooted handler state
// or admitted work. Public subscription controls must never own this strand.
struct ActorEventExecutorState {
  boost::asio::any_io_executor executor;

  explicit ActorEventExecutorState(boost::asio::any_io_executor value)
      : executor(Lifecycle::make_serial_executor(std::move(value))) {}
};

// A cancellation notification is itself joined work. Reverse destruction
// releases the signal and executor state before the generation lease.
struct CancellationDelivery {
  Lifecycle::WorkLease lease;
  std::shared_ptr<ActorEventExecutorState> execution;
  std::shared_ptr<Signal> signal;
  void operator()() noexcept {
    try {
      signal->emit(boost::asio::cancellation_type::all);
    } catch (...) {
      delivery_failure();
    }
  }
};
struct CancellationRecord {
  std::weak_ptr<void> lease;
  std::weak_ptr<ActorEventExecutorState> execution;
  std::weak_ptr<Signal> signal;
};
} // namespace

// This core-only control may outlive retirement through a public RAII handle.
// It never owns an executor, actor handler, frame or strong lifecycle
// reference.
struct ActorEventSubscriptionControl {
  std::mutex mutex;
  bool closed = false;
  Lifecycle::TokenPtr token;
  StandardEventCapabilityV1::Subscription native;
  std::vector<CancellationRecord> cancellations;

  auto accepts(const Lifecycle::TokenPtr &original) -> bool {
    std::lock_guard lock(mutex);
    return !closed && token == original && original->valid();
  }

  void stop(bool terminal) noexcept {
    StandardEventCapabilityV1::Subscription retired;
    std::vector<CancellationRecord> pending;
    {
      std::lock_guard lock(mutex);
      closed = closed || terminal;
      token.reset();
      retired = std::move(native);
      pending.swap(cancellations);
    }
    retired.reset();
    for (const auto &record : pending) {
      // Lock the admitted lease BEFORE executor or signal state. Both their
      // use and destruction may touch the context or actor code. A new work
      // admission after token invalidation would lose cancellation entirely.
      auto lease = record.lease.lock();
      if (!lease)
        continue;
      auto execution = record.execution.lock();
      if (!execution)
        continue;
      auto signal = record.signal.lock();
      if (!signal)
        continue;
      try {
        boost::asio::post(execution->executor,
                          CancellationDelivery{lease, execution, signal});
      } catch (...) {
        delivery_failure();
      }
    }
  }
};

namespace {
struct ActorEventState {
  std::shared_ptr<ActorEventSubscriptionControl> control;
  ActorEventSourceResolver resolve_source;
  std::weak_ptr<Lifecycle> lifecycle;
  boost::asio::any_io_executor bootstrap_executor;
  std::shared_ptr<ActorEventExecutorState> execution;
  ActorEvents::Handler handler;
};

// Every strong reference to handler state outside lifecycle roots is admitted
// first. The lease is deliberately FIRST, hence destroyed LAST, including on
// allocation, post/co_spawn failure and completion-handler teardown.
struct ActorEventJob {
  Lifecycle::WorkLease lease;
  std::shared_ptr<ActorEventState> state;
  Lifecycle::TokenPtr token;
  std::shared_ptr<const StandardEventV1> event;
  std::shared_ptr<Signal> signal;
};

// Not a coroutine lambda: the owned parameter survives suspension. The actor
// awaitable/frame is destroyed inside this helper while the job still owns its
// lease and handler. The completion closure also retains that same job.
auto run_actor_event(std::shared_ptr<ActorEventJob> job)
    -> boost::asio::awaitable<void> {
  try {
    if (job->state->control->accepts(job->token))
      co_await job->state->handler(job->event);
  } catch (...) {
    delivery_failure();
  }
}

void launch_actor_event(const std::shared_ptr<ActorEventJob> &job) noexcept {
  try {
    Lifecycle::spawn_owned(
        job->state->execution->executor, job->lease, run_actor_event(job),
        [job](std::exception_ptr error) noexcept {
          // The core terminal post runs after the final actor awaitable pump
          // unwinds. Merely retaining the job in co_spawn's inline completion
          // could release its final drain pin while that pump still ran DSO
          // code.
          job->signal->slot().clear();
          if (error)
            delivery_failure();
        },
        job->signal->slot());
  } catch (...) {
    job->signal->slot().clear();
    delivery_failure();
  }
}

void deliver_actor_event(
    std::weak_ptr<ActorEventState> weak_state,
    std::weak_ptr<Lifecycle> weak_lifecycle, Lifecycle::TokenPtr original,
    std::shared_ptr<const StandardEventV1> event) noexcept {
  try {
    auto lifecycle = weak_lifecycle.lock();
    if (!lifecycle)
      return;
    auto lease = lifecycle->acquire_work(original);
    if (!lease)
      return;
    auto state = weak_state.lock();
    if (!state)
      return;
    auto job = std::make_shared<ActorEventJob>(ActorEventJob{
        lease, state, original, std::move(event), std::make_shared<Signal>()});
    {
      std::lock_guard lock(state->control->mutex);
      if (state->control->closed || state->control->token != original ||
          !original->valid())
        return;
      auto &records = state->control->cancellations;
      std::erase_if(records,
                    [](const auto &record) { return record.signal.expired(); });
      records.push_back({job->lease, state->execution, job->signal});
    }
    boost::asio::post(state->execution->executor,
                      [job]() noexcept { launch_actor_event(job); });
  } catch (...) {
    delivery_failure();
  }
}

void start_actor_events(const std::shared_ptr<ActorEventState> &state,
                        Lifecycle::TokenPtr token) noexcept {
  try {
    {
      std::lock_guard lock(state->control->mutex);
      if (state->control->closed)
        return;
      state->control->token = token;
    }
    auto lifecycle = state->lifecycle.lock();
    auto lease = lifecycle ? lifecycle->acquire_work(token) : nullptr;
    if (!lease) {
      state->control->stop(false);
      return;
    }
    // Create the strand only while activation protects the live context. Reuse
    // it after an aborted reload: old canceling jobs may still be executing.
    if (!state->execution)
      state->execution =
          std::make_shared<ActorEventExecutorState>(state->bootstrap_executor);
    // Resolve once for this activation, never from a queued delivery/job.
    auto source = state->resolve_source();
    if (!source) {
      state->control->stop(false);
      OBCX_ERROR("Standard actor event source unavailable");
      return;
    }
    auto native = source->subscribe_scoped(
        [weak_state = std::weak_ptr<ActorEventState>{state},
         weak_lifecycle = state->lifecycle,
         token](std::shared_ptr<const StandardEventV1> event) noexcept {
          deliver_actor_event(weak_state, weak_lifecycle, token,
                              std::move(event));
        });
    {
      std::lock_guard lock(state->control->mutex);
      if (!state->control->closed && state->control->token == token)
        state->control->native = std::move(native);
    }
  } catch (...) {
    state->control->stop(false);
    delivery_failure();
  }
}
} // namespace
} // namespace detail

struct ActorEvents::Impl {
  struct Source {
    obcx::bot::BotInstallationRef installation;
    detail::ActorEventSourceResolver resolve_source;
  };
  std::weak_ptr<core::ActorGenerationLifecycle> lifecycle;
  // Runtime supplies its plain pool executor. Explicitly untracked so retained
  // services and rejected post-retirement registrations do not touch the pool.
  boost::asio::any_io_executor bootstrap_executor;
  std::vector<Source> sources;
};

namespace detail {
struct ActorEventsAccess {
  static void contribute(
      core::ActorServices &services,
      std::shared_ptr<core::ActorGenerationLifecycle> lifecycle,
      boost::asio::any_io_executor executor,
      const obcx::bot::BotInstallationRef &installation,
      ActorEventSourceResolver resolve_source) {
    if (!lifecycle || !resolve_source)
      throw std::invalid_argument("standard actor event services unavailable");
    auto service = services.get_service<ActorEvents>();
    if (!service) {
      service = std::shared_ptr<ActorEvents>(
          new ActorEvents{std::make_unique<ActorEvents::Impl>(ActorEvents::Impl{
              lifecycle,
              boost::asio::prefer(
                  std::move(executor),
                  boost::asio::execution::outstanding_work.untracked),
              {}})});
      services.register_service<ActorEvents>(service);
    } else if (service->impl_->lifecycle.lock() != lifecycle) {
      throw std::invalid_argument("standard actor event generation mismatch");
    }
    for (const auto &existing : service->impl_->sources) {
      if (existing.installation == installation)
        throw std::invalid_argument("duplicate standard actor event source");
    }
    service->impl_->sources.push_back(
        {installation, std::move(resolve_source)});
  }
};
namespace {
class NativeContributor final : public core::ActorServiceContributor {
public:
  NativeContributor(obcx::bot::BotInstallationRef installation,
                    std::shared_ptr<StandardEventCapabilityV1> source)
      : installation_(std::move(installation)), source_(std::move(source)) {}

  void contribute(core::ActorServices &services,
                  std::shared_ptr<core::ActorGenerationLifecycle> lifecycle,
                  boost::asio::any_io_executor executor) override {
    ActorEventsAccess::contribute(services, std::move(lifecycle),
                                  std::move(executor), installation_,
                                  [source = source_] { return source; });
  }

  auto source_for(const obcx::bot::BotInstallationRef &installation) const
      -> std::shared_ptr<StandardEventCapabilityV1> {
    return installation == installation_ ? source_ : nullptr;
  }

private:
  obcx::bot::BotInstallationRef installation_;
  std::shared_ptr<StandardEventCapabilityV1> source_;
};

class PlannedContributor final : public core::ActorServiceContributor {
public:
  PlannedContributor(obcx::bot::BotInstallationRef installation,
                     std::weak_ptr<core::BotInstallationDirectory> directory)
      : installation_(std::move(installation)),
        directory_(std::move(directory)) {}

  void contribute(core::ActorServices &services,
                  std::shared_ptr<core::ActorGenerationLifecycle> lifecycle,
                  boost::asio::any_io_executor executor) override {
    ActorEventsAccess::contribute(
        services, std::move(lifecycle), std::move(executor), installation_,
        [installation = installation_, directory = directory_]()
            -> std::shared_ptr<StandardEventCapabilityV1> {
          auto live_directory = directory.lock();
          if (!live_directory)
            return {};
          // The builder already selected the declared capability. This check
          // verifies its private native implementation, not platform support.
          auto native = std::dynamic_pointer_cast<NativeContributor>(
              live_directory->actor_service_contributor(installation));
          return native ? native->source_for(installation) : nullptr;
        });
  }

private:
  obcx::bot::BotInstallationRef installation_;
  std::weak_ptr<core::BotInstallationDirectory> directory_;
};
} // namespace

auto make_actor_service_contributor(
    obcx::bot::BotInstallationRef installation,
    std::shared_ptr<StandardEventCapabilityV1> source)
    -> std::shared_ptr<core::ActorServiceContributor> {
  installation.validate();
  if (installation.surface != obcx::bot::SurfaceId{"onebot11.qq"} || !source)
    throw std::invalid_argument("invalid standard actor event source");
  return std::make_shared<NativeContributor>(std::move(installation),
                                             std::move(source));
}

auto make_planned_standard_actor_service_contributor(
    obcx::bot::BotInstallationRef installation,
    std::weak_ptr<core::BotInstallationDirectory> directory)
    -> std::shared_ptr<core::ActorServiceContributor> {
  installation.validate();
  if (installation.surface != obcx::bot::SurfaceId{"onebot11.qq"})
    throw std::invalid_argument("invalid planned standard actor event source");
  return std::make_shared<PlannedContributor>(std::move(installation),
                                              std::move(directory));
}
} // namespace detail

ActorEvents::ActorEvents(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ActorEvents::~ActorEvents() = default;
ActorEvents::Subscription::Subscription(
    std::shared_ptr<detail::ActorEventSubscriptionControl> control)
    : control_(std::move(control)) {}
ActorEvents::Subscription::Subscription(Subscription &&other) noexcept
    : control_(std::move(other.control_)) {}
auto ActorEvents::Subscription::operator=(Subscription &&other) noexcept
    -> Subscription & {
  if (this != &other) {
    reset();
    control_ = std::move(other.control_);
  }
  return *this;
}
ActorEvents::Subscription::~Subscription() { reset(); }
void ActorEvents::Subscription::reset() noexcept {
  if (auto control = std::exchange(control_, {}))
    control->stop(true);
}
auto ActorEvents::subscribe(const obcx::bot::BotInstallationRef &installation,
                            Handler handler) -> Subscription {
  installation.validate();
  if (!handler)
    throw std::invalid_argument("standard actor event handler required");
  auto lifecycle = impl_->lifecycle.lock();
  if (!lifecycle)
    throw std::invalid_argument("standard actor event generation unavailable");
  auto source = std::find_if(
      impl_->sources.begin(), impl_->sources.end(),
      [&](const auto &entry) { return entry.installation == installation; });
  if (source == impl_->sources.end())
    throw std::invalid_argument(
        "standard actor event installation unavailable");
  auto control = std::make_shared<detail::ActorEventSubscriptionControl>();
  // No strand construction here: a retained lifecycle may already be retired.
  // Only its accepted start callback may use this plain bootstrap executor.
  auto state = std::make_shared<detail::ActorEventState>(
      detail::ActorEventState{control,
                              source->resolve_source,
                              lifecycle,
                              impl_->bootstrap_executor,
                              {},
                              std::move(handler)});
  lifecycle->subscribe(
      [state](core::ActorGenerationLifecycle::TokenPtr token) noexcept {
        detail::start_actor_events(state, std::move(token));
      },
      [state]() noexcept { state->control->stop(false); });
  return Subscription{std::move(control)};
}
} // namespace obcx::onebot11::bot::standard
