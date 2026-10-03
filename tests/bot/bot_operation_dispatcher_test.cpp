#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/operation_handler.hpp"
#include "core/bot/typed_operation.hpp"
#include "network/http_client.hpp"
#include "onebot11/bot/operations.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace asio = boost::asio;
using obcx::bot::ActionId;
using obcx::bot::BotInstallationRef;
using obcx::bot::BotOperationErrorCode;
using obcx::bot::BotOperationResult;
using obcx::bot::GroupTarget;
using obcx::bot::SendGroupMessageRequest;
using obcx::bot::SendMessageResult;
using obcx::bot::SubmissionSafety;
using obcx::bot::SurfaceId;
using obcx::onebot11::bot::GetOneBotGroupMemberRequest;
using obcx::onebot11::bot::OneBotGroupMember;

auto known_surface(const SurfaceId &surface) -> bool {
  return surface == SurfaceId{"telegram.bot_api"} ||
         surface == SurfaceId{"onebot11.qq"};
}

class RecordingEndpoint final
    : public std::enable_shared_from_this<RecordingEndpoint> {
public:
  RecordingEndpoint(BotInstallationRef installation,
                    std::vector<ActionId> actions)
      : installation_(std::move(installation)), actions_(std::move(actions)) {}

  [[nodiscard]] auto installation() const -> BotInstallationRef {
    return installation_;
  }
  [[nodiscard]] auto declared_actions() const -> std::vector<ActionId> {
    return actions_;
  }

  auto execute(const SendGroupMessageRequest &request)
      -> asio::awaitable<BotOperationResult<SendMessageResult>> {
    ++send_calls;
    if (send_http_error_state) {
      throw obcx::network::HttpClientError(send_exception,
                                           *send_http_error_state);
    }
    if (!send_exception.empty()) {
      throw std::runtime_error(send_exception);
    }
    co_return BotOperationResult<SendMessageResult>::success(
        {.messages = {
             {.group = request.target, .native_message_id = "message-7"}}});
  }

  auto execute(const GetOneBotGroupMemberRequest &request)
      -> asio::awaitable<BotOperationResult<OneBotGroupMember>> {
    ++member_calls;
    if (!member_exception.empty()) {
      throw std::runtime_error(member_exception);
    }
    co_return BotOperationResult<OneBotGroupMember>::success(
        {.target = request.target,
         .user_id = request.user_id,
         .nickname = "member"});
  }

  auto registry() -> std::shared_ptr<obcx::core::OperationRegistry> {
    auto result =
        std::make_shared<obcx::core::OperationRegistry>(installation_);
    for (const auto &action : actions_) {
      if (action == SendGroupMessageRequest::action) {
        result->install(
            obcx::core::OperationDefinition<SendGroupMessageRequest>{{}}.bind(
                obcx::core::bind_operation_handler<SendGroupMessageRequest>(
                    shared_from_this(), obcx::bot::redact_bot_diagnostic)));
      } else if (action == GetOneBotGroupMemberRequest::action) {
        result->install(
            obcx::core::OperationDefinition<GetOneBotGroupMemberRequest>{{}}
                .bind(obcx::core::bind_operation_handler<
                      GetOneBotGroupMemberRequest>(
                    shared_from_this(), obcx::bot::redact_bot_diagnostic)));
      } else {
        throw std::invalid_argument("fixture operation has no handler");
      }
    }
    result->seal(installation_, {});
    return result;
  }

  std::atomic_int send_calls{};
  std::atomic_int member_calls{};
  std::string send_exception;
  std::optional<obcx::network::HttpRequestSubmissionState>
      send_http_error_state;
  std::string member_exception;

private:
  BotInstallationRef installation_;
  std::vector<ActionId> actions_;
};

template <typename T> auto run(asio::awaitable<T> operation) -> T {
  asio::io_context context;
  auto future = asio::co_spawn(context, std::move(operation), asio::use_future);
  context.run();
  return future.get();
}

