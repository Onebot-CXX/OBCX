#include "core/actor/actor.hpp"

#include <gtest/gtest.h>

namespace obcx::tests::actor_api {

struct StandaloneOutput {
  int value = 0;
};

inline void to_json(common::json &document, const StandaloneOutput &message) {
  document = {{"value", message.value}};
}

} // namespace obcx::tests::actor_api

using namespace obcx::core;

TEST(ActorApiTest, MessageEnvelopeCarriesRoutingMetadataAndPayload) {
  MessageEnvelope envelope;
  envelope.id = "evt-1";
  envelope.type = "obcx::core::events::RawMessageEvent";
  envelope.source_platform = "qq";
  envelope.source_bot = "qq_bot";
  envelope.conversation_id = "group:10001";
  envelope.correlation_id = "corr-1";
  envelope.causation_id = "cause-1";
  envelope.payload["message_id"] = "42";
  envelope.raw["post_type"] = "message";
  envelope.headers["partition"] = "qq:10001";

  EXPECT_EQ(envelope.id, "evt-1");
  EXPECT_EQ(envelope.type, "obcx::core::events::RawMessageEvent");
  EXPECT_EQ(envelope.source_platform, "qq");
  EXPECT_EQ(envelope.source_bot, "qq_bot");
  EXPECT_EQ(envelope.conversation_id, "group:10001");
  EXPECT_EQ(envelope.correlation_id, "corr-1");
  EXPECT_EQ(envelope.causation_id, "cause-1");
  EXPECT_EQ(envelope.payload["message_id"], "42");
  EXPECT_EQ(envelope.raw["post_type"], "message");
  EXPECT_EQ(envelope.headers["partition"], "qq:10001");
}

TEST(ActorApiTest, ActorResultTracksEmittedMessagesAndFailureState) {
  ActorResult result = ActorResult::success();
  MessageEnvelope emitted;
  emitted.type = "obcx::message_store::events::MessageStored";

  result.emit(std::move(emitted));

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().type,
            "obcx::message_store::events::MessageStored");

  ActorResult failed =
      ActorResult::failed("message_store_error", "sqlite write failed", true);

  ASSERT_FALSE(failed.ok());
  ASSERT_TRUE(failed.failure.has_value());
  EXPECT_EQ(failed.failure->code, "message_store_error");
  EXPECT_EQ(failed.failure->message, "sqlite write failed");
  EXPECT_TRUE(failed.failure->retryable);
}

TEST(ActorApiTest, TypedEmitIsDefinedByStandaloneActorHeader) {
  MessageEnvelope parent;
  parent.id = "parent";
  auto result = ActorResult::success();

  result.emit(obcx::tests::actor_api::StandaloneOutput{.value = 42}, parent);

  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().type,
            "obcx::tests::actor_api::StandaloneOutput");
  EXPECT_EQ(result.emitted.front().payload,
            (obcx::common::json{{"value", 42}}));
}

TEST(ActorApiTest, ActorContextCanReadSharedRuntimeServicesAndLocalOverrides) {
  struct DbServiceMarker {
    explicit DbServiceMarker(std::string name) : name(std::move(name)) {}

    std::string name;
  };

  auto runtime_services = std::make_shared<ActorServices>();
  auto shared_db = std::make_shared<DbServiceMarker>("runtime-db");
  runtime_services->register_service<DbServiceMarker>(shared_db);

  ActorContext context("message_store", runtime_services);

  EXPECT_EQ(context.get_service<DbServiceMarker>(), shared_db);

  auto actor_local_db = std::make_shared<DbServiceMarker>("actor-local-db");
  context.register_service<DbServiceMarker>(actor_local_db);

  EXPECT_EQ(context.get_service<DbServiceMarker>(), actor_local_db);
}
