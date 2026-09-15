#include "core/actor/actor.hpp"
#include "core/bot/messaging.hpp"
#include "onebot11/bot/command_adapter.hpp"
#include "telegram/bot/command_adapter.hpp"
#include "telegram/bot/operations.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include <stdexcept>

namespace obcx::core {
namespace {

class CapturingTelegramCatalog final : public CommandCatalogPublisher {
public:
  auto publish(const std::vector<CommandCatalogEntry> &entries)
      -> boost::asio::awaitable<CommandCatalogPublishResult> override {
    calls.push_back(entries);
    if (fail) {
      throw std::runtime_error{"catalog failure"};
    }
    co_return CommandCatalogPublishResult{.supported = true, .succeeded = true};
  }

  std::vector<std::vector<CommandCatalogEntry>> calls;
  bool fail = false;
};

template <typename T>
auto run_awaitable(boost::asio::awaitable<T> operation) -> T {
  boost::asio::io_context io;
  auto future =
      boost::asio::co_spawn(io, std::move(operation), boost::asio::use_future);
  io.run();
  return future.get();
}

auto telegram_event(std::string text, common::json entities)
    -> MessageEnvelope {
  MessageEnvelope event;
  event.type = "obcx::core::events::RawMessageEvent";
  event.source_platform = "telegram";
  event.source_bot = "telegram_bot";
  event.raw = {
      {"text", std::move(text)},
      {"entities", std::move(entities)},
  };
  return event;
}

auto qq_event(std::string text) -> MessageEnvelope {
  MessageEnvelope event;
  event.type = "obcx::core::events::RawMessageEvent";
  event.source_platform = "qq";
  event.source_bot = "qq_bot";
  event.raw = {{"raw_message", std::move(text)}};
  return event;
}

TEST(CommandPlatformAdapterTest, TelegramUsesEntityAndExactBotTarget) {
  const auto adapter = obcx::telegram::bot::make_command_adapter("my_bot");
  ASSERT_NE(adapter, nullptr);
  auto event = telegram_event(
      "/chat@my_bot hello",
      common::json::array(
          {{{"type", "bot_command"}, {"offset", 0}, {"length", 12}}}));
  const auto detected = adapter->detect(event);
  ASSERT_TRUE(detected.has_value());
  EXPECT_EQ(detected->name, "chat");
  EXPECT_EQ(detected->arguments, "hello");
  EXPECT_FALSE(obcx::telegram::bot::make_command_adapter("other_bot")
                   ->detect(event)
                   .has_value());
}

TEST(CommandPlatformAdapterTest, TelegramRejectsPrefixAndNonCommandEntity) {
  const auto adapter = obcx::telegram::bot::make_command_adapter("my_bot");
  auto event = telegram_event(
      "/recallxxx",
      common::json::array(
          {{{"type", "bot_command"}, {"offset", 0}, {"length", 10}}}));
  const auto detected = adapter->detect(event);
  ASSERT_TRUE(detected.has_value());
  EXPECT_EQ(detected->name, "recallxxx");
  EXPECT_NE(detected->name, "recall");

  event.raw["entities"][0]["type"] = "mention";
  EXPECT_FALSE(adapter->detect(event).has_value());
}

TEST(CommandPlatformAdapterTest, QqUsesLeadingExactToken) {
  const auto adapter = obcx::onebot11::bot::make_command_adapter();
  ASSERT_NE(adapter, nullptr);
  const auto detected = adapter->detect(qq_event("/checkalive now"));
  ASSERT_TRUE(detected.has_value());
  EXPECT_EQ(detected->name, "checkalive");
  EXPECT_EQ(detected->arguments, "now");
  EXPECT_FALSE(adapter->detect(qq_event("prefix /checkalive")).has_value());
}

TEST(CommandPlatformAdapterTest,
     QqProducesBoundedNormalizedNonCanonicalCandidates) {
  const auto adapter = obcx::onebot11::bot::make_command_adapter();
  ASSERT_NE(adapter, nullptr);
  const auto localized = adapter->detect(qq_event("/戳一下 target"));
  ASSERT_TRUE(localized.has_value());
  EXPECT_EQ(localized->name, "戳一下");
  EXPECT_EQ(localized->arguments, "target");

  const auto punctuation = adapter->detect(qq_event("/poke-user now"));
  ASSERT_TRUE(punctuation.has_value());
  EXPECT_EQ(punctuation->name, "poke-user");

  EXPECT_FALSE(
      adapter->detect(qq_event("/" + std::string(257, 'x'))).has_value());
}

TEST(CommandPlatformAdapterTest,
     BuildsExactTelegramGroupTopicAndPrivateReplyOperations) {
  const auto adapter = obcx::telegram::bot::make_command_adapter("my_bot");
  auto event = telegram_event("/help", common::json::array());
  event.conversation_id = "chat:-1001";
  event.payload = {{"sender", "7"},
                   {"group_id", "-1001"},
                   {"chat_id", "-1001"},
                   {"message_type", "group"}};
  auto reply = adapter->build_text_reply(event, "group help");
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply.operation->action,
            obcx::bot::SendGroupMessageRequest::action);
  EXPECT_EQ(reply.operation->installation.installation_id, "telegram_bot");
  EXPECT_EQ(reply.operation->payload.at("target").at("native_group_id"),
            "-1001");

