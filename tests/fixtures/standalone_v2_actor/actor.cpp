#include "common/logger.hpp"
#include "core/actor/command_availability.hpp"
#include "core/actor/reflected_actor.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"

auto sdk_helper_identity() -> std::string;

namespace obcx::sdk_fixture::events {
struct SdkSmoke {};
inline void to_json(common::json &json, const SdkSmoke &) {
  json = common::json::object();
}
inline void from_json(const common::json &, SdkSmoke &) {}

struct SdkCommand final : obcx::command::RequestMessage<SdkCommand> {};
} // namespace obcx::sdk_fixture::events

namespace {

class SdkV2Actor final : public obcx::core::ReflectedActor<SdkV2Actor> {
public:
  static constexpr auto command_contract() {
    return obcx::command::catalog(obcx::command::actor_scoped(
        obcx::command::observe<obcx::sdk_fixture::events::SdkCommand>(
            "sdk_ping", "Ping the standalone SDK actor",
            obcx::command::re2(R"(^(?:sdk_ping|sdk_alias)$)"))));
  }

  [[nodiscard]] static auto configuration_contract() -> obcx::common::json {
    return {
        {"bot_installation_collections",
         {{"installation_pairs",
           {{"minimum_items", 1},
            {"identity", "id"},
            {"bot_installations",
             {{"onebot11_installation", "qq"},
              {"telegram_installation", "telegram"}}}}}}},
    };
  }

  auto prepare_generation(obcx::core::ActorContext &context)
      -> obcx::core::ActorPreparationResult {
    const auto publisher =
        context.get_service<obcx::command::AvailabilityPublisher>();
    if (!publisher) {
      return obcx::core::ActorPreparationResult::failed(
          "scope publisher missing");
    }
    publisher->publish("sdk_ping", {{"telegram", "standalone-telegram", "-42",
                                     obcx::command::TopicSelection::Exact, 7}});
    return obcx::core::ActorPreparationResult::ready();
  }

  auto handle(const obcx::sdk_fixture::events::SdkSmoke &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorTask<obcx::core::ActorResult> {
    auto label =
        context.config().get_value<std::string>("label").value_or("missing");
    const auto helper_identity =
        co_await context.run_blocking([] { return sdk_helper_identity(); });
    OBCX_INFO("Standalone SDK actor resumed logging probe");
    const auto bot_operations =
        context.get_service<obcx::bot::BotOperationGateway>();
    const obcx::bot::BotInstallationRef installation{
        .installation_id = "standalone-telegram",
        .surface = obcx::bot::SurfaceId{"telegram.bot_api"},
    };
    const auto bot_operation_client_available = [&] {
      if (bot_operations == nullptr) {
        return false;
      }
      const auto supported = bot_operations->supported_actions(installation);
      return supported.ok() && supported.value->supports(
                                   obcx::bot::SendGroupMessageRequest::action);
    }();

    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope emitted;
    emitted.type = "SdkV2Handled";
    emitted.causation_id = message.id;
    emitted.payload = {
        {"label", label},
        {"identity",
         std::string{actor_name} + ":" + std::string{actor_version}},
        {"helper_identity", helper_identity},
        {"actor_logger", OBCX_CURRENT_LOGGER()->name()},
        {"bot_operation_client", bot_operation_client_available},
    };
    result.emit(std::move(emitted));
    co_return result;
  }

  auto handle(const obcx::sdk_fixture::events::SdkCommand &,
              const obcx::core::MessageEnvelope &, obcx::core::ActorContext &)
      -> obcx::core::ActorResult {
    return obcx::core::ActorResult::success();
  }
};

} // namespace

OBCX_ACTOR_EXPORT_V2(SdkV2Actor)
