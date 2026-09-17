#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"
#include "onebot11/bot/operation_component.hpp"
#include "onebot11/bot/operation_definitions.hpp"
#include "onebot11/bot/operations.hpp"
#include "onebot11/bot/protocol.hpp"
#include "onebot11/bot/transport.hpp"
#include "telegram/bot/command_catalog_component.hpp"
#include "telegram/bot/operation_component.hpp"
#include "telegram/bot/operation_definitions.hpp"
#include "telegram/bot/operations.hpp"
#include "telegram/bot/protocol.hpp"
#include "telegram/bot/transport.hpp"
#include <fstream>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace asio = boost::asio;
using obcx::bot::ActionId;
using obcx::bot::BotInstallationRef;
using obcx::bot::GroupTarget;
using obcx::bot::SurfaceId;
using obcx::core::BotComponent;
using obcx::core::BotOperationEndpoint;
using obcx::core::CapabilityId;
using obcx::core::CapabilityRegistry;
using obcx::core::ComponentDescriptor;
using obcx::core::ComponentId;

template <typename T> auto run(asio::awaitable<T> operation) -> T {
  asio::io_context io;
  auto future = asio::co_spawn(io, std::move(operation), asio::use_future);
  io.run();
  return future.get();
}

class FakeOneBotTransport final : public obcx::core::OneBot11Transport {
public:
  void set_event_callback(EventCallback callback) override {
    callback_ = std::move(callback);
  }
  [[nodiscard]] auto is_connected() const -> bool override { return true; }
  auto send_action(std::string payload, std::uint64_t)
      -> asio::awaitable<std::string> override {
    payloads.push_back(std::move(payload));
    if (responses.empty()) {
      throw std::runtime_error("missing OneBot response");
    }
    auto response = std::move(responses.front());
    responses.pop_front();
    co_return response;
  }

  std::deque<std::string> responses;
  std::vector<std::string> payloads;

private:
  EventCallback callback_;
};

class FakeTelegramTransport final : public obcx::core::TelegramTransport {
public:
  void set_event_callback(EventCallback callback) override {
    callback_ = std::move(callback);
  }
  [[nodiscard]] auto is_connected() const -> bool override { return true; }
  auto send_action(std::string payload, std::uint64_t)
      -> asio::awaitable<std::string> override {
    payloads.push_back(std::move(payload));
    if (responses.empty()) {
      throw std::runtime_error("missing Telegram response");
    }
    auto response = std::move(responses.front());
    responses.pop_front();
    co_return response;
  }
  auto download_file(std::string file_id)
      -> asio::awaitable<std::string> override {
    downloaded_file_id = std::move(file_id);
    co_return download_url;
  }
  auto download_file_content(std::string_view url, std::size_t maximum_bytes)
      -> asio::awaitable<std::string> override {
    downloaded_url = url;
    download_maximum_bytes = maximum_bytes;
    co_return file_content;
  }
  auto upload_photo(std::string_view chat_id,
                    const obcx::core::TelegramMediaUpload &photo,
                    std::string_view, std::optional<std::int64_t>)
      -> asio::awaitable<std::string> override {
    ++photo_upload_calls;
    upload_chat = chat_id;
    upload_count = 1;
    upload_bytes = photo.data.size();
    co_return photo_upload_response;
  }
  auto upload_media_group(
      std::string_view chat_id,
      const std::vector<obcx::core::TelegramMediaUpload> &media,
      std::string_view, std::optional<std::int64_t>, std::optional<std::string>,
      const std::vector<obcx::core::TelegramTextEntity> &)
      -> asio::awaitable<std::string> override {
    ++upload_calls;
    upload_chat = chat_id;
    upload_count = media.size();
    upload_bytes = 0;
    for (const auto &item : media) {
      upload_bytes += item.data.size();
    }
    co_return upload_response;
  }

