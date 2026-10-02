#include "telegram/bot/client.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <type_traits>

namespace {
namespace telegram = obcx::telegram::bot;
using obcx::bot::Json;
using obcx::bot::OperationTraits;

static_assert(
    std::is_same_v<
        OperationTraits<telegram::SendTelegramTopicMessageRequest>::result_type,
        obcx::bot::SendMessageResult>);
static_assert(
    !OperationTraits<telegram::FetchTelegramFileRequest>::side_effecting);
static_assert(
    OperationTraits<telegram::SendTelegramPhotoRequest>::side_effecting);

auto baseline() -> Json {
  std::ifstream input{OBCX_BOT_GOLDEN_PATH};
  if (!input) {
    throw std::runtime_error("cannot open bot contract golden fixture");
  }
  return Json::parse(input);
}

TEST(BotTelegramContractTest, ValidatesTopicReplyResultCountAndFileBounds) {
  const auto document = baseline();
  auto topic = document.at("operations")
                   .at("telegram.message.send_topic")
                   .at("request")
                   .get<telegram::SendTelegramTopicMessageRequest>();
  topic.target.topic_id = 0;
  EXPECT_THROW(topic.validate(), std::invalid_argument);
  const auto &album =
      document.at("operations").at("telegram.media.send_group_urls");
  auto request =
      album.at("request").get<telegram::SendTelegramMediaGroupUrlsRequest>();
  auto result =
      album.at("success").at("value").get<obcx::bot::SendMessageResult>();
  result.messages.pop_back();
  EXPECT_THROW(OperationTraits<telegram::SendTelegramMediaGroupUrlsRequest>::
                   validate_result(request, result),
               std::invalid_argument);
  request.reply_to->group.native_group_id = "different-chat";
  EXPECT_THROW(request.validate(), std::invalid_argument);

  const auto &file = document.at("operations").at("telegram.media.fetch_file");
  auto fetch = file.at("request").get<telegram::FetchTelegramFileRequest>();
  const auto fetched =
      file.at("success").at("value").get<telegram::FetchedTelegramFile>();
  fetch.maximum_bytes = 3;
  EXPECT_THROW(
      OperationTraits<telegram::FetchTelegramFileRequest>::validate_result(
          fetch, fetched),
      std::invalid_argument);
}

} // namespace
