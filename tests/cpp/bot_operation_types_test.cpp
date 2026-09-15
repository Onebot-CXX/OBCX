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

template <typename T> void expect_stable_round_trip(const T &value) {
  const auto first = nlohmann::json(value);
  const auto decoded = first.get<T>();
  const auto second = nlohmann::json(decoded);
  EXPECT_EQ(second.dump(), first.dump());
}

TEST(BotOperationTypesTest, ProductionWireIdsDoNotDependOnAnEnumOrdinal) {
  const std::vector<std::string> ids{"message.send_group",
                                     "message.send_private",
                                     "message.delete",
                                     "telegram.message.send_topic",
                                     "telegram.message.edit_text",
                                     "telegram.media.send_photo",
                                     "telegram.media.send_group_urls",
                                     "telegram.media.send_group_uploads",
                                     "telegram.media.fetch_file",
                                     "onebot11.group_member.get",
                                     "onebot11.forward_message.get",
                                     "onebot11.group_file.resolve",
                                     "onebot11.private_file.resolve",
                                     "onebot11.group.poke"};
  EXPECT_EQ((std::set<std::string>{ids.begin(), ids.end()}).size(), 14U);
  for (const auto &id : ids) {
    const ActionId action{id};
    EXPECT_EQ(nlohmann::json(action).get<ActionId>(), action);
  }
  EXPECT_NO_THROW((void)ActionId{"message.history"});
}

TEST(BotOperationTypesTest, ScopedReferencesRoundTripDeterministically) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "telegram-main",
                                 .surface = SurfaceId{"telegram.bot_api"}},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const nlohmann::json document = message;
  EXPECT_EQ(
      document.dump(),
      R"({"group":{"installation":{"installation_id":"telegram-main","surface":"telegram.bot_api"},"native_group_id":"-1001"},"native_message_id":"42"})");
  EXPECT_EQ(document.get<BotMessageRef>(), message);

  auto other = message;
  other.group.installation.installation_id = "telegram-secondary";
  EXPECT_NE(other, message);

  const PrivateMessageRef private_message{
      .target = {.installation = {.installation_id = "telegram-main",
                                  .surface = SurfaceId{"telegram.bot_api"}},
                 .native_user_id = "-1001"},
      .native_message_id = "42",
  };
  const nlohmann::json private_document = private_message;
  EXPECT_EQ(private_document.get<PrivateMessageRef>(), private_message);
  EXPECT_EQ(private_message.target.native_user_id,
            message.group.native_group_id);
  EXPECT_NE(nlohmann::json(private_message), nlohmann::json(message));
}

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

TEST(BotOperationTypesTest, ResultRoundTripsSuccessAndFailure) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "qq-main",
                                 .surface = SurfaceId{"onebot11.qq"}},
                .native_group_id = "123"},
      .native_message_id = "456",
  };
  const auto success = BotOperationResult<BotMessageRef>::success(message);
  const auto success_document = nlohmann::json(success);
  EXPECT_TRUE(success_document.at("ok"));
  EXPECT_EQ(success_document.get<BotOperationResult<BotMessageRef>>(), success);

  const BotOperationError error{
      .code = BotOperationErrorCode::ProviderRejected,
      .message = "provider rejected request",
      .provider_code = "429",
      .retry_after = std::chrono::milliseconds{1500},
      .retryable = true,
      .submission_safety = SubmissionSafety::DefinitelyNotSubmitted,
  };
  const auto failure = BotOperationResult<BotMessageRef>::failure(error);
  const auto failure_document = nlohmann::json(failure);
  EXPECT_FALSE(failure_document.at("ok"));
  EXPECT_EQ(failure_document.get<BotOperationResult<BotMessageRef>>(), failure);
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