  std::deque<std::string> responses;
  std::vector<std::string> payloads;
  std::string download_url = "https://api.telegram.test/file/bot-redacted/path";
  std::string file_content = "file-bytes";
  std::string photo_upload_response =
      R"({"ok":true,"result":{"message_id":70}})";
  std::string upload_response =
      R"({"ok":true,"result":[{"message_id":71},{"message_id":72}]})";
  std::string downloaded_file_id;
  std::string downloaded_url;
  std::size_t download_maximum_bytes{};
  std::string upload_chat;
  std::size_t upload_count{};
  std::size_t upload_calls{};
  std::size_t photo_upload_calls{};
  std::size_t upload_bytes{};

private:
  EventCallback callback_;
};

class FakeOneBotTransportComponent final : public BotComponent {
public:
  explicit FakeOneBotTransportComponent(
      std::shared_ptr<FakeOneBotTransport> transport)
      : transport_(std::move(transport)) {}

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    return {.id = ComponentId{"onebot11.transport.http"},
            .provides = {CapabilityId{"onebot11.transport"}},
            .required = {CapabilityId{"onebot11.protocol"}}};
  }
  void install_capabilities(CapabilityRegistry &registry) override {
    registry.install<obcx::core::OneBot11Transport>(
        ComponentId{"onebot11.transport.http"},
        CapabilityId{"onebot11.transport"}, transport_);
  }
  void prepare(const CapabilityRegistry &) override {}
  void start() override {}
  void stop() override {}

private:
  std::shared_ptr<FakeOneBotTransport> transport_;
};

class FakeTelegramTransportComponent final : public BotComponent {
public:
  explicit FakeTelegramTransportComponent(
      std::shared_ptr<FakeTelegramTransport> transport)
      : transport_(std::move(transport)) {}

  [[nodiscard]] auto descriptor() const -> ComponentDescriptor override {
    return {.id = ComponentId{"telegram.transport.http"},
            .provides = {CapabilityId{"telegram.transport"}},
            .required = {CapabilityId{"telegram.protocol"}}};
  }
  void install_capabilities(CapabilityRegistry &registry) override {
    registry.install<obcx::core::TelegramTransport>(
        ComponentId{"telegram.transport.http"},
        CapabilityId{"telegram.transport"}, transport_);
  }
  void prepare(const CapabilityRegistry &) override {}
  void start() override {}
  void stop() override {}

private:
  std::shared_ptr<FakeTelegramTransport> transport_;
};

auto text_message() -> obcx::common::Message {
  return {{.type = "text", .data = {{"text", "hello"}}}};
}

