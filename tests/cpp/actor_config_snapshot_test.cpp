#include "common/config_snapshot.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <type_traits>

namespace {
using obcx::bot::SurfaceId;
using obcx::common::ActorConfigSnapshotBuilder;
using obcx::common::BotInstallationMetadata;

template <typename T>
concept HasConnection = requires(T value) { value.connection; };
static_assert(!HasConnection<BotInstallationMetadata>);
static_assert(!std::is_default_constructible_v<BotInstallationMetadata>);

auto metadata() -> BotInstallationMetadata {
  return {"fake", true, SurfaceId{"test.echo"}, "memory", "echo", "fixture"};
}

TEST(ActorConfigSnapshotTest,
     BuildsFromExplicitActorDataWithoutPlatformRuntime) {
  const auto built =
      ActorConfigSnapshotBuilder::build(toml::parse(R"(
[actors.chat.config]
api_key = "ACTOR_OWNED_SECRET"
prompt = "hello"
[group_mappings]
label = "public-route"
)"),
                                        {metadata()}, "fixture.toml");
  ASSERT_TRUE(built);
  EXPECT_EQ(built.snapshot->get_bot_configs(), std::vector{metadata()});
  const obcx::common::ActorConfigService service{built.snapshot};
  const auto view = service.for_actor("chat");
  EXPECT_EQ(view.get_value<std::string>("api_key"), "ACTOR_OWNED_SECRET");
  ASSERT_TRUE(view.get_root_section("bots"));
  EXPECT_FALSE(view.get_root_section("bots.fake.connection"));
  EXPECT_FALSE(built.snapshot->get_value<std::string>(
      "bots.fake.connection.access_token"));
  EXPECT_EQ(built.snapshot->get_value<std::string>("bots.fake.surface"),
            "test.echo");
  EXPECT_EQ(
      built.snapshot->get_value<std::string>("bots.fake.ingress_platform"),
      "echo");
  EXPECT_EQ(built.snapshot->config_path(), "fixture.toml");
}

TEST(ActorConfigSnapshotTest,
     RejectsBotTablesInsteadOfPretendingToValidateConnections) {
  for (const auto &document : {R"(
[bots.fake.connection]
access_token = "PRIVATE_BOT_SECRET"
)",
                               R"(
bots = "PRIVATE_BOT_SECRET"
)",
                               R"(
[bots.fake]
enabled = true
surface = "test.echo"
transport = "memory"
)"}) {
    const auto built = ActorConfigSnapshotBuilder::build(
        toml::parse(document), {metadata()}, "fixture.toml");
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic);
    EXPECT_EQ(built.diagnostic->code,
              "actor_config_contains_bot_configuration");
    EXPECT_EQ(built.diagnostic->message.find("PRIVATE_BOT_SECRET"),
              std::string::npos);
  }
}

TEST(ActorConfigSnapshotTest, RejectsDuplicateOrInvalidMetadata) {
  EXPECT_FALSE(ActorConfigSnapshotBuilder::build(
      toml::table{}, {metadata(), metadata()}, "fixture.toml"));
  auto invalid = metadata();
  invalid.installation_id.clear();
  EXPECT_FALSE(ActorConfigSnapshotBuilder::build(toml::table{}, {invalid},
                                                 "fixture.toml"));
  invalid = metadata();
  invalid.ingress_platform.clear();
  EXPECT_FALSE(ActorConfigSnapshotBuilder::build(toml::table{}, {invalid},
                                                 "fixture.toml"));
}

TEST(ActorConfigSnapshotTest, OwnsItsValuesAndExportsOnlyMetadataFields) {
  auto bot = metadata();
  auto document = toml::parse("[actors.chat.config]\nvalue = 'before'");
  const auto built =
      ActorConfigSnapshotBuilder::build(document, {bot}, "fixture.toml");
  ASSERT_TRUE(built);
  document.at_path("actors.chat.config")
      .as_table()
      ->insert_or_assign("value", "after");
  bot.command_target = "changed";
  EXPECT_EQ(built.snapshot->get_actor_value<std::string>("chat", "value"),
            "before");
  EXPECT_EQ(built.snapshot->get_bot_configs().front().command_target,
            "fixture");
  const auto bots = built.snapshot->get_section("bots");
  ASSERT_TRUE(bots);
  const auto *entry = bots->get_as<toml::table>("fake");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->size(), 5U);
  EXPECT_FALSE(entry->contains("connection"));
  EXPECT_FALSE(entry->contains("access_token"));
  EXPECT_FALSE(entry->contains("host"));
}
} // namespace
