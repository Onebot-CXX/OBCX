#include "core/runtime/message_event_ingress.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace obcx::core {
namespace {

auto qq_message_event() -> common::MessageEvent {
  common::MessageEvent event;
  event.type = common::EventType::message;
  event.time = std::chrono::system_clock::time_point{
      std::chrono::milliseconds{1720000000123}};
  event.self_id = "qq-main";
  event.post_type = "message";
  event.message_type = "group";
  event.sub_type = "normal";
  event.message_id = "qq-101";
  event.user_id = "user-7";
  event.group_id = "group-3";
  event.raw_message = "hello actor";
  common::MessageSegment text;
  text.type = "text";
  text.data["text"] = "hello actor";
  event.message.push_back(std::move(text));
  event.data = {{"message_id", 101},
                {"message_type", "group"},
                {"user_id", 7},
                {"group_id", 3},
                {"raw_message", "hello actor"}};
  return event;
}

auto qq_poke_notice_event() -> common::NoticeEvent {
  common::NoticeEvent event;
  event.type = common::EventType::notice;
  event.time = std::chrono::system_clock::time_point{
      std::chrono::milliseconds{1720000000456}};
  event.self_id = "qq-main";
  event.post_type = "notice";
  event.notice_type = "notify";
  event.user_id = "user-7";
  event.group_id = "group-3";
  event.data = {
      {"post_type", "notice"}, {"notice_type", "notify"}, {"sub_type", "poke"},
      {"self_id", 90001},      {"user_id", 70007},        {"target_id", 80008},
      {"group_id", 30003},
  };
  return event;
}

} // namespace

TEST(MessageEventIngressTest, BuildsRawMessageEnvelopeFromMessageEvent) {
  const auto envelope =
      raw_message_envelope_from_event("qq", "qq-main", qq_message_event());

  EXPECT_EQ(envelope.type, "obcx::core::events::RawMessageEvent");
  EXPECT_EQ(envelope.source_platform, "qq");
  EXPECT_EQ(envelope.source_bot, "qq-main");
  EXPECT_EQ(envelope.conversation_id, "group:group-3");
  EXPECT_EQ(envelope.id, "raw:qq:qq-main:group:group-3:qq-101");
  EXPECT_EQ(envelope.payload["message_id"], "qq-101");
  EXPECT_EQ(envelope.payload["conversation_id"], "group:group-3");
  EXPECT_EQ(envelope.payload["source_bot_configured"], true);
  EXPECT_EQ(envelope.payload["sender"], "user-7");
  EXPECT_EQ(envelope.payload["group_id"], "group-3");
  EXPECT_EQ(envelope.payload["chat_id"], "");
  EXPECT_EQ(envelope.payload["message_type"], "group");
  ASSERT_TRUE(envelope.payload["payload"].contains("message"));
  EXPECT_EQ(envelope.payload["payload"]["raw_message"], "hello actor");
  EXPECT_EQ(envelope.raw["message_id"], "qq-101");
  EXPECT_EQ(envelope.raw["message"][0]["data"]["text"], "hello actor");
}

TEST(MessageEventIngressTest, NormalizesTelegramChatTopicAndPrivateIdentity) {
  auto event = qq_message_event();
  event.self_id = "0";
  event.message_id = "42";
  event.user_id = "7";
  event.group_id = "-1001";
  event.message_type = "group";
  event.data = {{"chat", {{"id", -1001}, {"type", "supergroup"}}},
                {"message_thread_id", 99}};
  auto envelope =
      raw_message_envelope_from_event("telegram", "telegram-main", event);
  EXPECT_EQ(envelope.source_bot, "telegram-main");
  EXPECT_EQ(envelope.conversation_id, "chat:-1001");
  EXPECT_EQ(envelope.payload["sender"], "7");
  EXPECT_EQ(envelope.payload["group_id"], "-1001");
  EXPECT_EQ(envelope.payload["chat_id"], "-1001");
  EXPECT_EQ(envelope.payload["topic_id"], 99);

  event.data["message_thread_id"] = "invalid";
  envelope =
      raw_message_envelope_from_event("telegram", "telegram-main", event);
  EXPECT_EQ(envelope.payload["topic_id"], 0);

  event.group_id.reset();
  event.message_type = "private";
  event.data = {{"chat", {{"id", 7}, {"type", "private"}}}};
  envelope =
      raw_message_envelope_from_event("telegram", "telegram-main", event);
  EXPECT_EQ(envelope.conversation_id, "chat:7");
  EXPECT_EQ(envelope.payload["group_id"], "");
  EXPECT_EQ(envelope.payload["chat_id"], "7");
  EXPECT_FALSE(envelope.payload.contains("topic_id"));
}

TEST(MessageEventIngressTest, UsesEventSelfIdWhenSourceBotIsEmpty) {
  const auto envelope =
      raw_message_envelope_from_event("qq", "", qq_message_event());

  EXPECT_EQ(envelope.source_bot, "qq-main");
  EXPECT_EQ(envelope.payload["source_bot_configured"], false);
}

TEST(MessageEventIngressTest, SeparatesEqualMessageIdsAcrossConversations) {
  auto first_event = qq_message_event();
  auto second_event = qq_message_event();
  second_event.group_id = "group-4";

  const auto first =
      raw_message_envelope_from_event("qq", "qq-main", first_event);
  const auto second =
      raw_message_envelope_from_event("qq", "qq-main", second_event);

  EXPECT_NE(first.id, second.id);
  EXPECT_EQ(first.conversation_id, "group:group-3");
  EXPECT_EQ(second.conversation_id, "group:group-4");
}

TEST(MessageEventIngressTest, BuildsRawNoticeEnvelopeFromPokeNotice) {
  const auto envelope =
      raw_notice_envelope_from_event("qq", "qq-main", qq_poke_notice_event());

  EXPECT_EQ(envelope.type, "obcx::core::events::RawNoticeEvent");
  EXPECT_EQ(envelope.source_platform, "qq");
  EXPECT_EQ(envelope.source_bot, "qq-main");
  EXPECT_EQ(envelope.conversation_id, "group:group-3");
  EXPECT_TRUE(
      envelope.id.starts_with("notice:qq:qq-main:group:group-3:notify:"));
  EXPECT_EQ(envelope.payload["notice_type"], "notify");
  EXPECT_EQ(envelope.payload["sender"], "user-7");
  EXPECT_EQ(envelope.payload["group_id"], "group-3");
  EXPECT_EQ(envelope.payload["payload"]["sub_type"], "poke");
  EXPECT_EQ(envelope.raw["target_id"], 80008);
  EXPECT_EQ(envelope.raw["notice_type"], "notify");
}

TEST(MessageEventIngressTest, NoticeUsesEventSelfIdWhenSourceBotIsEmpty) {
  const auto envelope =
      raw_notice_envelope_from_event("qq", "", qq_poke_notice_event());

  EXPECT_EQ(envelope.source_bot, "qq-main");
}

} // namespace obcx::core