TEST(BotOperationComponentTest,
     OneBotNativeEndpointExecutesAllDeclaredActions) {
  auto transport = std::make_shared<FakeOneBotTransport>();
  transport->responses = {
      R"({"status":"ok","retcode":0,"data":{"message_id":11}})",
      R"({"status":"ok","retcode":0,"data":{"message_id":12}})",
      R"({"status":"ok","retcode":0,"data":null})",
      R"({"status":"ok","retcode":0,"data":{"user_id":22,"nickname":"member"}})",
      R"({"status":"ok","retcode":0,"data":{"messages":[{"sender":{"nickname":"a"},"content":"hello"}]}})",
      R"({"status":"ok","retcode":0,"data":{"url":"https://example.test/group"}})",
      R"({"status":"ok","retcode":0,"data":{"url":"https://example.test/private"}})",
      R"({"status":"ok","retcode":0,"data":null})",
  };
  obcx::core::BotInstallation installation{"qq-main",
                                           obcx::bot::SurfaceId{"onebot11.qq"}};
  installation.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeOneBotTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::OneBot11OperationsComponent>("qq-main"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  ASSERT_NE(endpoint, nullptr);
  const auto onebot_actions = endpoint->declared_actions();
  EXPECT_EQ(
      std::set<ActionId>(onebot_actions.begin(), onebot_actions.end()).size(),
      8U);

  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .native_group_id = "100",
  };
  const auto sent = run(obcx::bot::invoke(
      *endpoint, obcx::bot::SendGroupMessageRequest{
                     .target = target, .message = text_message()}));
  ASSERT_TRUE(sent.ok());
  EXPECT_EQ(sent.value->primary().native_message_id, "11");
  const auto private_sent = run(obcx::bot::invoke(
      *endpoint, obcx::bot::SendPrivateMessageRequest{
                     .target = {.installation = target.installation,
                                .native_user_id = "22"},
                     .message = text_message()}));
  ASSERT_TRUE(private_sent.ok());
  EXPECT_EQ(private_sent.value->primary().native_message_id, "12");
  EXPECT_TRUE(
      run(obcx::bot::invoke(
              *endpoint,
              obcx::bot::DeleteMessageRequest{
                  .message = {.group = target, .native_message_id = "11"}}))
          .ok());
  EXPECT_TRUE(
      run(obcx::bot::invoke(*endpoint,
                            obcx::onebot11::bot::GetOneBotGroupMemberRequest{
                                .target = target, .user_id = "22"}))
          .ok());
  EXPECT_TRUE(
      run(obcx::bot::invoke(*endpoint,
                            obcx::onebot11::bot::GetOneBotForwardMessageRequest{
                                .installation = target.installation,
                                .forward_id = "forward"}))
          .ok());
  EXPECT_TRUE(
      run(obcx::bot::invoke(*endpoint,
                            obcx::onebot11::bot::ResolveOneBotGroupFileRequest{
                                .target = target, .file_id = "group-file"}))
          .ok());
  EXPECT_TRUE(run(obcx::bot::invoke(
                      *endpoint,
                      obcx::onebot11::bot::ResolveOneBotPrivateFileRequest{
                          .installation = target.installation,
                          .user_id = "22",
                          .file_id = "private-file"}))
                  .ok());
  EXPECT_TRUE(run(obcx::bot::invoke(*endpoint,
                                    obcx::onebot11::bot::PokeOneBotGroupRequest{
                                        .target = target, .user_id = "22"}))
                  .ok());
  ASSERT_EQ(transport->payloads.size(), 8U);
  EXPECT_EQ(nlohmann::json::parse(transport->payloads.front()).at("action"),
            "send_group_msg");
}

