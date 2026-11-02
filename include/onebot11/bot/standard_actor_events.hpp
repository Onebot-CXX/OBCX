#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/standard_event_runtime.hpp"
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <memory>

namespace obcx::onebot11::bot::standard {
namespace detail {
struct ActorEventSubscriptionControl;
struct ActorEventsAccess;
} // namespace detail

// Obtained through ActorContext::get_service<ActorEvents>(). Subscribe during
// preparation; delivery starts only when that generation becomes active.
class ActorEvents final {
public:
  using Handler = std::move_only_function<boost::asio::awaitable<void>(
      std::shared_ptr<const StandardEventV1>)>;

  class Subscription final {
  public:
    Subscription() noexcept = default;
    Subscription(const Subscription &) = delete;
    auto operator=(const Subscription &) -> Subscription & = delete;
    Subscription(Subscription &&) noexcept;
    auto operator=(Subscription &&) noexcept -> Subscription &;
    ~Subscription();
    // Terminal, nonblocking cancellation. Already admitted work drains through
    // the generation; cancellation does not forcibly interrupt actor code.
    void reset() noexcept;

  private:
    friend class ActorEvents;
    explicit Subscription(
        std::shared_ptr<detail::ActorEventSubscriptionControl>);
    std::shared_ptr<detail::ActorEventSubscriptionControl> control_;
  };

  ActorEvents(const ActorEvents &) = delete;
  auto operator=(const ActorEvents &) -> ActorEvents & = delete;
  ~ActorEvents();
  [[nodiscard]] auto subscribe(const obcx::bot::BotInstallationRef &, Handler)
      -> Subscription;

private:
  friend struct detail::ActorEventsAccess;
  struct Impl;
  explicit ActorEvents(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
} // namespace obcx::onebot11::bot::standard