  event.payload["topic_id"] = 42;
  reply = adapter->build_text_reply(event, "topic help");
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply.operation->action,
            obcx::telegram::bot::SendTelegramTopicMessageRequest::action);
  EXPECT_EQ(reply.operation->payload.at("target").at("topic_id"), 42);

  event.conversation_id = "chat:7";
  event.payload = {{"sender", "7"},
                   {"group_id", ""},
                   {"chat_id", "7"},
                   {"message_type", "private"}};
  reply = adapter->build_text_reply(event, "private help");
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply.operation->action,
            obcx::bot::SendPrivateMessageRequest::action);
  EXPECT_EQ(reply.operation->payload.at("target").at("native_user_id"), "7");

  event.payload["chat_id"] = "8";
  EXPECT_FALSE(static_cast<bool>(adapter->build_text_reply(event, "invalid")));
}

TEST(CommandPlatformAdapterTest, BuildsExactOneBotGroupAndPrivateReplies) {
  const auto adapter = obcx::onebot11::bot::make_command_adapter();
  auto event = qq_event("/help");
  event.conversation_id = "group:42";
  event.payload = {
      {"sender", "7"}, {"group_id", "42"}, {"message_type", "group"}};
  auto reply = adapter->build_text_reply(event, "group help");
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply.operation->action,
            obcx::bot::SendGroupMessageRequest::action);
  EXPECT_EQ(reply.operation->installation.installation_id, "qq_bot");

  event.conversation_id = "private:7";
  event.payload = {
      {"sender", "7"}, {"group_id", ""}, {"message_type", "private"}};
  reply = adapter->build_text_reply(event, "private help");
  ASSERT_TRUE(reply);
  EXPECT_EQ(reply.operation->action,
            obcx::bot::SendPrivateMessageRequest::action);

  event.source_bot.clear();
  EXPECT_FALSE(static_cast<bool>(adapter->build_text_reply(event, "invalid")));
}

TEST(CommandPlatformAdapterTest, CatalogCapabilitiesArePlatformSpecific) {
  const auto telegram = obcx::telegram::bot::make_command_adapter("my_bot");
  const auto qq = obcx::onebot11::bot::make_command_adapter();
  ASSERT_NE(telegram, nullptr);
  ASSERT_NE(qq, nullptr);
  EXPECT_TRUE(telegram->supports_catalog_publication());
  EXPECT_FALSE(qq->supports_catalog_publication());
  EXPECT_FALSE(telegram
                   ->validate_catalog({CommandCatalogEntry{
                       .name = "chat", .description = "Chat"}})
                   .has_value());
  EXPECT_TRUE(telegram
                  ->validate_catalog({CommandCatalogEntry{
                      .name = "Bad!", .description = "Bad"}})
                  .has_value());
  EXPECT_EQ(telegram->platform(), "telegram");
  EXPECT_EQ(qq->platform(), "qq");
}

TEST(CommandPlatformAdapterTest, TelegramPublishesOneCompleteReplacementList) {
  auto adapter = obcx::telegram::bot::make_command_adapter("my_bot");
  CapturingTelegramCatalog catalog_capability;
  const std::vector catalog = {
      CommandCatalogEntry{.name = "chat", .description = "Chat"},
      CommandCatalogEntry{.name = "toggle_think",
                          .description = "Toggle thinking"},
  };

  auto result =
      run_awaitable(adapter->publish_catalog(&catalog_capability, catalog));
  EXPECT_TRUE(result.supported);
  EXPECT_TRUE(result.succeeded);
  ASSERT_EQ(catalog_capability.calls.size(), 1U);
  EXPECT_EQ(catalog_capability.calls.front(), catalog);

  catalog_capability.fail = true;
  result =
      run_awaitable(adapter->publish_catalog(&catalog_capability, catalog));
  EXPECT_FALSE(result.succeeded);
  EXPECT_EQ(result.code, "command_catalog_publish_failed");
}

} // namespace
} // namespace obcx::core
