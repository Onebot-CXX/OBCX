#pragma once

#include "common/message_type.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/domain_events.hpp"
#include "core/bot/self_identity_cache.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace obcx::core {

struct MessageIngressAdmission {
  std::uint64_t evidence_generation;
  bot::TimePoint received_at;
};

struct MessageAdmissionResult {
  SelfObservationResult observation;
  std::optional<MessageIngressAdmission> admission;
};

struct BotEventContext {
  std::string installation_id;
  bot::SurfaceId surface;
  std::string platform;
  std::optional<MessageIngressAdmission> message_admission;
  std::optional<SelfIdentityOrigin> source_origin;
  // Captured before notice delivery is queued; grants no identity admission.
  std::optional<bot::TimePoint> notice_received_at;
};

class BotEventCapability {
public:
  using OriginCaptureHandler =
      std::function<std::optional<SelfIdentityOrigin>()>;
  using MessageAdmissionHandler = std::function<MessageAdmissionResult(
      const BotEventContext &, const common::MessageEvent &)>;
  using MessageHandler = std::function<boost::asio::awaitable<void>(
      const BotEventContext &, const common::MessageEvent &)>;
  using NoticeHandler = std::function<boost::asio::awaitable<void>(
      const BotEventContext &, const common::NoticeEvent &)>;
  using HeartbeatHandler = std::function<boost::asio::awaitable<void>(
      const BotEventContext &, const common::HeartbeatEvent &)>;

  BotEventCapability(boost::asio::any_io_executor executor,
                     BotEventContext context);

  void set_origin_capture_handler(OriginCaptureHandler handler);
  [[nodiscard]] auto capture_origin() const
      -> std::optional<SelfIdentityOrigin>;
  void set_message_admission_handler(MessageAdmissionHandler handler);
  void subscribe_messages(MessageHandler handler);
  void subscribe_notices(NoticeHandler handler);
  void subscribe_heartbeats(HeartbeatHandler handler);
  void activate() noexcept;
  void close() noexcept;
  // Origin is explicit. Unowned non-message observations grant no admission;
  // messages always require an original captured origin.
  [[nodiscard]] auto publish(const common::Event &event,
                             std::optional<SelfIdentityOrigin> origin) const
      -> SelfObservationResult;

  [[nodiscard]] auto context() const -> const BotEventContext & {
    return context_;
  }
  [[nodiscard]] auto active() const noexcept -> bool {
    return active_.load(std::memory_order_acquire);
  }

private:
  boost::asio::any_io_executor executor_;
  BotEventContext context_;
  mutable std::mutex mutex_;
  OriginCaptureHandler origin_capture_handler_;
  MessageAdmissionHandler message_admission_handler_;
  std::vector<MessageHandler> message_handlers_;
  std::vector<NoticeHandler> notice_handlers_;
  std::vector<HeartbeatHandler> heartbeat_handlers_;
  std::atomic_bool active_{};
};

} // namespace obcx::core