auto text_message() -> obcx::common::Message {
  return {{.type = "text", .data = {{"text", "hello"}}}};
}

TEST(BotOperationDispatcherTest,
     RegistersExactInstallationsAndRejectsDuplicatesAndInvalidSurfaces) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto telegram = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = SurfaceId{"telegram.bot_api"}},
      std::vector{SendGroupMessageRequest::action});
  dispatcher.register_endpoint(telegram->registry());
  EXPECT_EQ(dispatcher.endpoint_count(), 1U);
  EXPECT_THROW(dispatcher.register_endpoint(telegram->registry()),
               std::invalid_argument);
  EXPECT_THROW(dispatcher.register_endpoint(nullptr), std::invalid_argument);

  auto invalid = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-invalid",
                         .surface = SurfaceId{"qq.official"}},
      std::vector{SendGroupMessageRequest::action});
  EXPECT_THROW(dispatcher.register_endpoint(invalid->registry()),
               std::invalid_argument);
}

TEST(BotOperationDispatcherTest, ClearEndpointsReleasesRegisteredOwnership) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "telegram-main",
                         .surface = SurfaceId{"telegram.bot_api"}},
      std::vector{SendGroupMessageRequest::action});
  const std::weak_ptr<RecordingEndpoint> registered = endpoint;

  dispatcher.register_endpoint(endpoint->registry());
  endpoint.reset();
  ASSERT_FALSE(registered.expired());

  dispatcher.clear_endpoints();
  EXPECT_TRUE(registered.expired());
  EXPECT_EQ(dispatcher.endpoint_count(), 0U);
}

TEST(BotOperationDispatcherTest, ReportsOnlyExactEndpointActions) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  const BotInstallationRef installation{.installation_id = "qq-main",
                                        .surface = SurfaceId{"onebot11.qq"}};
  dispatcher.register_endpoint(
      std::make_shared<RecordingEndpoint>(
          installation, std::vector{SendGroupMessageRequest::action,
                                    GetOneBotGroupMemberRequest::action})
          ->registry());
  const auto supported = dispatcher.supported_actions(installation);
  ASSERT_TRUE(supported.ok());
  EXPECT_TRUE(supported.value->supports(SendGroupMessageRequest::action));
  EXPECT_TRUE(supported.value->supports(GetOneBotGroupMemberRequest::action));
  EXPECT_FALSE(
      supported.value->supports(obcx::bot::DeleteMessageRequest::action));
}

TEST(BotOperationDispatcherTest, DispatchesByExactInstallation) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = SurfaceId{"onebot11.qq"}},
      std::vector{SendGroupMessageRequest::action});
  dispatcher.register_endpoint(endpoint->registry());
  const auto result = run(obcx::bot::invoke(
      dispatcher, SendGroupMessageRequest{
                      .target = {.installation = endpoint->installation(),
                                 .native_group_id = "123"},
                      .message = text_message()}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value->primary().native_message_id, "message-7");
  EXPECT_EQ(endpoint->send_calls.load(), 1);
}

TEST(BotOperationDispatcherTest, ObservesOnlySuccessfulOperations) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = SurfaceId{"telegram.bot_api"}},
      std::vector{SendGroupMessageRequest::action});
  dispatcher.register_endpoint(endpoint->registry());
  std::vector<obcx::core::SuccessfulBotOperation> observed;
  dispatcher.set_success_handler(
      [&observed](obcx::core::SuccessfulBotOperation operation)
          -> asio::awaitable<void> {
        observed.push_back(std::move(operation));
        co_return;
      });

  const SendGroupMessageRequest request{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "chat"},
      .message = text_message(),
  };
  const auto succeeded = run(obcx::bot::invoke(dispatcher, request));
  ASSERT_TRUE(succeeded.ok());
  ASSERT_EQ(observed.size(), 1U);
  EXPECT_EQ(observed.front().installation, endpoint->installation());
  EXPECT_EQ(observed.front().action, SendGroupMessageRequest::action);

  endpoint->send_exception = "send failed";
  const auto failed = run(obcx::bot::invoke(dispatcher, request));
  ASSERT_FALSE(failed.ok());
  EXPECT_EQ(observed.size(), 1U);
}

