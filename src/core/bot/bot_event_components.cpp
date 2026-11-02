#include "core/bot/bot_event_components.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <type_traits>
#include <utility>

namespace obcx::core {

BotEventCapability::BotEventCapability(boost::asio::any_io_executor executor,
                                       BotEventContext context)
    : executor_(std::move(executor)), context_(std::move(context)) {
  context_.surface.validate();
  if (context_.installation_id.empty() ||
      !bot::detail::valid_bot_id(context_.platform)) {
    throw BotComponentRuntimeError(
        "bot event capability requires an installation id");
  }
}

void BotEventCapability::set_origin_capture_handler(
    OriginCaptureHandler handler) {
  std::scoped_lock lock(mutex_);
  if (!handler || active() || origin_capture_handler_)
    throw BotComponentRuntimeError(
        "origin capture must be configured once before activation");
  origin_capture_handler_ = std::move(handler);
}
auto BotEventCapability::capture_origin() const
    -> std::optional<SelfIdentityOrigin> {
  OriginCaptureHandler handler;
  {
    std::scoped_lock lock(mutex_);
    handler = origin_capture_handler_;
  }
  // Never hold the capability lock while entering registry/cache authority.
  return handler ? handler() : std::nullopt;
}

void BotEventCapability::set_message_admission_handler(
    MessageAdmissionHandler handler) {
  std::scoped_lock lock(mutex_);
  if (!handler || active() || message_admission_handler_)
    throw BotComponentRuntimeError(
        "message admission must be configured once before activation");
  message_admission_handler_ = std::move(handler);
}

void BotEventCapability::subscribe_messages(MessageHandler handler) {
  if (!handler) {
    throw BotComponentRuntimeError("message event handler cannot be empty");
  }
  if (active()) {
    throw BotComponentRuntimeError(
        "message subscriptions must be installed before event activation");
  }
  std::scoped_lock lock(mutex_);
  message_handlers_.push_back(std::move(handler));
}

void BotEventCapability::subscribe_notices(NoticeHandler handler) {
  if (!handler) {
    throw BotComponentRuntimeError("notice event handler cannot be empty");
  }
  if (active()) {
    throw BotComponentRuntimeError(
        "notice subscriptions must be installed before event activation");
  }
  std::scoped_lock lock(mutex_);
  notice_handlers_.push_back(std::move(handler));
}

void BotEventCapability::subscribe_heartbeats(HeartbeatHandler handler) {
  if (!handler) {
    throw BotComponentRuntimeError("heartbeat event handler cannot be empty");
  }
  if (active()) {
    throw BotComponentRuntimeError(
        "heartbeat subscriptions must be installed before event activation");
  }
  std::scoped_lock lock(mutex_);
  heartbeat_handlers_.push_back(std::move(handler));
}

void BotEventCapability::activate() noexcept {
  active_.store(true, std::memory_order_release);
}

void BotEventCapability::close() noexcept {
  active_.store(false, std::memory_order_release);
}

auto BotEventCapability::publish(const common::Event &event,
                                 std::optional<SelfIdentityOrigin> origin) const
    -> SelfObservationResult {
  if (!active())
    return {SelfObservationStatus::Closed, std::nullopt};
  return std::visit(
      [this, &origin](const auto &typed_event) -> SelfObservationResult {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, common::MessageEvent>) {
          std::vector<MessageHandler> handlers;
          MessageAdmissionHandler admission_handler;
          {
            std::scoped_lock lock(mutex_);
            handlers = message_handlers_;
            admission_handler = message_admission_handler_;
          }
          if (!origin || handlers.empty() || !admission_handler)
            return {SelfObservationStatus::RejectedOrigin, std::nullopt};
          auto context = context_;
          context.source_origin = origin;
          MessageAdmissionResult result{
              {SelfObservationStatus::RejectedOrigin, std::nullopt},
              std::nullopt};
          try {
            result = admission_handler(context, typed_event);
          } catch (...) {
            return {SelfObservationStatus::InvalidIdentity, std::nullopt};
          }
          if (!result.observation.accepted() || !result.admission)
            return result.observation;
          context.message_admission = result.admission;
          // Observation may already have transitioned the account even if
          // event delivery closes now. Preserve that authoritative status.
          if (!active())
            return result.observation;
          for (const auto &handler : handlers) {
            try {
              boost::asio::co_spawn(
                  executor_,
                  [handler, context,
                   event = typed_event]() -> boost::asio::awaitable<void> {
                    co_await handler(context, event);
                  },
                  boost::asio::detached);
            } catch (...) {
              // Admission/transition already committed; delivery failure
              // cannot hide the status needed to retire old native origins.
            }
          }
          return result.observation;
        } else if constexpr (std::is_same_v<Event, common::NoticeEvent>) {
          auto context = context_;
          context.notice_received_at = bot::TimePoint{
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count()};
          std::vector<NoticeHandler> handlers;
          {
            std::scoped_lock lock(mutex_);
            handlers = notice_handlers_;
          }
          for (const auto &handler : handlers) {
            boost::asio::co_spawn(
                executor_,
                [handler, context,
                 event = typed_event]() -> boost::asio::awaitable<void> {
                  co_await handler(context, event);
                },
                boost::asio::detached);
          }
        } else if constexpr (std::is_same_v<Event, common::HeartbeatEvent>) {
          std::vector<HeartbeatHandler> handlers;
          {
            std::scoped_lock lock(mutex_);
            handlers = heartbeat_handlers_;
          }
          for (const auto &handler : handlers) {
            boost::asio::co_spawn(
                executor_,
                [handler, context = context_,
                 event = typed_event]() -> boost::asio::awaitable<void> {
                  co_await handler(context, event);
                },
                boost::asio::detached);
          }
        }
        // Non-message observations never grant identity authority.
        return {SelfObservationStatus::Accepted, std::nullopt};
      },
      event);
}

} // namespace obcx::core