TEST(BotOperationTypesTest, CommonAndTelegramMessageRequestsRoundTrip) {
  const GroupTarget qq_target{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .native_group_id = "123",
  };
  const obcx::common::Message payload = {
      {.type = "reply", .data = {{"id", "41"}}},
      {.type = "text", .data = {{"text", "hello"}}},
  };
  const obcx::bot::SendGroupMessageRequest send{.target = qq_target,
                                                .message = payload};
  const auto send_document = nlohmann::json(send);
  EXPECT_EQ(send_document.at("action"), "message.send_group");
  const auto decoded_send =
      send_document.get<obcx::bot::SendGroupMessageRequest>();
  EXPECT_EQ(nlohmann::json(decoded_send), send_document);

  const obcx::bot::SendPrivateMessageRequest private_send{
      .target = {.installation = qq_target.installation,
                 .native_user_id = "456"},
      .message = payload,
  };
  const auto private_document = nlohmann::json(private_send);
  EXPECT_EQ(private_document.at("action"), "message.send_private");
  EXPECT_EQ(nlohmann::json(
                private_document.get<obcx::bot::SendPrivateMessageRequest>()),
            private_document);

  const obcx::telegram::bot::SendTelegramTopicMessageRequest topic{
      .target = {.group = {.installation = {.installation_id = "tg-main",
                                            .surface =
                                                SurfaceId{"telegram.bot_api"}},
                           .native_group_id = "-1001"},
                 .topic_id = 7},
      .message = payload,
  };
  const auto topic_document = nlohmann::json(topic);
  EXPECT_EQ(topic_document.at("action"), "telegram.message.send_topic");
  EXPECT_EQ(
      nlohmann::json(
          topic_document
              .get<obcx::telegram::bot::SendTelegramTopicMessageRequest>()),
      topic_document);
}

