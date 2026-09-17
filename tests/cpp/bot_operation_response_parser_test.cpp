#include "onebot11/bot/response_parser.hpp"
#include "telegram/bot/response_parser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace {

using obcx::bot::BotOperationErrorCode;
using obcx::bot::SubmissionSafety;

TEST(BotOperationResponseParserTest, ParsesTelegramSuccessValues) {
  const auto message = obcx::telegram::bot::parse_telegram_operation_response(
      R"({"ok":true,"result":{"message_id":42,"chat":{"id":-1001}}})", true);
  ASSERT_TRUE(message.ok());
  EXPECT_EQ(message.value->at("message_id"), 42);

  const auto mutation = obcx::telegram::bot::parse_telegram_operation_response(
      R"({"ok":true,"result":true})", true);
  ASSERT_TRUE(mutation.ok());
  EXPECT_TRUE(mutation.value->get<bool>());

  const auto media = obcx::telegram::bot::parse_telegram_operation_response(
      R"({"ok":true,"result":[{"message_id":1},{"message_id":2}]})", true);
  ASSERT_TRUE(media.ok());
  EXPECT_EQ(media.value->size(), 2U);
}

TEST(BotOperationResponseParserTest,
     ParsesTelegramProviderErrorAndRetryMetadata) {
  const auto result = obcx::telegram::bot::parse_telegram_operation_response(
      R"({"ok":false,"error_code":429,"description":"Too Many Requests","parameters":{"retry_after":3}})",
      true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::ProviderRejected);
  EXPECT_EQ(result.error->provider_code, "429");
  EXPECT_EQ(result.error->retry_after, std::chrono::milliseconds{3000});
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotOperationResponseParserTest, RedactsTelegramProviderDescription) {
  const auto result = obcx::telegram::bot::parse_telegram_operation_response(
      R"({"ok":false,"error_code":400,"description":"authorization: Bearer secret"})",
      true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->message, "[redacted provider diagnostic]");
  EXPECT_EQ(nlohmann::json(*result.error).dump().find("Bearer secret"),
            std::string::npos);
}

TEST(BotOperationResponseParserTest, PlatformDiagnosticsRedactCredentials) {
  for (const auto description :
       {"access_token=fixture-secret", "proxy_username=fixture-user",
        "proxy_password=fixture-secret", "authorization: Bearer fixture",
        "https://example.test/file?X-Amz-Signature=fixture",
        "https://api.telegram.org/file/bot123:secret/file.jpg"}) {
    const nlohmann::json telegram{
        {"ok", false}, {"error_code", 429}, {"description", description}};
    const nlohmann::json onebot{
        {"status", "failed"}, {"retcode", -1}, {"message", description}};
    for (const auto &result :
         {obcx::telegram::bot::parse_telegram_operation_response(
              telegram.dump(), true),
          obcx::onebot11::bot::parse_onebot11_operation_response(onebot.dump(),
                                                                 true)}) {
      ASSERT_FALSE(result.ok());
      EXPECT_EQ(result.error->message, "[redacted provider diagnostic]");
      EXPECT_TRUE(result.error->retryable);
      EXPECT_EQ(result.error->submission_safety,
                SubmissionSafety::DefinitelyNotSubmitted);
      EXPECT_EQ(nlohmann::json(*result.error).dump().find(description),
                std::string::npos);
    }
  }
}

TEST(BotOperationResponseParserTest, TelegramOwnsBareTokenRecognition) {
  const auto result = obcx::telegram::bot::parse_telegram_operation_response(
      R"({"ok":false,"error_code":"123456:abcdef_ABC-12","description":"failed for bot123456:abcdef_ABC-12"})",
      true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->message, "[redacted provider diagnostic]");
  EXPECT_EQ(result.error->provider_code, "[redacted provider diagnostic]");
  EXPECT_EQ(nlohmann::json(*result.error).dump().find("abcdef_ABC-12"),
            std::string::npos);
}

TEST(BotOperationResponseParserTest,
     TelegramMalformedResponseUsesConservativeSafety) {
  const auto side_effect =
      obcx::telegram::bot::parse_telegram_operation_response(R"({"ok":true})",
                                                             true);
  ASSERT_FALSE(side_effect.ok());
  EXPECT_EQ(side_effect.error->code, BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(side_effect.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
  EXPECT_FALSE(side_effect.error->retryable);

  const auto read_only =
      obcx::telegram::bot::parse_telegram_operation_response("not-json", false);
  ASSERT_FALSE(read_only.ok());
  EXPECT_EQ(read_only.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
  EXPECT_TRUE(read_only.error->retryable);
}

TEST(BotOperationResponseParserTest, ParsesOneBotSuccessValues) {
  const auto send = obcx::onebot11::bot::parse_onebot11_operation_response(
      R"({"status":"ok","retcode":0,"data":{"message_id":7},"echo":1})", true);
  ASSERT_TRUE(send.ok());
  EXPECT_EQ(send.value->at("message_id"), 7);

  const auto deletion = obcx::onebot11::bot::parse_onebot11_operation_response(
      R"({"status":"ok","retcode":0,"data":null})", true);
  ASSERT_TRUE(deletion.ok());
  EXPECT_TRUE(deletion.value->is_null());
}

TEST(BotOperationResponseParserTest, ParsesOneBotProviderFailure) {
  const auto result = obcx::onebot11::bot::parse_onebot11_operation_response(
      R"({"status":"failed","retcode":1403,"message":"permission denied","wording":"denied","echo":1})",
      true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::ProviderRejected);
  EXPECT_EQ(result.error->provider_code, "1403");
  EXPECT_EQ(result.error->message, "permission denied");
  EXPECT_FALSE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotOperationResponseParserTest,
     NegativeOneBotRetcodeIsDefinitelyRetryable) {
  const auto result = obcx::onebot11::bot::parse_onebot11_operation_response(
      R"({"status":"failed","retcode":-1,"message":"temporary unavailable","data":null})",
      true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::ProviderRejected);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

TEST(BotOperationResponseParserTest,
     OneBotMalformedAndIncompleteBodiesNeverExposeBody) {
  const std::string secret_body = R"({"status":"ok","access_token":"secret"})";
  const auto result =
      obcx::onebot11::bot::parse_onebot11_operation_response(secret_body, true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
  const auto diagnostic = nlohmann::json(*result.error).dump();
  EXPECT_EQ(diagnostic.find("access_token"), std::string::npos);
  EXPECT_EQ(diagnostic.find("secret"), std::string::npos);
}

} // namespace