TEST(BotOperationComponentTest,
     TelegramNativeEndpointExecutesAllCapabilitiesAndBoundsMedia) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  transport->responses = {
      R"({"ok":true,"result":{"message_id":31}})",
      R"({"ok":true,"result":{"message_id":30,"chat":{"id":7}}})",
      R"({"ok":true,"result":true})",
      R"({"ok":true,"result":{"message_id":32}})",
      R"({"ok":true,"result":{"message_id":31}})",
      R"({"ok":true,"result":{"message_id":33}})",
      R"({"ok":true,"result":[{"message_id":34},{"message_id":35}]})",
  };
  obcx::core::BotInstallation installation{
      "tg-main", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-main",
                                                                true));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  ASSERT_NE(endpoint, nullptr);
  EXPECT_EQ(endpoint->declared_actions().size(), 10U);

  const GroupTarget target{
      .installation = {.installation_id = "tg-main",
                       .surface = SurfaceId{"telegram.bot_api"}},
      .native_group_id = "-1001",
  };
  const auto sent = run(obcx::bot::invoke(
      *endpoint, obcx::bot::SendGroupMessageRequest{
                     .target = target, .message = text_message()}));
  ASSERT_TRUE(sent.ok());
  EXPECT_EQ(sent.value->primary().native_message_id, "31");
  const auto private_sent = run(obcx::bot::invoke(
      *endpoint, obcx::bot::SendPrivateMessageRequest{
                     .target = {.installation = target.installation,
                                .native_user_id = "7"},
                     .message = text_message()}));
  ASSERT_TRUE(private_sent.ok());
  EXPECT_EQ(private_sent.value->primary().native_message_id, "30");
  EXPECT_TRUE(
      run(obcx::bot::invoke(
              *endpoint,
              obcx::bot::DeleteMessageRequest{
                  .message = {.group = target, .native_message_id = "31"}}))
          .ok());
  EXPECT_TRUE(run(obcx::bot::invoke(
                      *endpoint,
                      obcx::telegram::bot::SendTelegramTopicMessageRequest{
                          .target = {.group = target, .topic_id = 7},
                          .message = text_message()}))
                  .ok());
  EXPECT_TRUE(
      run(obcx::bot::invoke(
              *endpoint,
              obcx::telegram::bot::EditTelegramMessageTextRequest{
                  .message = {.group = target, .native_message_id = "31"},
                  .text = "edited"}))
          .ok());
  EXPECT_TRUE(
      run(obcx::bot::invoke(
              *endpoint,
              obcx::telegram::bot::SendTelegramPhotoRequest{
                  .target = target, .photo = "file-id", .caption = "caption"}))
          .ok());
  EXPECT_TRUE(run(obcx::bot::invoke(
                      *endpoint,
                      obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest{
                          .target = target,
                          .media = {{.type = "photo", .source = "file-a"},
                                    {.type = "photo", .source = "file-b"}}}))
                  .ok());
  const auto uploaded_photo = run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramPhotoUploadRequest{
                     .target = target,
                     .photo = {.type = "photo",
                               .filename = "thumbnail.webp",
                               .mime_type = "image/webp",
                               .bytes = {1, 2, 3}},
                     .maximum_bytes = 16}));
  ASSERT_TRUE(uploaded_photo.ok());
  EXPECT_EQ(transport->photo_upload_calls, 1U);
  const auto uploaded = run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest{
                     .target = target,
                     .media = {{.type = "photo",
                                .filename = "a.jpg",
                                .mime_type = "image/jpeg",
                                .bytes = {1, 2, 3}},
                               {.type = "photo",
                                .filename = "b.jpg",
                                .mime_type = "image/jpeg",
                                .bytes = {4, 5, 6}}},
                     .maximum_bytes = 16}));
  ASSERT_TRUE(uploaded.ok());
  EXPECT_EQ(transport->upload_chat, "-1001");
  EXPECT_EQ(transport->upload_count, 2U);

  obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest forged_request{
      .target = target,
      .media = {{.type = "photo",
                 .filename = "a.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {1}},
                {.type = "photo",
                 .filename = "b.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = {2}}},
      .reply_to =
          obcx::bot::BotMessageRef{.group = target, .native_message_id = "40"},
      .maximum_bytes = 16};
  auto forged_payload =
      obcx::bot::GatewayCodec<decltype(forged_request)>::encode(forged_request);
  forged_payload["reply_to"]["group"]["installation"]["installation_id"] =
      "another-installation";
  const auto forged_reply =
      run(endpoint->invoke({.installation = target.installation,
                            .action = decltype(forged_request)::action,
                            .payload = std::move(forged_payload)}));
  ASSERT_FALSE(forged_reply.ok());
  EXPECT_EQ(forged_reply.error->submission_safety,
            obcx::bot::SubmissionSafety::DefinitelyNotSubmitted);
  EXPECT_EQ(transport->upload_calls, 1U);

  auto fetched = run(obcx::bot::invoke(
      *endpoint,
      obcx::telegram::bot::FetchTelegramFileRequest{
          .installation = target.installation,
          .file = {.file_id = "telegram-file", .file_type = "document"},
          .maximum_bytes = 32}));
  ASSERT_TRUE(fetched.ok());
  EXPECT_EQ(
      std::string(fetched.value->bytes.begin(), fetched.value->bytes.end()),
      "file-bytes");
  EXPECT_EQ(transport->downloaded_file_id, "telegram-file");
  EXPECT_EQ(transport->download_maximum_bytes, 32U);

  transport->file_content = std::string(33, 'x');
  fetched = run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::FetchTelegramFileRequest{
                     .installation = target.installation,
                     .file = {.file_id = "oversized", .file_type = "document"},
                     .maximum_bytes = 32}));
  ASSERT_FALSE(fetched.ok());
  EXPECT_EQ(fetched.error->code,
            obcx::bot::BotOperationErrorCode::MediaTooLarge);
}

