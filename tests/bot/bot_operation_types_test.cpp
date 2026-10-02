#include "core/bot/messaging.hpp"
#include "onebot11/bot/operations.hpp"
#include "telegram/bot/operations.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using obcx::bot::ActionId;
using obcx::bot::BotInstallationRef;
using obcx::bot::BotMessageRef;
using obcx::bot::BotOperationError;
using obcx::bot::BotOperationErrorCode;
using obcx::bot::BotOperationResult;
using obcx::bot::GroupTarget;
using obcx::bot::PrivateMessageRef;
using obcx::bot::PrivateTarget;
using obcx::bot::SubmissionSafety;
using obcx::bot::SurfaceId;
using obcx::telegram::bot::TelegramTopicTarget;

TEST(BotOperationTypesTest, TelegramTopicRequiresTelegramAndPositiveId) {
  TelegramTopicTarget valid{
      .group = {.installation = {.installation_id = "tg",
                                 .surface = SurfaceId{"telegram.bot_api"}},
                .native_group_id = "chat"},
      .topic_id = 7,
  };
  EXPECT_NO_THROW(valid.validate());
  EXPECT_EQ(nlohmann::json(valid).get<TelegramTopicTarget>(), valid);

  valid.topic_id = 0;
  EXPECT_THROW(valid.validate(), std::invalid_argument);
  valid.topic_id = 7;
  valid.group.installation.surface = SurfaceId{"onebot11.qq"};
  EXPECT_THROW(valid.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, InvalidJsonAndOutcomeSafetyAreRejected) {
  // References validate syntax; production registration validates support.
  EXPECT_NO_THROW((void)nlohmann::json({{"installation_id", "telegram-main"},
                                        {"surface", "qq.official"}})
                      .get<BotInstallationRef>());
  EXPECT_THROW((void)nlohmann::json({{"surface", "telegram.bot_api"}})
                   .get<BotInstallationRef>(),
               std::invalid_argument);
  EXPECT_THROW((void)nlohmann::json(
                   {{"installation_id", ""}, {"surface", "telegram.bot_api"}})
                   .get<BotInstallationRef>(),
               std::invalid_argument);

  BotOperationError unknown{
      .code = BotOperationErrorCode::OutcomeUnknown,
      .message = "unknown",
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  EXPECT_THROW(unknown.validate(), std::invalid_argument);
  unknown.submission_safety = SubmissionSafety::PossiblySubmitted;
  EXPECT_NO_THROW(unknown.validate());
}

TEST(BotOperationTypesTest, TelegramEditRejectsForeignSurface) {
  const BotMessageRef telegram_message{
      .group = {.installation = {.installation_id = "tg-main",
                                 .surface = SurfaceId{"telegram.bot_api"}},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const obcx::telegram::bot::EditTelegramMessageTextRequest edit{
      .message = telegram_message,
      .text = "edited",
      .parse_mode = "HTML",
  };
  auto wrong_surface = edit;
  wrong_surface.message.group.installation.surface = SurfaceId{"onebot11.qq"};
  EXPECT_THROW(wrong_surface.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, MessageRequestValidationRejectsInvalidPayloads) {
  const auto target = GroupTarget{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .native_group_id = "123",
  };
  EXPECT_THROW(
      (obcx::bot::SendGroupMessageRequest{.target = target, .message = {}})
          .validate(),
      std::invalid_argument);

  EXPECT_THROW((void)nlohmann::json({{"installation",
                                      {{"installation_id", "qq-main"},
                                       {"surface", "onebot11.qq"}}},
                                     {"native_user_id", "7"},
                                     {"native_group_id", "7"}})
                   .get<PrivateTarget>(),
               std::invalid_argument);

  auto mismatched = nlohmann::json{
      {"action", "telegram.message.send_topic"},
      {"target", target},
      {"message",
       nlohmann::json::array({{{"type", "text"}, {"data", {{"text", "x"}}}}})},
  };
  EXPECT_THROW((void)mismatched.get<obcx::bot::SendGroupMessageRequest>(),
               std::invalid_argument);
}

TEST(BotOperationTypesTest, TelegramUploadBytesAreBoundedAndRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = SurfaceId{"telegram.bot_api"}},
      .native_group_id = "-1001",
  };
  obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest request{
      .target = target,
      .media = {{.type = "photo",
                 .filename = "image.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {0x00, 0x7F, 0x80, 0xFF}},
                {.type = "photo",
                 .filename = "image-2.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {0x01, 0x02}}},
      .caption = "upload",
      .maximum_bytes = 16,
  };
  const auto document = nlohmann::json(request);
  EXPECT_EQ(
      document.get<obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest>()
          .media,
      request.media);
  EXPECT_NO_THROW(request.validate());

  request.maximum_bytes = 3;
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.maximum_bytes = obcx::telegram::bot::maximum_actor_media_bytes + 1;
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.maximum_bytes = 16;
  request.media.resize(1);
  EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, TelegramFileFetchKeepsMetadataAndByteLimit) {
  const BotInstallationRef installation{
      .installation_id = "tg-main",
      .surface = SurfaceId{"telegram.bot_api"},
  };
  obcx::telegram::bot::FetchTelegramFileRequest request{
      .installation = installation,
      .file = {.file_id = "file-id",
               .file_unique_id = "unique-id",
               .file_type = "sticker",
               .file_size = 4,
               .mime_type = "image/webp",
               .file_name = "sticker.webp"},
      .maximum_bytes = 16,
  };
  const auto request_document = nlohmann::json(request);
  EXPECT_EQ(
      request_document.get<obcx::telegram::bot::FetchTelegramFileRequest>()
          .file,
      request.file);

  const obcx::telegram::bot::FetchedTelegramFile fetched{
      .installation = installation,
      .file = request.file,
      .bytes = {0x52, 0x49, 0x46, 0x46},
  };
  EXPECT_EQ(
      nlohmann::json(fetched).get<obcx::telegram::bot::FetchedTelegramFile>(),
      fetched);

  request.file.file_size = 17;
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.installation.surface = SurfaceId{"onebot11.qq"};
  EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, TelegramMediaRejectsWrongTargetAndGroupShape) {
  obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest request{
      .target = {.installation = {.installation_id = "qq-main",
                                  .surface = SurfaceId{"onebot11.qq"}},
                 .native_group_id = "123"},
      .media = {{.type = "photo", .source = "https://example.test/a.jpg"}},
  };
  EXPECT_THROW(request.validate(), std::invalid_argument);

  request.target.installation = {.installation_id = "tg-main",
                                 .surface = SurfaceId{"telegram.bot_api"}};
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.media.clear();
  EXPECT_THROW(request.validate(), std::invalid_argument);
  request.media.resize(11, {.type = "photo", .source = "file-id"});
  EXPECT_THROW(request.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, OneBotValuesRejectTelegramAndMalformedNodes) {
  obcx::onebot11::bot::GetOneBotForwardMessageRequest request{
      .installation = {.installation_id = "tg-main",
                       .surface = SurfaceId{"telegram.bot_api"}},
      .forward_id = "forward-7",
  };
  EXPECT_THROW(request.validate(), std::invalid_argument);

  obcx::onebot11::bot::OneBotForwardMessage response{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .forward_id = "forward-7",
      .messages = nlohmann::json::array({"not-an-object"}),
  };
  EXPECT_THROW(response.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, CredentialBearingErrorsMustBeRedacted) {
  const std::string unsafe =
      "GET https://api.telegram.org/file/bot123:secret/file.jpg?token=value";
  EXPECT_EQ(obcx::bot::redact_bot_diagnostic(unsafe),
            "[redacted provider diagnostic]");

  BotOperationError error{
      .code = BotOperationErrorCode::TransportFailure,
      .message = unsafe,
      .retryable = true,
      .submission_safety = SubmissionSafety::PossiblySubmitted,
  };
  EXPECT_THROW(error.validate(), std::invalid_argument);
  error.message = obcx::bot::redact_bot_diagnostic(error.message);
  EXPECT_NO_THROW(error.validate());
  const auto document = nlohmann::json(error).dump();
  EXPECT_EQ(document.find("123:secret"), std::string::npos);
  EXPECT_EQ(document.find("token=value"), std::string::npos);
}

TEST(BotOperationTypesTest, MissingRoutesAndInvalidJsonShapesAreRejected) {
  EXPECT_THROW((void)nlohmann::json({{"action", "message.send_group"},
                                     {"message", nlohmann::json::array()}})
                   .get<obcx::bot::SendGroupMessageRequest>(),
               std::invalid_argument);
  EXPECT_THROW(
      (void)nlohmann::json({{"installation_id", "tg-main"}, {"surface", 1}})
          .get<BotInstallationRef>(),
      std::invalid_argument);
  EXPECT_THROW(
      (void)nlohmann::json(
          {{"action", "telegram.media.fetch_file"},
           {"installation",
            {{"installation_id", "tg-main"}, {"surface", "telegram.bot_api"}}},
           {"file", {{"file_id", "id"}, {"file_type", "photo"}}},
           {"maximum_bytes", 0}})
          .get<obcx::telegram::bot::FetchTelegramFileRequest>(),
      std::invalid_argument);
  EXPECT_THROW(
      (void)nlohmann::json({{"ok", true}, {"error", {{"message", "invalid"}}}})
          .get<BotOperationResult<BotMessageRef>>(),
      std::invalid_argument);
}

} // namespace
