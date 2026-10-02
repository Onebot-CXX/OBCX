#include "core/bot/operation_result.hpp"
#include "onebot11/bot/client.hpp"
#include "support/sdk_gateway_fixture.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <type_traits>

namespace {
namespace onebot = obcx::onebot11::bot;
using obcx::bot::Json;
using obcx::bot::OperationTraits;

static_assert(std::is_same_v<
              OperationTraits<onebot::GetOneBotGroupMemberRequest>::result_type,
              onebot::OneBotGroupMember>);
static_assert(
    !OperationTraits<onebot::GetOneBotGroupMemberRequest>::side_effecting);
static_assert(OperationTraits<onebot::PokeOneBotGroupRequest>::side_effecting);

auto baseline() -> Json {
  std::ifstream input{OBCX_BOT_GOLDEN_PATH};
  if (!input) {
    throw std::runtime_error("cannot open bot contract golden fixture");
  }
  return Json::parse(input);
}

TEST(BotOneBotContractTest, OwnedCodecRejectsWrongSurfaceAndResultScope) {
  using Request = onebot::GetOneBotGroupMemberRequest;
  using Traits = OperationTraits<Request>;
  const auto document = baseline();
  const auto &entry = document.at("operations").at("onebot11.group_member.get");
  const auto request = entry.at("request").get<Request>();
  auto forged = entry.at("request");
  forged["target"]["installation"]["surface"] = "test.echo";
  EXPECT_THROW((void)forged.get<Request>(), std::invalid_argument);
  auto result =
      entry.at("success").at("value").get<onebot::OneBotGroupMember>();
  result.target.native_group_id = "different-group";
  EXPECT_THROW(Traits::validate_result(request, result), std::invalid_argument);
}

auto group_forward() -> onebot::SendOneBotGroupForwardMessageRequest {
  return {.target = {.installation = {.installation_id = "qq-main",
                                      .surface = onebot::surface},
                     .native_group_id = "100"},
          .messages = Json::parse(R"([
            {"type":"node","data":{"content":[{"type":"image","data":{"file":"base64://YWJj"}}]}},
            {"type":"node","data":{"content":[{"type":"text","data":{"text":"probe"}}]}}
          ])"),
          .maximum_payload_bytes = 4096,
          .shared_files = {}};
}

TEST(BotOneBotContractTest, GroupForwardRoundTripsAndUsesClient) {
  using Request = onebot::SendOneBotGroupForwardMessageRequest;
  using Result = onebot::OneBotGroupForwardMessageResult;
  using Traits = OperationTraits<Request>;
  static_assert(Traits::side_effecting);
  const auto request = group_forward();
  EXPECT_NO_THROW(request.validate());
  EXPECT_EQ(Json(Json(request).get<Request>()), Json(request));
  auto result = Result{
      .target = request.target, .message_id = "42", .forward_id = "resource"};
  EXPECT_EQ(Json(Json(result).get<Result>()), Json(result));
  obcx::tests::ReplyGateway gateway{
      Traits::action(), obcx::bot::OperationReply::success(
                            obcx::bot::GatewayCodec<Result>::encode(result))};
  onebot::Client client{gateway};
  auto sent = obcx::tests::await_sdk(client.execute(request));
  ASSERT_TRUE(sent.ok());
  EXPECT_EQ(sent.value->forward_id, "resource");
  result.target.native_group_id = "101";
  EXPECT_THROW(Traits::validate_result(request, result), std::invalid_argument);
}

TEST(BotOneBotContractTest,
     GroupForwardRejectsAllReferencesAndSenderOverrides) {
  const auto valid = group_forward();
  for (const Json reference : {Json(0), Json(-1), Json(1), Json(2), Json("0"),
                               Json(0.5), Json(nullptr)}) {
    auto request = valid;
    request.messages[1]["data"]["reply_to"] = reference;
    EXPECT_THROW(request.validate(), std::exception);
  }
  for (const auto *field : {"uin", "name", "user_id", "nickname", "id"}) {
    auto forged = valid;
    forged.messages[0]["data"][field] = "123";
    EXPECT_THROW(forged.validate(), std::exception) << field;
  }
  auto request = valid;
  request.messages[1]["data"]["content"][0] = {{"type", "reply"},
                                               {"data", {{"id", "123"}}}};
  EXPECT_THROW(request.validate(), std::exception);
  request = valid;
  request.messages.erase(1);
  EXPECT_THROW(request.validate(), std::exception);
  for (const auto *file : {"base64://", "base64://a", "base64://!bad",
                           "file:///tmp/p.png", "https://example.test/p.png"}) {
    request = valid;
    request.messages[0]["data"]["content"][0]["data"]["file"] = file;
    EXPECT_THROW(request.validate(), std::exception);
  }
  request = valid;
  request.target.installation.surface =
      obcx::bot::SurfaceId{"telegram.bot_api"};
  EXPECT_THROW(request.validate(), std::exception);
}

