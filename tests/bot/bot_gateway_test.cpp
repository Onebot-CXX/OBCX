#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"
#include "onebot11/bot/operations.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

namespace {
using namespace obcx::bot;

class FakeGateway final : public BotOperationGateway {
public:
  explicit FakeGateway(OperationReply reply) : reply_(std::move(reply)) {}

  auto supported_actions(const BotInstallationRef &installation) const
      -> BotOperationResult<SupportedActions> override {
    return BotOperationResult<SupportedActions>::success(
        {.installation = installation,
         .actions = {SendGroupMessageRequest::action}});
  }

  auto invoke(OperationEnvelope envelope)
      -> boost::asio::awaitable<OperationReply> override {
    ++calls;
    co_await boost::asio::post(boost::asio::use_awaitable);
    envelope.validate();
    observed.emplace(std::move(envelope));
    co_return std::move(reply_);
  }

  unsigned calls = 0;
  std::optional<OperationEnvelope> observed;

private:
  OperationReply reply_;
};

auto send_request() -> SendGroupMessageRequest {
  return {.target = {.installation = {.installation_id = "fixture",
                                      .surface = SurfaceId{"test.echo"}},
                     .native_group_id = "group-1"},
          .message = {{.type = "text", .data = {{"text", "hello"}}}}};
}

template <typename Request>
auto execute(FakeGateway &gateway, Request request) {
  boost::asio::io_context io;
  auto completion =
      boost::asio::co_spawn(io, obcx::bot::invoke(gateway, std::move(request)),
                            boost::asio::use_future);
  io.run();
  return completion.get();
}

TEST(BotGatewayTest, TypedCallPreservesOwnedRequestAcrossSuspension) {
  const auto request = send_request();
  FakeGateway gateway{OperationReply::success(Json(SendMessageResult{
      .messages = {{.group = request.target, .native_message_id = "42"}}}))};
  const auto result = execute(gateway, request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value->primary().group, request.target);
  ASSERT_TRUE(gateway.observed);
  EXPECT_EQ(gateway.observed->installation, request.target.installation);
  EXPECT_EQ(gateway.observed->action, SendGroupMessageRequest::action);
  EXPECT_EQ(gateway.observed->payload, Json(request));
  EXPECT_EQ(gateway.calls, 1U);
}

TEST(BotGatewayTest, InvalidRequestNeverEntersGateway) {
  auto request = send_request();
  request.message.clear();
  FakeGateway gateway{OperationReply::success(Json::object())};
  const auto result = execute(gateway, request);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::InvalidRequest);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
  EXPECT_EQ(gateway.calls, 0U);
}

TEST(BotGatewayTest, MalformedSuccessOrErrorCannotEnableSafeResend) {
  const auto request = send_request();
  auto wrong_scope = request.target;
  wrong_scope.installation.installation_id = "another-installation";
  const std::vector<OperationReply> invalid_replies{
      OperationReply::success(Json::object()),
      OperationReply::success(Json(SendMessageResult{
          .messages = {{.group = wrong_scope, .native_message_id = "42"}}})),
      {.value = Json::object(),
       .error = BotOperationError{.message = "conflicting result"}},
      {.error =
           BotOperationError{.code = static_cast<BotOperationErrorCode>(255),
                             .message = "invalid error code"}},
      {.error =
           BotOperationError{.message = "https://example.test/?token=secret"}}};
  for (const auto &reply : invalid_replies) {
    FakeGateway gateway{reply};
    const auto result = execute(gateway, request);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
    EXPECT_EQ(result.error->submission_safety,
              SubmissionSafety::PossiblySubmitted);
    EXPECT_FALSE(result.error->retryable);
    EXPECT_EQ(Json(*result.error).dump().find("token=secret"),
              std::string::npos);
  }
}

TEST(BotGatewayTest, ValidatedProviderFailureKeepsItsOriginalSafety) {
  const BotOperationError error{.code = BotOperationErrorCode::ProviderRejected,
                                .message = "try later",
                                .provider_code = "429",
                                .retry_after = std::chrono::milliseconds{1500},
                                .retryable = true,
                                .submission_safety =
                                    SubmissionSafety::DefinitelyNotSubmitted};
  FakeGateway gateway{OperationReply::failure(error)};
  const auto result = execute(gateway, send_request());
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error, error);
}

TEST(BotGatewayTest, InvalidReadResultDoesNotInventASideEffect) {
  const obcx::onebot11::bot::GetOneBotGroupMemberRequest request{
      .target = {.installation = {.installation_id = "qq-fixture",
                                  .surface = obcx::onebot11::bot::surface},
                 .native_group_id = "123"},
      .user_id = "456"};
  FakeGateway gateway{OperationReply::success(Json::object())};
  const auto result = execute(gateway, request);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotGatewayTest, CommonEnvelopeAndSupportValuesDoNotEnumeratePlatforms) {
  SupportedActions supported{
      .installation = {.installation_id = "fixture",
                       .surface = SurfaceId{"test.echo"}},
      .actions = {ActionId{"test.z"}, ActionId{"test.a"}}};
  EXPECT_TRUE(supported.supports(ActionId{"test.a"}));
  const Json encoded = supported;
  EXPECT_EQ(encoded.at("actions"), Json::array({"test.a", "test.z"}));
  EXPECT_EQ(Json(encoded.get<SupportedActions>()), encoded);
  supported.actions.push_back(ActionId{"test.a"});
  EXPECT_THROW(supported.validate(), std::invalid_argument);
  OperationEnvelope envelope{.installation = supported.installation,
                             .action = ActionId{"test.a"},
                             .payload = {{"action", "test.z"}}};
  EXPECT_THROW(envelope.validate(), std::invalid_argument);
}

} // namespace
