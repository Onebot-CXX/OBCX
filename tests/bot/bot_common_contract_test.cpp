// Deliberately limited to the modular common contract surface.
#include "core/bot/messaging_client.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {
using namespace obcx::bot;

static_assert(!std::is_default_constructible_v<BotInstallationRef>);
static_assert(!std::is_default_constructible_v<GroupTarget>);
static_assert(!std::is_default_constructible_v<BotMessageRef>);
static_assert(!std::is_default_constructible_v<PrivateTarget>);
static_assert(!std::is_default_constructible_v<PrivateMessageRef>);
static_assert(!std::is_default_constructible_v<SendGroupMessageRequest>);
static_assert(!std::is_default_constructible_v<SendPrivateMessageRequest>);

TEST(BotCommonContractTest, ReferencesHaveExplicitOpenSurfaceIdentity) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "fixture",
                                 .surface = SurfaceId{"test.echo"}},
                .native_group_id = "group-1"},
      .native_message_id = "42"};
  EXPECT_EQ(Json(message).get<BotMessageRef>(), message);
  auto different = message;
  different.group.installation.installation_id = "second";
  EXPECT_NE(different, message);
  different = message;
  different.group.native_group_id = "group-2";
  EXPECT_NE(different, message);
  auto malformed = Json(message.group.installation);
  malformed.erase("surface");
  EXPECT_THROW((void)malformed.get<BotInstallationRef>(),
               std::invalid_argument);
}

TEST(BotCommonContractTest, RejectsForgedActionIdentity) {
  const SendGroupMessageRequest request{
      .target = {.installation = {.installation_id = "fixture",
                                  .surface = SurfaceId{"test.echo"}},
                 .native_group_id = "group-1"},
      .message = {{.type = "text", .data = {{"text", "hello"}}}}};
  const auto document = Json(request);
  auto forged = document;
  forged["action"] = "test.echo.unregistered";
  EXPECT_THROW((void)forged.get<SendGroupMessageRequest>(),
               std::invalid_argument);
}

TEST(BotCommonContractTest, ErrorsPreserveSubmissionSafetyWithoutPlatformDtos) {
  const auto failure = BotOperationResult<SendMessageResult>::failure(
      {.code = BotOperationErrorCode::OutcomeUnknown,
       .message = "result unavailable",
       .retryable = false,
       .submission_safety = SubmissionSafety::PossiblySubmitted});
  EXPECT_EQ(Json(failure).get<BotOperationResult<SendMessageResult>>(),
            failure);
  EXPECT_EQ(Json(failure).at("error").at("submission_safety"),
            "possibly_submitted");
}

} // namespace