TEST(BotOneBotContractTest, GroupForwardUsesOnlyRegisteredCanonicalFileUris) {
  using Request = onebot::SendOneBotGroupForwardMessageRequest;
  auto request = group_forward();
  const std::string uri = "file:///shared/qq/%E5%9B%BE%20%23%25.png";
  auto &file = request.messages[0]["data"]["content"][0]["data"]["file"];
  file = uri;
  EXPECT_THROW(request.validate(), std::exception);
  request.shared_files = {uri};
  EXPECT_NO_THROW(request.validate());
  EXPECT_EQ(Json(Json(request).get<Request>()), Json(request));
  EXPECT_FALSE(request.wire_document(1)["params"].contains("shared_files"));
  const auto bytes =
      request.wire_document(std::numeric_limits<std::uint64_t>::max())
          .dump()
          .size();
  request.maximum_payload_bytes = bytes;
  EXPECT_NO_THROW(request.validate());
  --request.maximum_payload_bytes;
  EXPECT_THROW(request.validate(), std::exception);
  request.maximum_payload_bytes = 4096;
  for (const auto *bad : {"file:///",
                          "file://host/shared/a.png",
                          "file:////shared/a.png",
                          "file:///shared/../a.png",
                          "file:///shared/./a.png",
                          "file:///shared/%2E%2E/a.png",
                          "file:///shared/%2Fetc",
                          "file:///shared/%5Ca.png",
                          "file:///shared/%00.png",
                          "file:///shared/%0A.png",
                          "file:///shared/%FF.png",
                          "file:///shared/%C0%AF.png",
                          "file:///shared/a%2f.png",
                          "file:///shared/a%.png",
                          "file:///shared/a%2.png",
                          "file:///shared/a.png?query",
                          "file:///shared/a.png#fragment",
                          "file:///shared/a b.png",
                          "file:///shared/a/",
                          "https://host/a.png"}) {
    file = bad;
    request.shared_files = {bad};
    EXPECT_THROW(request.validate(), std::exception) << bad;
  }
  file = uri;
  request.shared_files = {uri, uri};
  EXPECT_THROW(request.validate(), std::exception);
  request.shared_files = {uri, "file:///unused.png"};
  EXPECT_THROW(request.validate(), std::exception);
  request.shared_files = {uri};
  auto document = Json(request);
  document.erase("shared_files");
  EXPECT_THROW((void)document.get<Request>(), std::exception);
}

TEST(BotOneBotContractTest, GroupForwardRequiresExplicitBudgetAndBoundedNodes) {
  using Request = onebot::SendOneBotGroupForwardMessageRequest;
  auto request = group_forward();
  auto document = Json(request);
  document["action"] = "onebot11.experimental.local_forward.send";
  EXPECT_THROW((void)document.get<Request>(), std::exception);
  document = Json(request);
  document.erase("maximum_payload_bytes");
  EXPECT_THROW((void)document.get<Request>(), std::exception);
  document["maximum_payload_bytes"] = -1;
  EXPECT_THROW((void)document.get<Request>(), std::exception);
  request.maximum_payload_bytes = 1;
  EXPECT_THROW(request.validate(), std::exception);
  request = group_forward();
  const auto text = Json::parse(
      R"({"type":"node","data":{"content":[{"type":"text","data":{"text":"x"}}]}})");
  request.messages = Json::array();
  EXPECT_THROW(request.validate(), std::exception);
  request.maximum_payload_bytes = 65536;
  for (int i = 0; i < 99; ++i)
    request.messages.push_back(text);
  EXPECT_NO_THROW(request.validate());
  request.messages.push_back(text);
  EXPECT_THROW(request.validate(), std::exception);
}

TEST(BotOneBotContractTest,
     GroupForwardKeepsPairsAndNeverSplitsOversizedOutput) {
  auto request = group_forward();
  const auto pair = request.messages;
  request.maximum_payload_bytes = 65536;
  request.messages = Json::array();
  for (int i = 0; i < 49; ++i) {
    request.messages.push_back(pair[0]);
    auto text = pair[1];
    text["data"]["content"][0]["data"]["text"] = std::to_string(i);
    request.messages.push_back(text);
  }
  EXPECT_NO_THROW(request.validate());
  EXPECT_EQ(request.messages.size(), 98U);
  request.messages.push_back(pair[1]);
  EXPECT_NO_THROW(request.validate());
  request.messages.back() = pair[0];
  EXPECT_THROW(request.validate(), std::exception); // An incomplete pair.
  request.messages.push_back(pair[1]);
  EXPECT_THROW(request.validate(), std::exception); // 50 pairs, no splitting.
  request = group_forward();
  request.messages[1] = pair[0];
  EXPECT_THROW(request.validate(), std::exception); // Two images in a row.
}

TEST(BotOneBotContractTest, GroupForwardBudgetIncludesEscapingAndEnvelope) {
  auto request = group_forward();
  request.messages[1]["data"]["content"][0]["data"]["text"] =
      "quote: \"\\\n中文";
  const auto wire =
      request.wire_document(std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(wire["action"], "send_group_forward_msg");
  EXPECT_FALSE(wire["params"].contains("maximum_payload_bytes"));
  request.maximum_payload_bytes = wire.dump().size();
  EXPECT_NO_THROW(request.validate());
  --request.maximum_payload_bytes;
  EXPECT_THROW(request.validate(), std::exception);
}

} // namespace
