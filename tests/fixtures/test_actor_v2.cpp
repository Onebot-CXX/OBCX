#include "core/actor/reflected_actor.hpp"

namespace obcx::tests::events {
struct SdkSmoke {};
inline void to_json(common::json &json, const SdkSmoke &) {
  json = common::json::object();
}
inline void from_json(const common::json &, SdkSmoke &) {}

struct SdkCommand final : obcx::command::RequestMessage<SdkCommand> {};
} // namespace obcx::tests::events

namespace {
class TestActorV2 final : public obcx::core::ReflectedActor<TestActorV2> {
public:
  static constexpr std::string_view actor_name = "test_actor_v2";
  static constexpr std::string_view actor_version = "2.0.0";

  static constexpr auto command_contract() {
    return obcx::command::catalog(
        obcx::command::observe<obcx::tests::events::SdkCommand>(
            "sdk_ping", "Ping the SDK fixture",
            obcx::command::re2(R"(^(?:sdk_ping|sdk_alias)$)")));
  }

  [[nodiscard]] static auto configuration_contract() -> obcx::common::json {
    return {
        {"integers",
         {{"positive_limit", {{"default", 5}, {"minimum", 1}}},
          {"retry_base", {{"default", 2}, {"minimum", 1}}},
          {"retry_max", {{"default", 10}, {"minimum", 1}}}}},
        {"required_strings", obcx::common::json::array({"label"})},
        {"bot_installations",
         {{"target_installation",
           {{"types",
             obcx::common::json::array({"onebot11.qq", "telegram.bot_api"})},
            {"alternative_group", "target_form"}}}}},
        {"bot_installation_collections",
         {{"target_installations",
           {{"minimum_items", 1},
            {"identity", "id"},
            {"bot_installations",
             {{"target_installation",
               obcx::common::json::array(
                   {"onebot11.qq", "telegram.bot_api"})}}},
            {"unique_fields",
             obcx::common::json::array({"target_installation"})},
            {"alternative_group", "target_form"}}}}},
        {"collection_identity_references",
         obcx::common::json::array(
             {{{"source_key", "selected_target"},
               {"target_collection", "target_installations"},
               {"target_identity", "id"},
               {"optional", true}}})},
        {"less_equal", obcx::common::json::array({obcx::common::json::array(
                           {"retry_base", "retry_max"})})},
    };
  }

  auto prepare_generation(obcx::core::ActorContext &context)
      -> obcx::core::ActorPreparationResult {
    const auto configured = context.config()
                                .get_value<std::string>("preparation_status")
                                .value_or(std::string{});
    if (context.actor_id() == "prepare-failed" || configured == "failed") {
      return obcx::core::ActorPreparationResult::failed(
          "fixture preparation failed");
    }
    if (context.actor_id() == "prepare-restart" || configured == "restart") {
      return obcx::core::ActorPreparationResult::restart_required(
          "fixture preparation requires restart");
    }
    return obcx::core::ActorPreparationResult::ready();
  }

  auto handle(const obcx::tests::events::SdkSmoke &,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &context) -> obcx::core::ActorResult {
    (void)context;
    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope emitted;
    emitted.id = "handled-" + message.id;
    emitted.type = "V2Handled";
    emitted.causation_id = message.id;
    result.emit(std::move(emitted));
    return result;
  }

  auto handle(const obcx::tests::events::SdkCommand &request,
              const obcx::core::MessageEnvelope &message,
              obcx::core::ActorContext &) -> obcx::core::ActorResult {
    auto result = obcx::core::ActorResult::success();
    obcx::core::MessageEnvelope observed;
    observed.id = message.id + ":observed";
    observed.type = "SdkCommandObserved";
    observed.source_platform = message.source_platform;
    observed.source_bot = message.source_bot;
    observed.conversation_id = message.conversation_id;
    observed.headers = message.headers;
    result.emit(std::move(observed));
    result.emit(
        obcx::command::CommandCompleted{
            .transaction_id = request.invocation.transaction_id,
            .propagation = request.invocation.arguments == "continue"
                               ? obcx::command::Propagation::Continue
                               : obcx::command::Propagation::Consume,
        },
        message);
    return result;
  }
};

} // namespace

OBCX_ACTOR_EXPORT_V2(TestActorV2)
