#pragma once

#include "core/bot/references.hpp"
#include "onebot11/bot/standard_events.hpp"
#include <functional>
#include <memory>
#include <string_view>

namespace obcx::onebot11::bot::standard {
inline constexpr std::string_view standard_event_capability_v1 =
    "onebot11.standard.events.v1";
namespace detail {
struct StandardEventContext;
struct StandardEventAccess;
} // namespace detail

// Not constructible from observations, flags, references or serialized tokens.
class EventActionHandle final {
public:
  EventActionHandle(const EventActionHandle &) = default;
  EventActionHandle(EventActionHandle &&) noexcept = default;
  auto operator=(const EventActionHandle &) -> EventActionHandle & = default;
  auto operator=(EventActionHandle &&) noexcept
      -> EventActionHandle & = default;
  auto installation() const -> const obcx::bot::BotInstallationRef &;

private:
  friend struct detail::StandardEventAccess;
  explicit EventActionHandle(
      std::shared_ptr<const detail::StandardEventContext>);
  std::shared_ptr<const detail::StandardEventContext> context_;
};

class StandardEventV1 final {
public:
  static constexpr std::uint32_t version = 1;
  auto observation() const -> const StandardEventObservation & {
    return observation_;
  }
  auto action_handle() const -> const std::optional<EventActionHandle> & {
    return handle_;
  }
  auto installation() const -> const obcx::bot::BotInstallationRef & {
    return installation_;
  }

private:
  friend struct detail::StandardEventAccess;
  StandardEventV1(obcx::bot::BotInstallationRef, StandardEventObservation,
                  std::optional<EventActionHandle>);
  obcx::bot::BotInstallationRef installation_;
  StandardEventObservation observation_;
  std::optional<EventActionHandle> handle_;
};

// Sibling delivery only: never publishes MessageStored or bridge events.
// Subscribers run synchronously on the configured executor and must not block.
class StandardEventCapabilityV1 final {
  struct Subscriber;

public:
  using Handler = std::function<void(std::shared_ptr<const StandardEventV1>)>;
  class Subscription final {
  public:
    Subscription() noexcept = default;
    Subscription(const Subscription &) = delete;
    auto operator=(const Subscription &) -> Subscription & = delete;
    Subscription(Subscription &&) noexcept;
    auto operator=(Subscription &&) noexcept -> Subscription &;
    ~Subscription();
    void reset() noexcept;

  private:
    friend class StandardEventCapabilityV1;
    explicit Subscription(std::shared_ptr<Subscriber>);
    std::shared_ptr<Subscriber> subscriber_;
  };
  StandardEventCapabilityV1(const StandardEventCapabilityV1 &) = delete;
  auto operator=(const StandardEventCapabilityV1 &)
      -> StandardEventCapabilityV1 & = delete;
  StandardEventCapabilityV1(StandardEventCapabilityV1 &&) = delete;
  auto operator=(StandardEventCapabilityV1 &&)
      -> StandardEventCapabilityV1 & = delete;
  ~StandardEventCapabilityV1();
  void subscribe(Handler);
  // Removal is terminal. An invocation already claimed may finish, but queued
  // snapshots contain only removable state, never independent handler copies.
  [[nodiscard]] auto subscribe_scoped(Handler) -> Subscription;
  void close() noexcept;

private:
  friend struct detail::StandardEventAccess;
  struct State;
  explicit StandardEventCapabilityV1(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
} // namespace obcx::onebot11::bot::standard
