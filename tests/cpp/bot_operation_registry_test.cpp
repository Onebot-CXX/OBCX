#include "core/bot/messaging.hpp"
#include "core/bot/operation_registry.hpp"
#include "support/sdk_gateway_fixture.hpp"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <gtest/gtest.h>

namespace {
using namespace obcx::bot;
using obcx::core::OperationDefinition;
using obcx::core::OperationRegistry;

const BotInstallationRef installation{.installation_id = "fixture",
                                      .surface = SurfaceId{"test.echo"}};

auto request() -> SendGroupMessageRequest {
  return {
      .target = {.installation = installation, .native_group_id = "group-1"},
      .message = {{.type = "text", .data = {{"text", "hello"}}}}};
}

auto envelope() -> OperationEnvelope {
  auto operation = request();
  return {.installation = installation,
          .action = SendGroupMessageRequest::action,
          .payload = GatewayCodec<SendGroupMessageRequest>::encode(operation)};
}

auto success(const SendGroupMessageRequest &operation)
    -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
  co_return BotOperationResult<SendMessageResult>::success(
      {.messages = {{.group = operation.target, .native_message_id = "42"}}});
}

TEST(OperationRegistryTest, DefinitionsShareManifestAndExecutableBinding) {
  const OperationDefinition<SendGroupMessageRequest> definition{
      {"test.protocol"}};
  OperationRegistry registry{installation};
  registry.install(definition.bind(success));
  ASSERT_EQ(registry.descriptions().size(), 1U);
  EXPECT_EQ(registry.descriptions().front(), definition.description());
  EXPECT_FALSE(registry.supported_actions(installation).ok());
  const std::vector<std::string> capabilities{"test.protocol"};
  registry.seal(installation, capabilities);
  const auto supported = registry.supported_actions(installation);
  ASSERT_TRUE(supported.ok());
  EXPECT_EQ(supported.value->actions,
            (std::vector{SendGroupMessageRequest::action}));
  const auto result =
      obcx::tests::await_sdk(obcx::bot::invoke(registry, request()));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value->primary().group, request().target);
}

TEST(OperationRegistryTest,
     RejectsIncompleteDuplicateAndCrossInstallationDependencies) {
  using Definition = OperationDefinition<SendGroupMessageRequest>;
  const Definition definition{{"test.protocol"}};
  EXPECT_THROW((void)definition.bind(Definition::Handler{}),
               std::invalid_argument);
  EXPECT_THROW((void)Definition({"test.protocol", "test.protocol"}),
               std::invalid_argument);
  OperationRegistry registry{installation};
  registry.install(definition.bind(success));
  EXPECT_THROW(registry.install(definition.bind(success)),
               std::invalid_argument);
  EXPECT_THROW(registry.seal(installation, {}), std::invalid_argument);
  const std::vector<std::string> capabilities{"test.protocol"};
  auto foreign = installation;
  foreign.installation_id = "another";
  EXPECT_THROW(registry.seal(foreign, capabilities), std::invalid_argument);
  registry.seal(installation, capabilities);
  EXPECT_THROW(registry.install(definition.bind(success)), std::logic_error);
}