TEST(BotOperationComponentTest,
     TelegramUploadActionIsAbsentWithoutUploaderCapability) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  obcx::core::BotInstallation installation{
      "tg-reduced", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-reduced",
                                                                false));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  const auto actions = endpoint->declared_actions();
  EXPECT_EQ(actions.size(), 8U);
  EXPECT_EQ(
      std::ranges::find(
          actions,
          obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest::action),
      actions.end());
  EXPECT_EQ(
      std::ranges::find(
          actions, obcx::telegram::bot::SendTelegramPhotoUploadRequest::action),
      actions.end());
}

TEST(BotOperationComponentTest,
     TelegramSendOperationsEnforceProviderResultShapesAndCounts) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  transport->responses = {
      R"({"ok":true,"result":{"message_id":30,"chat":{"id":8}}})",
      R"({"ok":true,"result":[{"message_id":31}]})",
      R"({"ok":true,"result":[{"message_id":32}]})",
      R"({"ok":true,"result":{"message_id":33}})",
  };
  transport->photo_upload_response =
      R"({"ok":true,"result":[{"message_id":34}]})";
  transport->upload_response = R"({"ok":true,"result":[{"message_id":34}]})";
  obcx::core::BotInstallation installation{
      "tg-shapes", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-shapes",
                                                                true));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  const GroupTarget target{
      .installation = {.installation_id = "tg-shapes",
                       .surface = SurfaceId{"telegram.bot_api"}},
      .native_group_id = "-1001",
  };
  const auto expect_malformed = [](const auto &result) {
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error->code,
              obcx::bot::BotOperationErrorCode::MalformedResponse);
    EXPECT_EQ(result.error->submission_safety,
              obcx::bot::SubmissionSafety::PossiblySubmitted);
  };

  expect_malformed(run(obcx::bot::invoke(
      *endpoint, obcx::bot::SendPrivateMessageRequest{
                     .target = {.installation = target.installation,
                                .native_user_id = "7"},
                     .message = text_message()})));
  expect_malformed(run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramTopicMessageRequest{
                     .target = {.group = target, .topic_id = 7},
                     .message = text_message()})));
  expect_malformed(run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramPhotoRequest{
                     .target = target, .photo = "file-id"})));
  expect_malformed(run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest{
                     .target = target,
                     .media = {{.type = "photo", .source = "file-a"},
                               {.type = "photo", .source = "file-b"}}})));
  expect_malformed(run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramPhotoUploadRequest{
                     .target = target,
                     .photo = {.type = "photo",
                               .filename = "thumbnail.webp",
                               .mime_type = "image/webp",
                               .bytes = {1}},
                     .maximum_bytes = 16})));
  expect_malformed(run(obcx::bot::invoke(
      *endpoint, obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest{
                     .target = target,
                     .media = {{.type = "photo",
                                .filename = "a.jpg",
                                .mime_type = "image/jpeg",
                                .bytes = {1}},
                               {.type = "photo",
                                .filename = "b.jpg",
                                .mime_type = "image/jpeg",
                                .bytes = {2}}},
                     .maximum_bytes = 16})));
}

TEST(BotOperationComponentTest,
     TelegramCommandCatalogUsesItsExplicitInstallationCapability) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  transport->responses.push_back(R"({"ok":true,"result":true})");
  obcx::core::BotInstallation installation{
      "tg-commands", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramCommandCatalogComponent>());
  installation.start();
  const auto catalog =
      installation.capability<obcx::core::CommandCatalogPublisher>(
          CapabilityId{"telegram.command-catalog"});
  const auto result = run(
      catalog->publish({{.name = "chat", .description = "Chat with the bot"}}));
  EXPECT_TRUE(result.succeeded);
  ASSERT_EQ(transport->payloads.size(), 1U);
  const auto payload = nlohmann::json::parse(transport->payloads.front());
  EXPECT_EQ(payload.at("method"), "setMyCommands");
  EXPECT_EQ(payload.at("commands").at(0).at("command"), "chat");
}

template <typename Request>
void bind_fixture_installation(Request &request,
                               const obcx::bot::BotInstallationRef &owner) {
  if constexpr (requires { request.target.installation; }) {
    request.target.installation = owner;
  } else if constexpr (requires { request.target.group.installation; }) {
    request.target.group.installation = owner;
  } else if constexpr (requires { request.message.group.installation; }) {
    request.message.group.installation = owner;
  } else {
    request.installation = owner;
  }
}

