#pragma once

// Process-only lifecycle/cache implementation. Actors use GetSelfRequest.
#include "core/bot/self_info.hpp"
#include <atomic>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace obcx::core {
class SelfIdentityCache final
    : public std::enable_shared_from_this<SelfIdentityCache> {
public:
  using Result = bot::BotOperationResult<bot::SelfInfo>;
  using Loader = std::function<boost::asio::awaitable<Result>()>;
  SelfIdentityCache(boost::asio::any_io_executor, bot::BotInstallationRef,
                    std::chrono::milliseconds interval);
  void configure(Loader);
  void connection_changed(bool connected);
  void refresh();
  void observe_self_id(std::string id);
  void close() noexcept;
  [[nodiscard]] auto read() const -> Result;

private:
  void request_refresh();
  static auto run(std::shared_ptr<SelfIdentityCache>, std::uint64_t epoch)
      -> boost::asio::awaitable<void>;
  void schedule();
  bot::BotInstallationRef installation_;
  std::chrono::milliseconds interval_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;
  boost::asio::steady_timer timer_;
  Loader loader_;
  mutable std::mutex mutex_;
  std::optional<bot::SelfInfo> snapshot_;
  enum class State { Unavailable, Refreshing, Ready };
  State state_{State::Unavailable};
  bool closed_{}; // protected by mutex_, as are snapshot, state and expected ID
  // Scheduling fields are strand-owned; epoch also changes at ingress fences.
  bool connected_{};
  bool inflight_{};
  bool refresh_pending_{};
  std::atomic_uint64_t epoch_{};
  std::atomic_uint64_t connection_sequence_{};
  std::uint64_t inflight_epoch_{};
  std::string expected_self_id_;
};
} // namespace obcx::core