TEST(OperationRegistryTest, ForgedAndUnknownEnvelopesNeverInvokeTheHandler) {
  unsigned calls = 0;
  const OperationDefinition<SendGroupMessageRequest> definition{{}};
  OperationRegistry registry{installation};
  registry.install(definition.bind(
      [&calls](const SendGroupMessageRequest &operation)
          -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
        ++calls;
        co_return co_await success(operation);
      }));
  auto unprepared = obcx::tests::await_sdk(registry.invoke(envelope()));
  EXPECT_FALSE(unprepared.ok());
  EXPECT_EQ(calls, 0U);
  registry.seal(installation, {});
  std::vector<OperationEnvelope> invalid;
  auto unknown = envelope();
  unknown.action = ActionId{"test.unregistered"};
  unknown.payload["action"] = unknown.action;
  invalid.push_back(std::move(unknown));
  auto conflict = envelope();
  conflict.payload["action"] = "test.conflict";
  invalid.push_back(std::move(conflict));
  auto wrong_payload = envelope();
  wrong_payload.payload["target"]["installation"]["installation_id"] =
      "another";
  invalid.push_back(std::move(wrong_payload));
  auto wrong_surface = envelope();
  wrong_surface.installation.surface = SurfaceId{"another.surface"};
  invalid.push_back(std::move(wrong_surface));
  auto malformed = envelope();
  malformed.payload["message"] = Json::array();
  invalid.push_back(std::move(malformed));
  for (auto &input : invalid) {
    const auto result =
        obcx::tests::await_sdk(registry.invoke(std::move(input)));
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error->submission_safety,
              SubmissionSafety::DefinitelyNotSubmitted);
  }
  EXPECT_EQ(calls, 0U);
}

TEST(OperationRegistryTest, InvalidHandlerSuccessRemainsPossiblySubmitted) {
  const OperationDefinition<SendGroupMessageRequest> definition{{}};
  OperationRegistry registry{installation};
  registry.install(definition.bind(
      [](const SendGroupMessageRequest &)
          -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
        co_return BotOperationResult<SendMessageResult>::success(
            {.messages = {}});
      }));
  registry.seal(installation, {});
  const auto result = obcx::tests::await_sdk(registry.invoke(envelope()));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::PossiblySubmitted);
  EXPECT_FALSE(result.error->retryable);
}

TEST(OperationRegistryTest,
     AdmittedInvocationRetainsHandlerAfterRegistryDestruction) {
  boost::asio::io_context io;
  auto timer = std::make_shared<boost::asio::steady_timer>(io);
  timer->expires_after(std::chrono::seconds{30});
  auto handler_lifetime = std::make_shared<int>(42);
  const std::weak_ptr<int> lifetime = handler_lifetime;
  bool entered = false;
  const OperationDefinition<SendGroupMessageRequest> definition{{}};
  auto registry = std::make_unique<OperationRegistry>(installation);
  registry->install(definition.bind(
      [timer, lease = handler_lifetime,
       &entered](const SendGroupMessageRequest &operation)
          -> boost::asio::awaitable<BotOperationResult<SendMessageResult>> {
        entered = true;
        boost::system::error_code error;
        co_await timer->async_wait(
            boost::asio::redirect_error(boost::asio::use_awaitable, error));
        co_return BotOperationResult<SendMessageResult>::success(
            {.messages = {{.group = operation.target,
                           .native_message_id = std::to_string(*lease)}}});
      }));
  registry->seal(installation, {});
  handler_lifetime.reset();
  auto result = boost::asio::co_spawn(io, registry->invoke(envelope()),
                                      boost::asio::use_future);
  io.poll();
  ASSERT_TRUE(entered);
  registry.reset();
  EXPECT_FALSE(lifetime.expired());
  timer->cancel();
  io.restart();
  io.run();
  const auto reply = result.get();
  ASSERT_TRUE(reply.ok());
  EXPECT_EQ(reply.value->get<SendMessageResult>().primary().native_message_id,
            "42");
  EXPECT_TRUE(lifetime.expired());
}

TEST(OperationRegistryTest,
     DestructionClosesAdmissionBeforeLazyInvocationStarts) {
  const OperationDefinition<SendGroupMessageRequest> definition{{}};
  auto registry = std::make_unique<OperationRegistry>(installation);
  registry->install(definition.bind(success));
  registry->seal(installation, {});
  auto pending = registry->invoke(envelope());
  registry.reset();
  const auto result = obcx::tests::await_sdk(std::move(pending));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error->code, BotOperationErrorCode::Cancelled);
  EXPECT_EQ(result.error->submission_safety,
            SubmissionSafety::DefinitelyNotSubmitted);
}

} // namespace
