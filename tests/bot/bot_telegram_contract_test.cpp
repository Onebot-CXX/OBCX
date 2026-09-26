#include "support/sdk_gateway_fixture.hpp"
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

template <typename Request> void replay(const Json &document) {
  using Traits = OperationTraits<Request>;
  using Result = typename Traits::result_type;
  const auto &entry = document.at("operations").at(Traits::action().value());
  const auto request = entry.at("request").template get<Request>();
  const auto result = entry.at("success").at("value").template get<Result>();
  EXPECT_EQ(Json(request), entry.at("request"));
  EXPECT_EQ(Json(result), entry.at("success").at("value"));
  EXPECT_EQ(Traits::installation(request).surface, telegram::surface);
  EXPECT_NO_THROW(Traits::validate_result(request, result));
  auto gateway_result = result;
  obcx::tests::ReplyGateway gateway{
      Traits::action(),
      obcx::bot::OperationReply::success(
          obcx::bot::GatewayCodec<Result>::encode(gateway_result))};
  telegram::Client client{gateway};
  const auto delivered = obcx::tests::await_sdk(client.execute(request));
  ASSERT_TRUE(delivered.ok());
  EXPECT_EQ(Json(*delivered.value), entry.at("success").at("value"));
  ASSERT_TRUE(gateway.observed);
  EXPECT_EQ(gateway.observed->action, Traits::action());
  if constexpr (std::is_same_v<
                    Request, telegram::SendTelegramMediaGroupUploadsRequest>) {
    EXPECT_TRUE(
        gateway.observed->payload.at("media").at(0).at("bytes").is_binary());
  }
  for (const auto &error : document.at("errors")) {
    EXPECT_EQ(Json(error.template get<obcx::bot::BotOperationResult<Result>>()),
              error);
  }
}

TEST(BotTelegramContractTest, AllOwnedActionsReplayWithoutAnotherPlatformSdk) {
  const auto document = baseline();
  replay<telegram::SendTelegramTopicMessageRequest>(document);
  replay<telegram::EditTelegramMessageTextRequest>(document);
  replay<telegram::SendTelegramPhotoRequest>(document);
  replay<telegram::SendTelegramMediaGroupUrlsRequest>(document);
  replay<telegram::SendTelegramMediaGroupUploadsRequest>(document);
  replay<telegram::FetchTelegramFileRequest>(document);
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