auto production_operation_fixture() -> obcx::bot::Json {
  std::ifstream input{OBCX_BOT_GOLDEN_PATH};
  if (!input) {
    throw std::runtime_error("cannot open operation golden fixture");
  }
  return obcx::bot::Json::parse(input);
}

TEST(BotOperationComponentTest, BoundedPhotoReachesMultipartTransport) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  obcx::core::BotInstallation installation{
      "tg-main", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-main",
                                                                true));
  installation.start();
  auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  obcx::telegram::bot::SendTelegramPhotoUploadRequest request{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "-1001"},
      .photo = {.type = "photo",
                .filename = "thumbnail.webp",
                .mime_type = "image/webp",
                .bytes = {0x52, 0x49, 0x46, 0x46}},
      .caption = "caption",
      .topic_id = 7,
      .maximum_bytes = 1024};
  const auto result = run(obcx::bot::invoke(*endpoint, std::move(request)));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(transport->photo_upload_calls, 1U);
  EXPECT_EQ(transport->upload_bytes, 4U);
  EXPECT_EQ(transport->upload_count, 1U);
  EXPECT_EQ(result.value->messages.size(), 1U);
}

TEST(BotOperationComponentTest, BoundedBinaryMediaReachesMultipartTransport) {
  auto transport = std::make_shared<FakeTelegramTransport>();
  obcx::core::BotInstallation installation{
      "tg-main", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-main",
                                                                true));
  installation.start();
  auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  constexpr std::size_t item_bytes = 2U * 1024U * 1024U;
  obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest request{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "-1001"},
      .media = {{.type = "photo",
                 .filename = "a.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = std::vector<std::uint8_t>(item_bytes, 127)},
                {.type = "photo",
                 .filename = "b.jpg",
                 .mime_type = "image/jpeg",
                 .bytes = std::vector<std::uint8_t>(item_bytes, 255)}},
      .maximum_bytes = 2U * item_bytes};
  const auto result = run(obcx::bot::invoke(*endpoint, std::move(request)));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(transport->upload_bytes, 2U * item_bytes);
  EXPECT_EQ(transport->upload_count, 2U);
  EXPECT_EQ(result.value->messages.size(), 2U);
}

TEST(BotOperationComponentTest,
     EveryOneBotActionRejectsProviderFailureAndMalformedResponse) {
  const auto fixture = production_operation_fixture();
  auto transport = std::make_shared<FakeOneBotTransport>();
  obcx::core::BotInstallation installation{"qq-main",
                                           obcx::bot::SurfaceId{"onebot11.qq"}};
  installation.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeOneBotTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::OneBot11OperationsComponent>("qq-main"));
  installation.start();
  auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  obcx::onebot11::bot::for_each_operation([&](const auto &definition) {
    using Request =
        typename std::remove_cvref_t<decltype(definition)>::request_type;
    using Traits = obcx::bot::OperationTraits<Request>;
    auto request = fixture.at("operations")
                       .at(Request::action.value())
                       .at("request")
                       .template get<Request>();
    bind_fixture_installation(request, endpoint->installation());
    for (const auto malformed : {false, true}) {
      SCOPED_TRACE(Request::action.value());
      transport->responses.push_back(
          malformed
              ? "not-json"
              : R"({"status":"failed","retcode":-1,"message":"try later"})");
      const auto result = run(obcx::bot::invoke(*endpoint, request));
      ASSERT_FALSE(result.ok());
      EXPECT_EQ(result.error->code,
                malformed ? obcx::bot::BotOperationErrorCode::MalformedResponse
                          : obcx::bot::BotOperationErrorCode::ProviderRejected);
      EXPECT_EQ(result.error->submission_safety,
                malformed && Traits::side_effecting
                    ? obcx::bot::SubmissionSafety::PossiblySubmitted
                    : obcx::bot::SubmissionSafety::DefinitelyNotSubmitted);
    }
  });
  EXPECT_EQ(transport->payloads.size(), 16U);
}

