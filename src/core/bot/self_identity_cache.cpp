#include "core/bot/self_identity_cache.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <common/logger.hpp>

namespace obcx::core {
SelfIdentityCache::SelfIdentityCache(boost::asio::any_io_executor executor,
                                     bot::BotInstallationRef installation,
                                     std::chrono::milliseconds interval)
    : installation_(std::move(installation)), interval_(interval),
      strand_(boost::asio::make_strand(std::move(executor))), timer_(strand_) {
  installation_.validate();
  if (interval_.count() <= 0)
    throw std::invalid_argument(
        "self identity refresh interval must be positive");
}
void SelfIdentityCache::configure(Loader loader) {
  if (!loader || loader_)
    throw std::logic_error("self identity cache requires one loader");
  loader_ = std::move(loader);
}
void SelfIdentityCache::set_generation_invalidation_handler(
    std::function<void()> handler) {
  std::lock_guard lock(mutex_);
  if (!handler || generation_invalidation_handler_ || lifecycle_started_ ||
      closed_)
    throw std::logic_error(
        "self generation hook must be installed once before activation");
  generation_invalidation_handler_ = std::move(handler);
}
void SelfIdentityCache::advance_generation_locked() noexcept {
  ++epoch_;
  if (generation_invalidation_handler_)
    generation_invalidation_handler_();
}
auto SelfIdentityCache::read() const -> Result {
  std::lock_guard lock(mutex_);
  if (!closed_ && connected_ && snapshot_)
    return Result::success(*snapshot_);
  return Result::failure(
      {.code = bot::BotOperationErrorCode::CacheUnavailable,
       .message = state_ == State::Refreshing
                      ? "self identity cache is refreshing without a snapshot"
                      : "self identity cache is unavailable",
       .retryable = false,
       .submission_safety = bot::SubmissionSafety::DefinitelyNotSubmitted});
}
void SelfIdentityCache::connection_changed(bool connected) {
  // Fence readers/publications immediately, before the scheduling callback can
  // run on another executor. A disconnected identity must not leak to actors.
  std::uint64_t sequence;
  {
    std::lock_guard lock(mutex_);
    if (closed_)
      return;
    lifecycle_started_ = true;
    connected_ = connected;
    advance_generation_locked();
    sequence = ++connection_sequence_;
    expected_self_id_.reset();
    snapshot_.reset();
    state_ = State::Unavailable;
  }
  boost::asio::post(strand_, [self = shared_from_this(), connected, sequence] {
    if (sequence != self->connection_sequence_)
      return;
    self->timer_.cancel();
    if (connected)
      self->request_refresh();
  });
}
void SelfIdentityCache::refresh() {
  {
    std::lock_guard lock(mutex_);
    if (closed_ || !connected_)
      return;
    lifecycle_started_ = true;
  }
  boost::asio::post(strand_,
                    [self = shared_from_this()] { self->request_refresh(); });
}
auto SelfIdentityCache::capture_origin() const
    -> std::optional<SelfIdentityOrigin> {
  std::lock_guard lock(mutex_);
  if (closed_ || !connected_ || weak_from_this().expired())
    return std::nullopt;
  return SelfIdentityOrigin{weak_from_this(), epoch_.load()};
}
auto SelfIdentityCache::matches_observed_user(const SelfIdentityOrigin &origin,
                                              const bot::UserRef &user) const
    -> bool {
  try {
    user.validate();
  } catch (...) {
    return false;
  }
  std::lock_guard lock(mutex_);
  return !closed_ && matches_origin_locked(origin) &&
         user.installation == installation_ && expected_self_id_ &&
         user.id == *expected_self_id_;
}
auto SelfIdentityCache::matches_origin_locked(
    const SelfIdentityOrigin &origin) const -> bool {
  return connected_ && origin.owner_.lock().get() == this &&
         origin.epoch_ == epoch_;
}
auto SelfIdentityCache::observation_locked(bool transitioned) const
    -> SelfObservationResult {
  return {transitioned ? SelfObservationStatus::Transitioned
                       : SelfObservationStatus::Accepted,
          SelfIdentityOrigin{weak_from_this(), epoch_.load()}};
}
auto SelfIdentityCache::observe_user_locked(const bot::UserRef *user) -> bool {
  if (!user)
    return false;
  const bool conflict = (expected_self_id_ && *expected_self_id_ != user->id) ||
                        (snapshot_ && snapshot_->user != *user);
  expected_self_id_ = user->id;
  if (conflict) {
    advance_generation_locked();
    snapshot_.reset();
    state_ = State::Unavailable;
  }
  return conflict;
}
auto SelfIdentityCache::observe_user(const SelfIdentityOrigin &origin,
                                     bot::UserRef user)
    -> SelfObservationResult {
  return observe_user_and_capture(origin, std::move(user), [] {});
}
auto SelfIdentityCache::observe_user_and_capture(
    const SelfIdentityOrigin &origin, std::optional<bot::UserRef> user,
    const std::function<void()> &capture) -> SelfObservationResult {
  if (!capture)
    throw std::invalid_argument("self observation requires a capture callback");
  SelfObservationResult result{SelfObservationStatus::RejectedOrigin,
                               std::nullopt};
  {
    std::lock_guard lock(mutex_);
    if (closed_)
      return {SelfObservationStatus::Closed, std::nullopt};
    if (!matches_origin_locked(origin))
      return result;
    if (user) {
      try {
        user->validate();
      } catch (...) {
        return {SelfObservationStatus::InvalidIdentity, std::nullopt};
      }
      if (user->installation != installation_)
        return {SelfObservationStatus::InvalidIdentity, std::nullopt};
    }
    lifecycle_started_ = true;
    const auto transitioned = observe_user_locked(user ? &*user : nullptr);
    capture();
    result = observation_locked(transitioned);
  }
  if (result.status == SelfObservationStatus::Transitioned) {
    try {
      refresh();
    } catch (...) {
      // The transition already committed. Scheduling failure must not hide
      // its status from the owner that must retire old transport work.
    }
  }
  return result;
}
auto SelfIdentityCache::publish_self(const SelfIdentityOrigin &origin,
                                     const bot::SelfInfo &info)
    -> SelfObservationResult {
  std::lock_guard lock(mutex_);
  if (closed_)
    return {SelfObservationStatus::Closed, std::nullopt};
  if (!matches_origin_locked(origin))
    return {SelfObservationStatus::RejectedOrigin, std::nullopt};
  try {
    info.validate();
  } catch (...) {
    return {SelfObservationStatus::InvalidIdentity, std::nullopt};
  }
  if (info.user.installation != installation_)
    return {SelfObservationStatus::InvalidIdentity, std::nullopt};
  // Prepare the validated profile before changing expected identity. A copy
  // failure cannot leave a half-published account transition.
  auto snapshot = info;
  lifecycle_started_ = true;
  const auto transitioned = observe_user_locked(&info.user);
  snapshot_ = std::move(snapshot);
  state_ = State::Ready;
  return observation_locked(transitioned);
}
void SelfIdentityCache::request_refresh() {
  std::optional<SelfIdentityOrigin> origin;
  {
    std::lock_guard lock(mutex_);
    if (closed_ || !connected_ || !loader_)
      return;
    if (inflight_) {
      refresh_pending_ = inflight_epoch_ != epoch_;
      return;
    }
    origin = SelfIdentityOrigin{weak_from_this(), epoch_.load()};
    refresh_pending_ = false;
    inflight_ = true;
    inflight_epoch_ = epoch_.load();
    state_ = State::Refreshing;
  }
  timer_.cancel();
  boost::asio::co_spawn(
      strand_, run(shared_from_this(), std::move(*origin)),
      [self = shared_from_this()](std::exception_ptr error) {
        self->inflight_ = false;
        {
          std::lock_guard lock(self->mutex_);
          self->state_ = self->snapshot_ ? State::Ready : State::Unavailable;
        }
        if (error)
          OBCX_WARN(
              "Self identity refresh failed; waiting for configured retry");
        if (self->refresh_pending_)
          self->request_refresh();
        else
          self->schedule();
      });
}
auto SelfIdentityCache::run(std::shared_ptr<SelfIdentityCache> self,
                            SelfIdentityOrigin origin)
    -> boost::asio::awaitable<void> {
  // origin was captured before co_spawn, not when this coroutine resumes.
  {
    std::lock_guard lock(self->mutex_);
    if (self->closed_ || !self->matches_origin_locked(origin))
      co_return;
  }
  const auto result = co_await self->loader_(origin);
  {
    std::lock_guard lock(self->mutex_);
    // Disconnection is synchronous, even if its timer callback is queued.
    // A loader that published a transition also retires this original token.
    if (self->closed_ || !self->connected_ ||
        !self->matches_origin_locked(origin))
      co_return;
  }
  if (!result.ok()) {
    OBCX_WARN("Self identity refresh unavailable; retaining same-generation "
              "snapshot");
    co_return;
  }
  // Native loaders may already have atomically published a transition and
  // retired their transport origins. Never rebase this old completion token.
  (void)self->publish_self(origin, *result.value);
}
void SelfIdentityCache::schedule() {
  {
    std::lock_guard lock(mutex_);
    if (closed_ || !connected_)
      return;
  }
  timer_.expires_after(interval_);
  timer_.async_wait(
      [self = shared_from_this()](const boost::system::error_code &error) {
        if (!error)
          self->request_refresh();
      });
}
void SelfIdentityCache::close() noexcept {
  {
    std::lock_guard lock(mutex_);
    if (closed_)
      return;
    closed_ = true;
    connected_ = false;
    advance_generation_locked();
    snapshot_.reset();
    state_ = State::Unavailable;
  }
  try {
    boost::asio::post(strand_,
                      [self = shared_from_this()] { self->timer_.cancel(); });
  } catch (...) {
    // Public reads are already closed; an admitted refresh cannot publish.
  }
}
} // namespace obcx::core
