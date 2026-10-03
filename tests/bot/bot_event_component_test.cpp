#include "core/bot/bot_event_components.hpp"

#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

TEST(BotEventComponentTest,
     PublishesExactInstallationIdentityAndOnlySupportedIngressTypes) {
  boost::asio::io_context io;
  obcx::core::BotEventCapability events{
      io.get_executor(),
      {.installation_id = "telegram-secondary",
       .surface = obcx::bot::SurfaceId{"telegram.bot_api"},
       .platform = "telegram"}};
  std::vector<std::string> observed;
  events.subscribe_messages(
      [&observed](const obcx::core::BotEventContext &context,
                  const obcx::common::MessageEvent &event)
          -> boost::asio::awaitable<void> {
        observed.push_back(context.installation_id +
                           ":message:" + event.message_id);
        EXPECT_EQ(context.surface, obcx::bot::SurfaceId{"telegram.bot_api"});
        co_return;
      });
  events.subscribe_notices(
      [&observed](const obcx::core::BotEventContext &context,
                  const obcx::common::NoticeEvent &event)
          -> boost::asio::awaitable<void> {
        observed.push_back(context.installation_id +
                           ":notice:" + event.notice_type);
        co_return;
      });
  events.subscribe_heartbeats(
      [&observed](const obcx::core::BotEventContext &context,
                  const obcx::common::HeartbeatEvent &event)
          -> boost::asio::awaitable<void> {
        observed.push_back(context.installation_id +
                           ":heartbeat:" + std::to_string(event.interval));
        co_return;
      });
  events.activate();
  EXPECT_THROW(events.subscribe_messages({}),
               obcx::core::BotComponentRuntimeError);

  obcx::common::MessageEvent message;
  message.message_id = "42";
  obcx::common::NoticeEvent notice;
  notice.notice_type = "member_join";
  obcx::common::HeartbeatEvent heartbeat;
  heartbeat.interval = 30000;
  events.publish(message);
  events.publish(notice);
  events.publish(heartbeat);
  events.publish(obcx::common::RequestEvent{});
  io.run();
  EXPECT_EQ(observed,
            (std::vector<std::string>{"telegram-secondary:message:42",
                                      "telegram-secondary:notice:member_join",
                                      "telegram-secondary:heartbeat:30000"}));

  events.close();
  io.restart();
  message.message_id = "43";
  events.publish(message);
  io.run();
  EXPECT_EQ(observed.size(), 3U);
}

} // namespace
