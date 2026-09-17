#include "core/actor/reflected_actor.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"

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
  static constexpr std::string_view actor_name = "sdk_v2_fixture";
  static constexpr std::string_view actor_version = "2.0.0";

  static constexpr auto command_contract() {
    return obcx::command::catalog(
        obcx::command::observe<obcx::sdk_fixture::events::SdkCommand>(
            "sdk_ping", "Ping the standalone SDK actor",
            obcx::command::re2(R"(^(?:sdk_ping|sdk_alias)$)")));
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

  auto handle(const obcx::sdk_fixture::events::SdkSmoke &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context)
      -> obcx::core::ActorTask<obcx::core::ActorResult> {
    auto label =
        context.config().get_value<std::string>("label").value_or("missing");
    label = co_await context.run_blocking(
        [label = std::move(label)]() mutable { return std::move(label); });
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