TEST(BotOperationTypesTest, DeleteAndEditRequestsKeepMessageScope) {
  const BotMessageRef telegram_message{
      .group = {.installation = {.installation_id = "tg-main",
                                 .surface = SurfaceId{"telegram.bot_api"}},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const obcx::bot::DeleteMessageRequest deletion{.message = telegram_message};
  const auto delete_document = nlohmann::json(deletion);
  EXPECT_EQ(delete_document.at("message").at("group").at("native_group_id"),
            "-1001");
  EXPECT_EQ(delete_document.at("message").at("native_message_id"), "42");
  EXPECT_EQ(delete_document.get<obcx::bot::DeleteMessageRequest>().message,
            telegram_message);

  const obcx::telegram::bot::EditTelegramMessageTextRequest edit{
      .message = telegram_message,
      .text = "edited",
      .parse_mode = "HTML",
  };
  const auto edit_document = nlohmann::json(edit);
  EXPECT_EQ(
      nlohmann::json(
          edit_document
              .get<obcx::telegram::bot::EditTelegramMessageTextRequest>()),
      edit_document);

  auto wrong_surface = edit;
  wrong_surface.message.group.installation.surface = SurfaceId{"onebot11.qq"};
  EXPECT_THROW(wrong_surface.validate(), std::invalid_argument);
}

TEST(BotOperationTypesTest, SendAndMutationResultsRoundTrip) {
  const BotMessageRef message{
      .group = {.installation = {.installation_id = "tg-main",
                                 .surface = SurfaceId{"telegram.bot_api"}},
                .native_group_id = "-1001"},
      .native_message_id = "42",
  };
  const obcx::bot::SendMessageResult sent{.messages = {message}};
  EXPECT_EQ(nlohmann::json(sent).get<obcx::bot::SendMessageResult>(), sent);
  EXPECT_EQ(sent.primary(), message);

  const PrivateMessageRef private_message{
      .target = {.installation = message.group.installation,
                 .native_user_id = "7"},
      .native_message_id = "43"};
  const obcx::bot::SendPrivateMessageResult private_sent{
      .messages = {private_message}};
  EXPECT_EQ(
      nlohmann::json(private_sent).get<obcx::bot::SendPrivateMessageResult>(),
      private_sent);
  EXPECT_EQ(private_sent.primary(), private_message);

  const obcx::bot::DeleteMessageResult deleted{.message = message};
  EXPECT_EQ(nlohmann::json(deleted).get<obcx::bot::DeleteMessageResult>(),
            deleted);
  const obcx::telegram::bot::EditMessageTextResult edited{.message = message};
  EXPECT_EQ(
      nlohmann::json(edited).get<obcx::telegram::bot::EditMessageTextResult>(),
      edited);
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

TEST(BotOperationTypesTest, TelegramPhotoAndUrlMediaRequestsRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = SurfaceId{"telegram.bot_api"}},
      .native_group_id = "-1001",
  };
  const std::vector<obcx::telegram::bot::TelegramTextEntity> entities = {
      {.type = "bold", .offset = 0, .length = 4},
  };
  const obcx::telegram::bot::SendTelegramPhotoRequest photo{
      .target = target,
      .photo = "telegram-file-id",
      .caption = "test",
      .caption_entities = entities,
  };
  const auto photo_document = nlohmann::json(photo);
  EXPECT_EQ(
      nlohmann::json(
          photo_document.get<obcx::telegram::bot::SendTelegramPhotoRequest>()),
      photo_document);

  const BotMessageRef reply{.group = target, .native_message_id = "40"};
  const obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest media{
      .target = target,
      .media = {{.type = "photo", .source = "https://example.test/a.jpg"},
                {.type = "photo", .source = "telegram-file-id"}},
      .caption = "album",
      .topic_id = 7,
      .reply_to = reply,
      .caption_entities = entities,
  };
  const auto media_document = nlohmann::json(media);
  EXPECT_EQ(
      nlohmann::json(
          media_document
              .get<obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest>()),
      media_document);
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

TEST(BotOperationTypesTest, OneBotMemberAndForwardValuesRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .native_group_id = "123",
  };
  const obcx::onebot11::bot::GetOneBotGroupMemberRequest request{
      .target = target,
      .user_id = "456",
      .no_cache = false,
  };
  const auto request_document = nlohmann::json(request);
  EXPECT_EQ(nlohmann::json(
                request_document
                    .get<obcx::onebot11::bot::GetOneBotGroupMemberRequest>()),
            request_document);

  const obcx::onebot11::bot::OneBotGroupMember member{
      .target = target,
      .user_id = "456",
      .nickname = "nickname",
      .card = "group card",
      .title = "title",
  };
  EXPECT_EQ(
      nlohmann::json(member).get<obcx::onebot11::bot::OneBotGroupMember>(),
      member);

  const obcx::onebot11::bot::OneBotForwardMessage forward{
      .installation = target.installation,
      .forward_id = "forward-7",
      .messages = nlohmann::json::array(
          {{{"sender", {{"nickname", "alice"}}},
            {"content",
             nlohmann::json::array(
                 {{{"type", "text"}, {"data", {{"text", "hello"}}}}})}}}),
  };
  EXPECT_EQ(
      nlohmann::json(forward).get<obcx::onebot11::bot::OneBotForwardMessage>(),
      forward);
}

TEST(BotOperationTypesTest, OneBotFileAndPokeValuesRoundTrip) {
  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .native_group_id = "123",
  };
  const obcx::onebot11::bot::ResolveOneBotGroupFileRequest group_request{
      .target = target,
      .file_id = "group-file",
  };
  EXPECT_EQ(nlohmann::json(group_request)
                .get<obcx::onebot11::bot::ResolveOneBotGroupFileRequest>()
                .file_id,
            "group-file");
  const obcx::onebot11::bot::ResolvedOneBotGroupFile group_file{
      .target = target,
      .file_id = "group-file",
      .url = "https://example.test/group-file",
  };
  EXPECT_EQ(nlohmann::json(group_file)
                .get<obcx::onebot11::bot::ResolvedOneBotGroupFile>(),
            group_file);

  const obcx::onebot11::bot::ResolveOneBotPrivateFileRequest private_request{
      .installation = target.installation,
      .user_id = "456",
      .file_id = "private-file",
  };
  EXPECT_EQ(nlohmann::json(private_request)
                .get<obcx::onebot11::bot::ResolveOneBotPrivateFileRequest>()
                .user_id,
            "456");
  const obcx::onebot11::bot::ResolvedOneBotPrivateFile private_file{
      .installation = target.installation,
      .user_id = "456",
      .file_id = "private-file",
      .url = "https://example.test/private-file",
  };
  EXPECT_EQ(nlohmann::json(private_file)
                .get<obcx::onebot11::bot::ResolvedOneBotPrivateFile>(),
            private_file);

  const obcx::onebot11::bot::PokeOneBotGroupRequest poke{
      .target = target,
      .user_id = "456",
  };
  EXPECT_EQ(nlohmann::json(poke)
                .get<obcx::onebot11::bot::PokeOneBotGroupRequest>()
                .target,
            target);
  const obcx::onebot11::bot::OneBotGroupPokeResult result{
      .target = target,
      .user_id = "456",
  };
  EXPECT_EQ(
      nlohmann::json(result).get<obcx::onebot11::bot::OneBotGroupPokeResult>(),
      result);
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

TEST(BotOperationTypesTest, EveryClosedActionRequestHasStableJson) {
  const BotInstallationRef telegram{.installation_id = "tg-main",
                                    .surface = SurfaceId{"telegram.bot_api"}};
  const BotInstallationRef onebot{.installation_id = "qq-main",
                                  .surface = SurfaceId{"onebot11.qq"}};
  const GroupTarget telegram_group{.installation = telegram,
                                   .native_group_id = "-1001"};
  const GroupTarget onebot_group{.installation = onebot,
                                 .native_group_id = "123"};
  const BotMessageRef telegram_message{.group = telegram_group,
                                       .native_message_id = "42"};
  const BotMessageRef onebot_message{.group = onebot_group,
                                     .native_message_id = "43"};
  const obcx::common::Message text = {
      {.type = "text", .data = {{"text", "hello"}}}};

  expect_stable_round_trip(obcx::bot::SendGroupMessageRequest{
      .target = onebot_group, .message = text});
  expect_stable_round_trip(obcx::bot::SendPrivateMessageRequest{
      .target = {.installation = onebot, .native_user_id = "456"},
      .message = text});
  expect_stable_round_trip(
      obcx::bot::DeleteMessageRequest{.message = onebot_message});
  expect_stable_round_trip(obcx::telegram::bot::SendTelegramTopicMessageRequest{
      .target = {.group = telegram_group, .topic_id = 7}, .message = text});
  expect_stable_round_trip(obcx::telegram::bot::EditTelegramMessageTextRequest{
      .message = telegram_message, .text = "edited", .parse_mode = "HTML"});
  expect_stable_round_trip(obcx::telegram::bot::SendTelegramPhotoRequest{
      .target = telegram_group, .photo = "file-id"});
  expect_stable_round_trip(
      obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest{
          .target = telegram_group,
          .media = {{.type = "photo", .source = "https://example.test/a"},
                    {.type = "photo", .source = "https://example.test/b"}}});
  expect_stable_round_trip(
      obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest{
          .target = telegram_group,
          .media = {{.type = "photo",
                     .filename = "a.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {1, 2, 3}},
                    {.type = "photo",
                     .filename = "b.jpg",
                     .mime_type = "image/jpeg",
                     .bytes = {4, 5, 6}}},
          .maximum_bytes = 16});
  expect_stable_round_trip(obcx::telegram::bot::FetchTelegramFileRequest{
      .installation = telegram,
      .file = {.file_id = "file-id", .file_type = "photo"},
      .maximum_bytes = 16});
  expect_stable_round_trip(obcx::onebot11::bot::GetOneBotGroupMemberRequest{
      .target = onebot_group, .user_id = "456"});
  expect_stable_round_trip(obcx::onebot11::bot::GetOneBotForwardMessageRequest{
      .installation = onebot, .forward_id = "forward-id"});
  expect_stable_round_trip(obcx::onebot11::bot::ResolveOneBotGroupFileRequest{
      .target = onebot_group, .file_id = "file-id"});
  expect_stable_round_trip(obcx::onebot11::bot::ResolveOneBotPrivateFileRequest{
      .installation = onebot, .user_id = "456", .file_id = "file-id"});
  expect_stable_round_trip(obcx::onebot11::bot::PokeOneBotGroupRequest{
      .target = onebot_group, .user_id = "456"});
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

TEST(BotOperationTypesTest, SurfaceCompatibilityBelongsToOperationTraits) {
  using Common = obcx::bot::OperationTraits<obcx::bot::SendGroupMessageRequest>;
  using Private =
      obcx::bot::OperationTraits<obcx::bot::SendPrivateMessageRequest>;
  using Fetch =
      obcx::bot::OperationTraits<obcx::telegram::bot::FetchTelegramFileRequest>;
  using Poke =
      obcx::bot::OperationTraits<obcx::onebot11::bot::PokeOneBotGroupRequest>;
  EXPECT_TRUE(Common::supports_surface(SurfaceId{"telegram.bot_api"}));
  EXPECT_TRUE(Common::supports_surface(SurfaceId{"onebot11.qq"}));
  EXPECT_TRUE(Private::supports_surface(SurfaceId{"telegram.bot_api"}));
  EXPECT_TRUE(Private::supports_surface(SurfaceId{"onebot11.qq"}));
  EXPECT_TRUE(Fetch::supports_surface(SurfaceId{"telegram.bot_api"}));
  EXPECT_FALSE(Fetch::supports_surface(SurfaceId{"onebot11.qq"}));
  EXPECT_TRUE(Poke::supports_surface(SurfaceId{"onebot11.qq"}));
  EXPECT_FALSE(Poke::supports_surface(SurfaceId{"telegram.bot_api"}));
}

} // namespace
