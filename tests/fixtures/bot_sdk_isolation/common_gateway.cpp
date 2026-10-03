#include "core/bot/messaging_client.hpp"
#include "support/sdk_gateway_fixture.hpp"
#include <gtest/gtest.h>

TEST(CommonInstalledGateway, InvokesWithoutAnyPlatformHeadersOrRuntime) {
  const obcx::bot::GroupTarget target{
      {"fixture", obcx::bot::SurfaceId{"test.echo"}}, "group"};
  const obcx::bot::SendMessageResult expected{{{target, "42"}}};
  obcx::tests::ReplyGateway gateway{
      obcx::bot::SendGroupMessageRequest::action,
      obcx::bot::OperationReply::success(obcx::bot::Json(expected))};
  const auto result = obcx::tests::await_sdk(
      obcx::bot::invoke(gateway, obcx::bot::SendGroupMessageRequest{
                                     target, {{"text", {{"text", "hello"}}}}}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result.value, expected);
  ASSERT_TRUE(gateway.observed);
  EXPECT_EQ(gateway.observed->installation, target.installation);

  const obcx::bot::PrivateTarget private_target{target.installation, "user"};
  const obcx::bot::SendPrivateMessageResult private_expected{
      {{private_target, "43"}}};
  obcx::tests::ReplyGateway private_gateway{
      obcx::bot::SendPrivateMessageRequest::action,
      obcx::bot::OperationReply::success(obcx::bot::Json(private_expected))};
  const auto private_result = obcx::tests::await_sdk(obcx::bot::invoke(
      private_gateway, obcx::bot::SendPrivateMessageRequest{
                           private_target, {{"text", {{"text", "private"}}}}}));
  ASSERT_TRUE(private_result.ok());
  EXPECT_EQ(*private_result.value, private_expected);
  ASSERT_TRUE(private_gateway.observed);
  EXPECT_EQ(private_gateway.observed->installation,
            private_target.installation);
}
