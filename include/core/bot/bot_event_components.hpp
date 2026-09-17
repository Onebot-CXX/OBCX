#ifndef OBCX_INCLUDE_CORE_BOT_EVENT_COMPONENTS_HPP_
#define OBCX_INCLUDE_CORE_BOT_EVENT_COMPONENTS_HPP_

#include "common/message_type.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/ids.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace obcx::core {

struct BotEventContext {
  std::string installation_id;
  bot::SurfaceId surface;
  std::string platform;
};

class BotEventCapability {
public:
  using MessageHandler = std::function<boost::asio::awaitable<void>(
      const BotEventContext &, const common::MessageEvent &)>;
  using NoticeHandler = std::function<boost::asio::awaitable<void>(
      const BotEventContext &, const common::NoticeEvent &)>;
  using HeartbeatHandler = std::function<boost::asio::awaitable<void>(
      const BotEventContext &, const common::HeartbeatEvent &)>;

  BotEventCapability(boost::asio::any_io_executor executor,
                     BotEventContext context);

  void subscribe_messages(MessageHandler handler);
  void subscribe_notices(NoticeHandler handler);
  void subscribe_heartbeats(HeartbeatHandler handler);
  void activate() noexcept;
  void close() noexcept;
  void publish(const common::Event &event) const;

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
  std::vector<MessageHandler> message_handlers_;
  std::vector<NoticeHandler> notice_handlers_;
  std::vector<HeartbeatHandler> heartbeat_handlers_;
  std::atomic_bool active_{};
};

} // namespace obcx::core

#endif // OBCX_INCLUDE_CORE_BOT_EVENT_COMPONENTS_HPP_