TEST(BotOperationDispatcherTest, MissingWrongSurfaceAndUnsupportedDoNoIo) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = SurfaceId{"onebot11.qq"}},
      std::vector{SendGroupMessageRequest::action});
  dispatcher.register_endpoint(endpoint->registry());
  SendGroupMessageRequest request{
      .target = {.installation = {.installation_id = "missing",
                                  .surface = SurfaceId{"onebot11.qq"}},
                 .native_group_id = "123"},
      .message = text_message(),
  };
  auto result = run(obcx::bot::invoke(dispatcher, request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::RouteNotFound);

  request.target.installation = {.installation_id = "qq-main",
                                 .surface = SurfaceId{"telegram.bot_api"}};
  result = run(obcx::bot::invoke(dispatcher, request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::SurfaceMismatch);

  const auto unsupported = run(obcx::bot::invoke(
      dispatcher, obcx::onebot11::bot::GetOneBotGroupMemberRequest{
                      .target = {.installation = endpoint->installation(),
                                 .native_group_id = "123"},
                      .user_id = "456"}));
  ASSERT_FALSE(unsupported.ok());
  EXPECT_EQ(unsupported.error->code, BotOperationErrorCode::UnsupportedAction);
  EXPECT_EQ(endpoint->send_calls.load(), 0);
  EXPECT_EQ(endpoint->member_calls.load(), 0);
}

TEST(BotOperationDispatcherTest, SideEffectExceptionIsConservativeAndRedacted) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = SurfaceId{"telegram.bot_api"}},
      std::vector{SendGroupMessageRequest::action});
  endpoint->send_exception =
      "https://api.telegram.org/file/bot123:secret/x?token=value";
  dispatcher.register_endpoint(endpoint->registry());
  const auto result = run(obcx::bot::invoke(
      dispatcher, SendGroupMessageRequest{
                      .target = {.installation = endpoint->installation(),
                                 .native_group_id = "chat"},
                      .message = text_message()}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
  EXPECT_EQ(result.error->message, "[redacted provider diagnostic]");
}

TEST(BotOperationDispatcherTest, TransportSubmissionStateIsPreserved) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "tg-main",
                         .surface = SurfaceId{"telegram.bot_api"}},
      std::vector{SendGroupMessageRequest::action});
  dispatcher.register_endpoint(endpoint->registry());
  const auto request = SendGroupMessageRequest{
      .target = {.installation = endpoint->installation(),
                 .native_group_id = "chat"},
      .message = text_message()};

  endpoint->send_exception = "connection refused";
  endpoint->send_http_error_state =
      obcx::network::HttpRequestSubmissionState::DefinitelyNotSubmitted;
  auto result = run(obcx::bot::invoke(dispatcher, request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::TransportFailure);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);

  endpoint->send_http_error_state =
      obcx::network::HttpRequestSubmissionState::PossiblySubmitted;
  result = run(obcx::bot::invoke(dispatcher, request));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::OutcomeUnknown);
  EXPECT_FALSE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
}

TEST(BotOperationDispatcherTest, ReadOnlyExceptionIsSafeToRetry) {
  obcx::core::BotOperationDispatcher dispatcher{known_surface};
  auto endpoint = std::make_shared<RecordingEndpoint>(
      BotInstallationRef{.installation_id = "qq-main",
                         .surface = SurfaceId{"onebot11.qq"}},
      std::vector{GetOneBotGroupMemberRequest::action});
  endpoint->member_exception = "temporary lookup failure";
  dispatcher.register_endpoint(endpoint->registry());
  const auto result = run(obcx::bot::invoke(
      dispatcher, GetOneBotGroupMemberRequest{
                      .target = {.installation = endpoint->installation(),
                                 .native_group_id = "123"},
                      .user_id = "456"}));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::TransportFailure);
  EXPECT_TRUE(result.error->retryable);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

} // namespace
