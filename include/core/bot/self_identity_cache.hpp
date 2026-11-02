#pragma once

// Process-only lifecycle/cache implementation. Actors use GetSelfRequest.
#include "core/bot/self_info.hpp"
#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace obcx::core {
class SelfIdentityCache;

// Process authority, never a public reference or a serialized generation.
class SelfIdentityOrigin final {
public:
  SelfIdentityOrigin(const SelfIdentityOrigin &) = default;
  SelfIdentityOrigin(SelfIdentityOrigin &&) = default;
  auto operator=(const SelfIdentityOrigin &) -> SelfIdentityOrigin & = default;
  auto operator=(SelfIdentityOrigin &&) -> SelfIdentityOrigin & = default;

private:
  friend class SelfIdentityCache;
  SelfIdentityOrigin(std::weak_ptr<const SelfIdentityCache> owner,
                     std::uint64_t epoch)
      : owner_(std::move(owner)), epoch_(epoch) {}
  std::weak_ptr<const SelfIdentityCache> owner_;
  std::uint64_t epoch_;
};

enum class SelfObservationStatus {
  Accepted,
  Transitioned,
  RejectedOrigin,
  Closed,
  InvalidIdentity
};

struct SelfObservationResult {
  SelfObservationStatus status;
  // Only for NEW originating work; never rebase data from an old callback.
  std::optional<SelfIdentityOrigin> current_origin;
  [[nodiscard]] auto accepted() const noexcept -> bool {
    return status == SelfObservationStatus::Accepted ||
           status == SelfObservationStatus::Transitioned;
  }
};

class SelfIdentityCache final
    : public std::enable_shared_from_this<SelfIdentityCache> {
public:
  using Result = bot::BotOperationResult<bot::SelfInfo>;
  using Loader =
      std::function<boost::asio::awaitable<Result>(SelfIdentityOrigin)>;
  SelfIdentityCache(boost::asio::any_io_executor, bot::BotInstallationRef,
                    std::chrono::milliseconds interval);
  void configure(Loader);
  // Install once before activation. Called synchronously under mutex_: must
  // not throw, query this cache, await, or acquire locks that can enter it.
  void set_generation_invalidation_handler(std::function<void()> handler);
  // No authority before connection establishment or during disconnection.
  [[nodiscard]] auto capture_origin() const
      -> std::optional<SelfIdentityOrigin>;
  // Atomic, non-mutating check of received-context identity. A profile is not
  // required, but the account must already be known in this original epoch.
  // False for unknown identity, retired origins, disconnection or closure.
  [[nodiscard]] auto matches_observed_user(const SelfIdentityOrigin &,
                                           const bot::UserRef &) const -> bool;
  // Check originating authority BEFORE observation and dependent capture in
  // one critical section. capture follows the non-reentrant lock rule above.
  // Transport locks, if needed, must already be held; never acquire them here.
  [[nodiscard]] auto observe_user_and_capture(
      const SelfIdentityOrigin &, std::optional<bot::UserRef> user,
      const std::function<void()> &capture) -> SelfObservationResult;
  [[nodiscard]] auto observe_user(const SelfIdentityOrigin &, bot::UserRef user)
      -> SelfObservationResult;
  [[nodiscard]] auto publish_self(const SelfIdentityOrigin &,
                                  const bot::SelfInfo &)
      -> SelfObservationResult;
  void connection_changed(bool connected);
  void refresh();
  void close() noexcept;
  [[nodiscard]] auto read() const -> Result;

private:
  void advance_generation_locked() noexcept;
  auto matches_origin_locked(const SelfIdentityOrigin &) const -> bool;
  auto observe_user_locked(const bot::UserRef *) -> bool;
  auto observation_locked(bool transitioned) const -> SelfObservationResult;
  void request_refresh();
  static auto run(std::shared_ptr<SelfIdentityCache>, SelfIdentityOrigin)
      -> boost::asio::awaitable<void>;
  void schedule();
  bot::BotInstallationRef installation_;
  std::chrono::milliseconds interval_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;
  boost::asio::steady_timer timer_;
  Loader loader_;
  std::function<void()> generation_invalidation_handler_;
  mutable std::mutex mutex_;
  std::optional<bot::SelfInfo> snapshot_;
  enum class State { Unavailable, Refreshing, Ready };
  State state_{State::Unavailable};
  bool lifecycle_started_{}; // mutex_-protected; forbids late hook attachment
  bool closed_{}; // protected by mutex_, as are snapshot, state and expected ID
  // Connection eligibility changes synchronously with epoch under mutex_.
  bool connected_{};
  // Scheduling fields are strand-owned; epoch also changes at ingress fences.
  bool inflight_{};
  bool refresh_pending_{};
  std::atomic_uint64_t epoch_{};
  std::atomic_uint64_t connection_sequence_{};
  std::uint64_t inflight_epoch_{};
  std::optional<bot::UserId> expected_self_id_;
};
} // namespace obcx::core