TEST(BotOperationComponentTest,
     EveryTelegramActionRejectsProviderFailureAndMalformedResponse) {
  const auto fixture = production_operation_fixture();
  auto transport = std::make_shared<FakeTelegramTransport>();
  obcx::core::BotInstallation installation{
      "tg-main", obcx::bot::SurfaceId{"telegram.bot_api"}};
  installation.add_component(
      std::make_unique<obcx::core::TelegramProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeTelegramTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::TelegramMediaUploadComponent>());
  installation.add_component(
      std::make_unique<obcx::core::TelegramOperationsComponent>("tg-main",
                                                                true));
  installation.start();
  auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  obcx::telegram::bot::for_each_operation(true, [&](const auto &definition) {
    using Request =
        typename std::remove_cvref_t<decltype(definition)>::request_type;
    using Traits = obcx::bot::OperationTraits<Request>;
    auto request = fixture.at("operations")
                       .at(Request::action.value())
                       .at("request")
                       .template get<Request>();
    bind_fixture_installation(request, endpoint->installation());
    for (const auto malformed : {false, true}) {
      SCOPED_TRACE(Request::action.value());
      const std::string response =
          malformed
              ? "not-json"
              : R"({"ok":false,"error_code":429,"description":"try later"})";
      if constexpr (std::is_same_v<
                        Request,
                        obcx::telegram::bot::FetchTelegramFileRequest>) {
        transport->download_url = malformed ? "https://example.test/file" : "";
        transport->file_content.clear();
      } else if constexpr (std::is_same_v<Request,
                                          obcx::telegram::bot::
                                              SendTelegramPhotoUploadRequest>) {
        transport->photo_upload_response = response;
      } else if constexpr (std::is_same_v<
                               Request,
                               obcx::telegram::bot::
                                   SendTelegramMediaGroupUploadsRequest>) {
        transport->upload_response = response;
      } else {
        transport->responses.push_back(response);
      }
      const auto result = run(obcx::bot::invoke(*endpoint, request));
      ASSERT_FALSE(result.ok());
      EXPECT_EQ(result.error->code,
                malformed ? obcx::bot::BotOperationErrorCode::MalformedResponse
                          : obcx::bot::BotOperationErrorCode::ProviderRejected);
      EXPECT_EQ(result.error->submission_safety,
                malformed && Traits::side_effecting
                    ? obcx::bot::SubmissionSafety::PossiblySubmitted
                    : obcx::bot::SubmissionSafety::DefinitelyNotSubmitted);
    }
  });
  EXPECT_EQ(transport->payloads.size(), 14U);
  EXPECT_EQ(transport->upload_calls, 2U);
  EXPECT_EQ(transport->photo_upload_calls, 2U);
}

TEST(BotOperationComponentTest,
     NativeEndpointsConservativelyRejectMalformedProviderSuccess) {
  auto transport = std::make_shared<FakeOneBotTransport>();
  transport->responses.push_back(R"({"status":"ok","retcode":0,"data":null})");
  obcx::core::BotInstallation installation{"qq-main",
                                           obcx::bot::SurfaceId{"onebot11.qq"}};
  installation.add_component(
      std::make_unique<obcx::core::OneBot11ProtocolComponent>());
  installation.add_component(
      std::make_unique<FakeOneBotTransportComponent>(transport));
  installation.add_component(
      std::make_unique<obcx::core::OneBot11OperationsComponent>("qq-main"));
  installation.start();
  const auto endpoint = installation.capability<BotOperationEndpoint>(
      CapabilityId{"bot.operations"});
  const GroupTarget target{
      .installation = {.installation_id = "qq-main",
                       .surface = SurfaceId{"onebot11.qq"}},
      .native_group_id = "100",
  };
  const auto result = run(obcx::bot::invoke(
      *endpoint, obcx::bot::SendGroupMessageRequest{
                     .target = target, .message = text_message()}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code,
            obcx::bot::BotOperationErrorCode::MalformedResponse);
  EXPECT_EQ(result.error->submission_safety,
            obcx::bot::SubmissionSafety::PossiblySubmitted);
}

} // namespace
