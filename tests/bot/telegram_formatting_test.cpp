#include "telegram/adapter/protocol_adapter.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

auto styled_message(bool with_photo = false) -> obcx::common::Message {
  obcx::common::Message message{
      {.type = "text",
       .data = {{"text", "[张😀]"}, {"telegram_style", "italic"}}},
      {.type = "text", .data = {{"text", "\thello"}}},
  };
  if (with_photo) {
    message.push_back(
        {.type = "image", .data = {{"url", "https://example.test/a.png"}}});
  }
  return message;
}

TEST(TelegramFormattingTest, EmitsItalicEntityForStyledTextSegment) {
  obcx::adapter::telegram::ProtocolAdapter adapter;
  const auto payload = nlohmann::json::parse(
      adapter.serialize_send_message_request("-1001", styled_message(), 7));

  EXPECT_EQ(payload["method"], "sendMessage");
  EXPECT_EQ(payload["text"], "[张😀]\thello");
  ASSERT_TRUE(payload.contains("entities"));
  ASSERT_EQ(payload["entities"].size(), 1U);
  EXPECT_EQ(payload["entities"][0]["type"], "italic");
  EXPECT_EQ(payload["entities"][0]["offset"], 0U);
  EXPECT_EQ(payload["entities"][0]["length"], 5U);
  EXPECT_FALSE(payload.contains("parse_mode"));
}

TEST(TelegramFormattingTest, EmitsItalicEntityForMediaCaption) {
  obcx::adapter::telegram::ProtocolAdapter adapter;
  const auto payload = nlohmann::json::parse(
      adapter.serialize_send_message_request("-1001", styled_message(true), 8));

  EXPECT_EQ(payload["method"], "sendPhoto");
  EXPECT_EQ(payload["caption"], "[张😀]\thello");
  ASSERT_TRUE(payload.contains("caption_entities"));
  ASSERT_EQ(payload["caption_entities"].size(), 1U);
  EXPECT_EQ(payload["caption_entities"][0]["type"], "italic");
  EXPECT_EQ(payload["caption_entities"][0]["offset"], 0U);
  EXPECT_EQ(payload["caption_entities"][0]["length"], 5U);
}

TEST(TelegramFormattingTest, EmitsCaptionEntitiesForMediaGroup) {
  obcx::adapter::telegram::ProtocolAdapter adapter;
  const std::vector<std::pair<std::string, std::string>> media = {
      {"photo", "https://example.test/a.png"},
      {"photo", "https://example.test/b.png"},
  };
  const std::vector<obcx::core::TelegramTextEntity> entities = {
      {.type = "italic", .offset = 0, .length = 5}};
  const auto payload = nlohmann::json::parse(
      adapter.serialize_send_media_group_request_with_entities(
          "-1001", media, "[张😀]", {}, {}, 9, entities));

  ASSERT_EQ(payload["media"].size(), 2U);
  EXPECT_EQ(payload["media"][0]["caption"], "[张😀]");
  ASSERT_EQ(payload["media"][0]["caption_entities"].size(), 1U);
  EXPECT_EQ(payload["media"][0]["caption_entities"][0]["offset"], 0U);
  EXPECT_EQ(payload["media"][0]["caption_entities"][0]["length"], 5U);
  EXPECT_FALSE(payload["media"][1].contains("caption_entities"));
}

} // namespace
