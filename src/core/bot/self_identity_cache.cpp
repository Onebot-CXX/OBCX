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
auto SelfIdentityCache::read() const -> Result {
  std::lock_guard lock(mutex_);
  if (!closed_ && snapshot_)
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
    ++epoch_;
    sequence = ++connection_sequence_;
    expected_self_id_.clear();
    snapshot_.reset();
    state_ = State::Unavailable;
  }
  boost::asio::post(strand_, [self = shared_from_this(), connected, sequence] {
    if (sequence != self->connection_sequence_)
      return;
    self->connected_ = connected;
    self->timer_.cancel();
    if (connected)
      self->request_refresh();
  });
}
void SelfIdentityCache::refresh() {
  boost::asio::post(strand_,
                    [self = shared_from_this()] { self->request_refresh(); });
}
void SelfIdentityCache::observe_self_id(std::string id) {
  if (id.empty() || id.size() > 20 || id.front() == '0' ||
      !std::ranges::all_of(id, [](char c) { return c >= '0' && c <= '9'; }))
    return;
  bool conflict;
  {
    std::lock_guard lock(mutex_);
    if (closed_ || expected_self_id_ == id)
      return;
    conflict = (!expected_self_id_.empty() && expected_self_id_ != id) ||
               (snapshot_ && snapshot_->user_id != id);
    expected_self_id_ = std::move(id);
    if (conflict) {
      ++epoch_;
      snapshot_.reset();
      state_ = State::Unavailable;
    }
  }
  if (conflict)
    refresh();
}
void SelfIdentityCache::request_refresh() {
  {
    std::lock_guard lock(mutex_);
    if (closed_ || !connected_ || !loader_)
      return;
  }
  timer_.cancel();
  if (inflight_) {
    refresh_pending_ = inflight_epoch_ != epoch_;
    return;
  }
  refresh_pending_ = false;
  inflight_ = true;
  inflight_epoch_ = epoch_.load();
  {
    std::lock_guard lock(mutex_);
    state_ = State::Refreshing;
  }
  boost::asio::co_spawn(
      strand_, run(shared_from_this(), inflight_epoch_),
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
                            std::uint64_t epoch)
    -> boost::asio::awaitable<void> {
  const auto result = co_await self->loader_();
  if (epoch != self->epoch_ || !self->connected_)
    co_return;
  if (!result.ok()) {
    OBCX_WARN("Self identity refresh unavailable; retaining same-generation "
              "snapshot");
    co_return;
  }
  result.value->validate();
  std::lock_guard lock(self->mutex_);
  if (self->closed_ || epoch != self->epoch_)
    co_return;
  if (result.value->installation != self->installation_ ||
      (!self->expected_self_id_.empty() &&
       result.value->user_id != self->expected_self_id_)) {
    ++self->epoch_;
    self->snapshot_.reset();
    co_return;
  }
  self->snapshot_ = *result.value;
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
    closed_ = true;
    ++epoch_;
    snapshot_.reset();
    state_ = State::Unavailable;
  }
  try {
    boost::asio::post(strand_, [self = shared_from_this()] {
      self->connected_ = false;
      self->timer_.cancel();
    });
  } catch (...) {
    // Public reads are already closed; an admitted refresh cannot publish.
  }
}
} // namespace obcx::core
