#include "telegram/network/connection_manager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {

auto multipart_field(const std::string &body, std::string_view name)
    -> std::string {
  const std::string marker = "Content-Disposition: form-data; name=\"" +
                             std::string{name} + "\"\r\n\r\n";
  const auto start = body.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  const auto value_start = start + marker.size();
  const auto value_end = body.find("\r\n--", value_start);
  if (value_end == std::string::npos) {
    return {};
  }
  return body.substr(value_start, value_end - value_start);
}

TEST(TelegramMultipartTest, BuildsAttachMediaGroupWithBinaryFileParts) {
  const std::string first_data{"first\0image", 11};
  const std::string second_data{"second-image"};
  const std::vector<obcx::core::TelegramMediaUpload> media = {
      {.type = "photo",
       .filename = "first\"\r\n.png",
       .mime_type = "image/png",
       .data = first_data},
      {.type = "photo",
       .filename = "second.jpg",
       .mime_type = "image/jpeg",
       .data = second_data},
  };

  const auto request =
      obcx::network::build_telegram_media_group_multipart_with_entities(
          "-10042", media, "album caption", 7, "99",
          {{.type = "italic", .offset = 0, .length = 5}});

  EXPECT_TRUE(request.content_type.starts_with(
      "multipart/form-data; boundary=----OBCXBoundary"));
  EXPECT_EQ(multipart_field(request.body, "chat_id"), "-10042");
  EXPECT_EQ(multipart_field(request.body, "message_thread_id"), "7");
  EXPECT_EQ(multipart_field(request.body, "reply_to_message_id"), "99");

  const auto input_media =
      nlohmann::json::parse(multipart_field(request.body, "media"));
  ASSERT_EQ(input_media.size(), 2U);
  EXPECT_EQ(input_media[0]["media"], "attach://media_0");
  EXPECT_EQ(input_media[0]["caption"], "album caption");
  ASSERT_EQ(input_media[0]["caption_entities"].size(), 1U);
  EXPECT_EQ(input_media[0]["caption_entities"][0]["type"], "italic");
  EXPECT_EQ(input_media[0]["caption_entities"][0]["offset"], 0U);
  EXPECT_EQ(input_media[0]["caption_entities"][0]["length"], 5U);
  EXPECT_EQ(input_media[1]["media"], "attach://media_1");
  EXPECT_FALSE(input_media[1].contains("caption"));
  EXPECT_FALSE(input_media[1].contains("caption_entities"));

  EXPECT_NE(request.body.find("name=\"media_0\"; filename=\"first___.png\""),
            std::string::npos);
  EXPECT_NE(request.body.find("Content-Type: image/png"), std::string::npos);
  EXPECT_NE(request.body.find(first_data), std::string::npos);
  EXPECT_NE(request.body.find(second_data), std::string::npos);
}

TEST(TelegramHttpResponseTest, PreservesProviderErrorEnvelope) {
  const std::string body =
      R"({"ok":false,"error_code":429,"description":"Too Many Requests","parameters":{"retry_after":3}})";
  const obcx::network::HttpResponse response{.status_code = 429, .body = body};

  EXPECT_EQ(obcx::network::telegram_api_response_body(response), body);
}

TEST(TelegramHttpResponseTest, RejectsNonTelegramHttpErrorBody) {
  const obcx::network::HttpResponse response{.status_code = 502,
                                             .body = "proxy failure"};

  EXPECT_THROW((void)obcx::network::telegram_api_response_body(response),
               std::runtime_error);
}

TEST(TelegramMultipartTest, RejectsInvalidGroupSizeAndEmptyFiles) {
  EXPECT_THROW(
      {
        const auto ignored =
            obcx::network::build_telegram_media_group_multipart(
                "chat",
                {{.type = "photo",
                  .filename = "one.jpg",
                  .mime_type = "image/jpeg",
                  .data = "one"}},
                "", std::nullopt, std::nullopt);
        (void)ignored;
      },
      std::invalid_argument);

  std::vector<obcx::core::TelegramMediaUpload> media = {
      {.type = "photo",
       .filename = "empty.jpg",
       .mime_type = "image/jpeg",
       .data = ""},
      {.type = "photo",
       .filename = "two.jpg",
       .mime_type = "image/jpeg",
       .data = "two"},
  };
  EXPECT_THROW(
      {
        const auto ignored =
            obcx::network::build_telegram_media_group_multipart(
                "chat", media, "", std::nullopt, std::nullopt);
        (void)ignored;
      },
      std::invalid_argument);
}

} // namespace
