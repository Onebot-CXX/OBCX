#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <gtest/gtest.h>
#include <variant>

namespace {

TEST(ProtocolEventParsingTest, OneBotParsesMessageAndUnescapesOnlyRawText) {
  obcx::adapter::onebot11::ProtocolAdapter adapter;
  const auto event = adapter.parse_event(R"({
    "post_type": "message", "message_type": "group",
    "self_id": "100", "message_id": 42, "user_id": 123, "group_id": 456,
    "raw_message": "&#91;hello&#93;&amp;",
    "message": [{"type": "text", "data": {"text": "&#91;hello&#93;&amp;"}}]
  })");
  ASSERT_TRUE(event.has_value());
  ASSERT_TRUE(std::holds_alternative<obcx::common::MessageEvent>(*event));
  const auto &message = std::get<obcx::common::MessageEvent>(*event);
  EXPECT_EQ(message.message_id, "42");
  EXPECT_EQ(message.user_id, "123");
  EXPECT_EQ(message.group_id, "456");
  EXPECT_EQ(message.raw_message, "[hello]&");
  ASSERT_EQ(message.message.size(), 1U);
  EXPECT_EQ(message.message.front().data.at("text"), "&#91;hello&#93;&amp;");
}

TEST(ProtocolEventParsingTest, OneBotParsesNoticeAndRequest) {
  obcx::adapter::onebot11::ProtocolAdapter adapter;
  const auto notice = adapter.parse_event(R"({
    "post_type": "notice", "notice_type": "group_recall",
    "user_id": 123, "group_id": 456, "message_id": 42
  })");
  ASSERT_TRUE(notice.has_value());
  ASSERT_TRUE(std::holds_alternative<obcx::common::NoticeEvent>(*notice));
  const auto &parsed_notice = std::get<obcx::common::NoticeEvent>(*notice);
  EXPECT_EQ(parsed_notice.notice_type, "group_recall");
  EXPECT_EQ(parsed_notice.data.at("message_id"), 42);

  const auto request = adapter.parse_event(R"({
    "post_type": "request", "request_type": "friend",
    "user_id": 123, "comment": "hello", "flag": "request-1"
  })");
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(std::holds_alternative<obcx::common::RequestEvent>(*request));
  const auto &parsed_request = std::get<obcx::common::RequestEvent>(*request);
  EXPECT_EQ(parsed_request.request_type, "friend");
  EXPECT_EQ(parsed_request.flag, "request-1");
}

TEST(ProtocolEventParsingTest, OneBotDistinguishesHeartbeatAndOtherMetaEvents) {
  obcx::adapter::onebot11::ProtocolAdapter adapter;
  const auto heartbeat = adapter.parse_event(R"({
    "post_type": "meta_event", "meta_event_type": "heartbeat",
    "interval": 5000, "status": {"online": true}
  })");
  ASSERT_TRUE(heartbeat.has_value());
  ASSERT_TRUE(std::holds_alternative<obcx::common::HeartbeatEvent>(*heartbeat));
  const auto &parsed = std::get<obcx::common::HeartbeatEvent>(*heartbeat);
  EXPECT_EQ(parsed.interval, 5000);
  EXPECT_EQ(parsed.status.at("online"), true);

  const auto lifecycle = adapter.parse_event(R"({
    "post_type": "meta_event", "meta_event_type": "lifecycle",
    "sub_type": "connect"
  })");
  ASSERT_TRUE(lifecycle.has_value());
  ASSERT_TRUE(std::holds_alternative<obcx::common::MetaEvent>(*lifecycle));
  EXPECT_EQ(std::get<obcx::common::MetaEvent>(*lifecycle).sub_type, "connect");
}

TEST(ProtocolEventParsingTest, OneBotRejectsInvalidAndUnknownEvents) {
  obcx::adapter::onebot11::ProtocolAdapter adapter;
  EXPECT_FALSE(adapter.parse_event("not json"));
  EXPECT_FALSE(adapter.parse_event("{}"));
  EXPECT_FALSE(adapter.parse_event(R"({"post_type":"unknown"})"));
  EXPECT_FALSE(adapter.parse_event(R"({"post_type":"message","message":42})"));
}

TEST(ProtocolEventParsingTest, TelegramParsesMessageThroughProtocolAdapter) {
  obcx::adapter::telegram::ProtocolAdapter adapter;
  const auto event = adapter.parse_event(R"({
    "update_id": 1,
    "message": {
      "message_id": 42, "from": {"id": 123},
      "chat": {"id": -456, "type": "supergroup"}, "text": "hello"
    }
  })");
  ASSERT_TRUE(event.has_value());
  ASSERT_TRUE(std::holds_alternative<obcx::common::MessageEvent>(*event));
  const auto &message = std::get<obcx::common::MessageEvent>(*event);
  EXPECT_EQ(message.message_id, "42");
  EXPECT_EQ(message.user_id, "123");
  EXPECT_EQ(message.group_id, "-456");
  EXPECT_EQ(message.raw_message, "hello");
  EXPECT_FALSE(adapter.parse_event("not json"));
  EXPECT_FALSE(adapter.parse_event("{}"));
  EXPECT_FALSE(adapter.parse_event(R"({"update_id":2})"));
}

} // namespace
