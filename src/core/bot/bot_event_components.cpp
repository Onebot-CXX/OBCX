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

void BotEventCapability::publish(const common::Event &event) const {
  if (!active()) {
    return;
  }
  std::visit(
      [this](const auto &typed_event) {
        using Event = std::decay_t<decltype(typed_event)>;
        if constexpr (std::is_same_v<Event, common::MessageEvent>) {
          std::vector<MessageHandler> handlers;
          {
            std::scoped_lock lock(mutex_);
            handlers = message_handlers_;
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
        } else if constexpr (std::is_same_v<Event, common::NoticeEvent>) {
          std::vector<NoticeHandler> handlers;
          {
            std::scoped_lock lock(mutex_);
            handlers = notice_handlers_;
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
      },
      event);
}

} // namespace obcx::core
