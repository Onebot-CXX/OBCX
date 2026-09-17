#include "actor_config_fixture.hpp"
#include "core/actor/actor_manager.hpp"
#include "core/actor/native_actor_scheduler.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"
#include "fake_gateway_dispatch.hpp"
#include "onebot11/bot/operations.hpp"
#include "telegram/bot/operations.hpp"

#include "common/config_snapshot.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/thread_pool.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <utility>

namespace {

class FixtureBotOperationGateway final : public obcx::bot::BotOperationGateway {
public:
  auto invoke(obcx::bot::OperationEnvelope envelope)
      -> boost::asio::awaitable<obcx::bot::OperationReply> override {
    return obcx::tests::dispatch_fake_gateway<
        obcx::bot::SendGroupMessageRequest>(*this, std::move(envelope));
  }
  auto execute(const obcx::bot::SendGroupMessageRequest &request)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<obcx::bot::SendMessageResult>> {
    co_return obcx::bot::BotOperationResult<obcx::bot::SendMessageResult>::
        success({.messages = {{.group = request.target,
                               .native_message_id = "sdk-42"}}});
  }

  [[nodiscard]] auto supported_actions(
      const obcx::bot::BotInstallationRef &installation) const
      -> obcx::bot::BotOperationResult<obcx::bot::SupportedActions> override {
    if (installation.installation_id != "standalone-telegram" ||
        installation.surface != obcx::bot::SurfaceId{"telegram.bot_api"}) {
      return obcx::bot::failed_operation<obcx::bot::SupportedActions>(
          obcx::bot::BotOperationErrorCode::RouteNotFound,
          "fixture installation not found");
    }
    return obcx::bot::BotOperationResult<obcx::bot::SupportedActions>::success(
        {.installation = installation,
         .actions = {obcx::bot::SendGroupMessageRequest::action}});
  }
};

auto bot_operation_contract_smoke() -> bool {
  const obcx::bot::BotInstallationRef installation{
      .installation_id = "standalone-telegram",
      .surface = obcx::bot::SurfaceId{"telegram.bot_api"},
  };
  const obcx::telegram::bot::SendTelegramTopicMessageRequest request{
      .target = {.group = {.installation = installation,
                           .native_group_id = "-1001"},
                 .topic_id = 7},
      .message = {{.type = "text", .data = {{"text", "sdk smoke"}}}},
  };
  const auto request_document = nlohmann::json(request);
  const auto decoded_request =
      request_document
          .get<obcx::telegram::bot::SendTelegramTopicMessageRequest>();
  if (nlohmann::json(decoded_request).dump() != request_document.dump()) {
    return false;
  }

  const obcx::bot::BotMessageRef message{
      .group = request.target.group,
      .native_message_id = "42",
  };
  const auto result =
      obcx::bot::BotOperationResult<obcx::bot::SendMessageResult>::success(
          {.messages = {message}});
  const auto result_document = nlohmann::json(result);
  const auto decoded_result =
      result_document
          .get<obcx::bot::BotOperationResult<obcx::bot::SendMessageResult>>();
  if (decoded_result != result ||
      nlohmann::json(decoded_result).dump() != result_document.dump()) {
    return false;
  }

  const obcx::onebot11::bot::PokeOneBotGroupRequest poke{
      .target = {.installation = {.installation_id = "standalone-onebot",
                                  .surface =
                                      obcx::bot::SurfaceId{"onebot11.qq"}},
                 .native_group_id = "123"},
      .user_id = "456",
  };
  return nlohmann::json(poke)
             .get<obcx::onebot11::bot::PokeOneBotGroupRequest>()
             .user_id == "456";
}

} // namespace

int main(int argc, char **argv) {
  using namespace std::chrono_literals;
  using namespace obcx::core;

  if (argc != 2) {
    return 1;
  }
  if (!bot_operation_contract_smoke()) {
    return 9;
  }
  const std::filesystem::path actor_path = argv[1];
  {
    ActorManager manager;
    if (!manager.load_actor_from_path(actor_path)) {
      return 2;
    }
    auto actor = manager.get_actor_shared("sdk_v2_fixture");
    if (!actor) {
      return 3;
    }
    const auto *contract = manager.get_actor_contract("sdk_v2_fixture");
    if (contract == nullptr || contract->commands.size() != 1 ||
        contract->commands.front().name != "sdk_ping" ||
        contract->commands.front().request_type !=
            "obcx::sdk_fixture::events::SdkCommand" ||
        !contract->commands.front().matcher ||
        contract->commands.front().matcher->kind != "re2" ||
        contract->commands.front().matcher->pattern !=
            R"(^(?:sdk_ping|sdk_alias)$)" ||
        contract->commands.front().matcher->mode != "full" ||
        contract->bot_installation_collection_configuration.size() != 1 ||
        contract->bot_installation_collection_configuration.front().key !=
            "installation_pairs" ||
        contract->bot_installation_collection_configuration.front()
                .identity_key != "id" ||
        contract->bot_installation_collection_configuration.front()
                .installation_fields.size() != 2) {
      return 8;
    }

    const auto config_path =
        std::filesystem::temp_directory_path() /
        ("obcx-sdk-v2-smoke-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".toml");
    {
      std::ofstream config(config_path);
      config << "[actors.sdk_v2_fixture.config]\nlabel = \"generation-a\"\n";
    }
    auto built = obcx::test::actor_fixture_snapshot(config_path.string());
    if (!built) {
      return 4;
    }
    auto blocking_executor = std::make_shared<BlockingExecutor>(1);
    boost::asio::thread_pool actor_io_pool{1};
    auto services = std::make_shared<ActorServices>();
    services->register_service<obcx::common::ActorConfigService>(
        std::make_shared<obcx::common::ActorConfigService>(built.snapshot));
    services->register_service<BlockingExecutor>(blocking_executor);
    services->register_service<obcx::bot::BotOperationGateway>(
        std::make_shared<FixtureBotOperationGateway>());
    services->register_service<boost::asio::any_io_executor>(
        std::make_shared<boost::asio::any_io_executor>(
            actor_io_pool.get_executor()));

    NativeActorScheduler scheduler(
        NativeActorSchedulerOptions{.worker_count = 2}, services);
    scheduler.register_actor(std::move(actor));
    MessageEnvelope message;
    message.id = "standalone-sdk";
    message.type = "obcx::sdk_fixture::events::SdkSmoke";
    std::promise<ActorResult> completion;
    auto future = completion.get_future();
    if (!scheduler.enqueue(ActorInvocation{.actor_id = "sdk_v2_fixture",
                                           .partition_key = "same",
                                           .message = std::move(message)},
                           [&completion](ActorResult result) {
                             completion.set_value(std::move(result));
                           })) {
      return 5;
    }
    if (future.wait_for(5s) != std::future_status::ready) {
      return 6;
    }
    const auto result = future.get();
    scheduler.shutdown();
    actor_io_pool.join();
    blocking_executor->shutdown();
    if (!result.ok() || result.emitted.size() != 1 ||
        result.emitted.front().type != "SdkV2Handled" ||
        result.emitted.front().causation_id != "standalone-sdk" ||
        result.emitted.front().payload.value("label", "") != "generation-a" ||
        !result.emitted.front().payload.value("bot_operation_client", false)) {
      return 7;
    }
    std::filesystem::remove(config_path);
  }

  const auto unload_probe = actor_path.string() + ".unload-probe";
  std::filesystem::rename(actor_path, unload_probe);
  std::filesystem::rename(unload_probe, actor_path);
  return 0;
}
