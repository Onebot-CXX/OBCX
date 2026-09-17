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

template <typename Request> void replay(const Json &document) {
  using Traits = OperationTraits<Request>;
  using Result = typename Traits::result_type;
  const auto &entry = document.at("operations").at(Traits::action().value());
  const auto request = entry.at("request").template get<Request>();
  const auto result = entry.at("success").at("value").template get<Result>();
  EXPECT_EQ(Json(request), entry.at("request"));
  EXPECT_EQ(Json(result), entry.at("success").at("value"));
  EXPECT_EQ(Traits::installation(request).surface, onebot::surface);
  EXPECT_NO_THROW(Traits::validate_result(request, result));
  auto gateway_result = result;
  obcx::tests::ReplyGateway gateway{
      Traits::action(),
      obcx::bot::OperationReply::success(
          obcx::bot::GatewayCodec<Result>::encode(gateway_result))};
  onebot::Client client{gateway};
  const auto delivered = obcx::tests::await_sdk(client.execute(request));
  ASSERT_TRUE(delivered.ok());
  EXPECT_EQ(Json(*delivered.value), entry.at("success").at("value"));
  ASSERT_TRUE(gateway.observed);
  EXPECT_EQ(gateway.observed->action, Traits::action());
  for (const auto &error : document.at("errors")) {
    EXPECT_EQ(Json(error.template get<obcx::bot::BotOperationResult<Result>>()),
              error);
  }
}

TEST(BotOneBotContractTest, AllOwnedActionsReplayWithoutAnotherPlatformSdk) {
  const auto document = baseline();
  replay<onebot::GetOneBotGroupMemberRequest>(document);
  replay<onebot::GetOneBotForwardMessageRequest>(document);
  replay<onebot::ResolveOneBotGroupFileRequest>(document);
  replay<onebot::ResolveOneBotPrivateFileRequest>(document);
  replay<onebot::PokeOneBotGroupRequest>(document);
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

} // namespace
