#include "core/bot/ids.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
using nlohmann::json;
using obcx::bot::ActionId;
using obcx::bot::SurfaceId;

static_assert(!std::is_default_constructible_v<SurfaceId>);
static_assert(!std::is_default_constructible_v<ActionId>);
static_assert(!std::is_convertible_v<std::string, SurfaceId>);
static_assert(!std::is_convertible_v<std::string_view, ActionId>);
static_assert(!std::is_convertible_v<SurfaceId, ActionId>);
static_assert(!std::is_convertible_v<ActionId, SurfaceId>);

template <typename Id> void check_invalid_ids() {
  for (const auto &value : std::vector<std::string>{
           "", "Telegram.bot_api", "has space", "path/to", "path\\to",
           "token:value", "line\nbreak", "a\tb", std::string{"a\0b", 3},
           std::string(1, '\x7f'), std::string(1, '\x80'),
           std::string(1, '\xff'), std::string(129, 'a')}) {
    EXPECT_THROW((void)Id{value}, std::invalid_argument);
    EXPECT_THROW((void)json(value).template get<Id>(), std::invalid_argument);
  }
  for (const auto &document : {json(nullptr), json(true), json(1), json(1.5),
                               json::object(), json::array()}) {
    EXPECT_THROW((void)document.template get<Id>(), std::invalid_argument);
  }
}

TEST(BotIdsTest, NoPlatformListOrOrdinalIsNeededForRoundTrip) {
  const SurfaceId surface{"test.echo"};
  const ActionId action{"test.echo.reply-v2_7"};
  EXPECT_EQ(json(surface).dump(), "\"test.echo\"");
  EXPECT_EQ(json(action).dump(), "\"test.echo.reply-v2_7\"");
  EXPECT_EQ(json(surface).get<SurfaceId>(), surface);
  EXPECT_EQ(json(action).get<ActionId>(), action);
  // Syntax-valid names do not imply production registration or alias support.
  EXPECT_NO_THROW((void)SurfaceId{"qq.official"});
  EXPECT_NO_THROW((void)SurfaceId{"telegram"});
  EXPECT_NO_THROW((void)ActionId{"message.history"});
}

TEST(BotIdsTest, OwnsInputAndUsesExactEqualityAndHashing) {
  std::string input = "test.echo";
  const SurfaceId surface{input};
  input.assign("changed");
  EXPECT_EQ(surface.value(), "test.echo");
  EXPECT_EQ(surface, SurfaceId{"test.echo"});
  EXPECT_NE(surface, SurfaceId{"test.echo2"});
  EXPECT_EQ(std::hash<SurfaceId>{}(surface),
            std::hash<SurfaceId>{}(SurfaceId{"test.echo"}));
  const std::unordered_set<SurfaceId> surfaces{surface, SurfaceId{"test.echo"},
                                               SurfaceId{"another.module"}};
  EXPECT_EQ(surfaces.size(), 2U);
  const std::unordered_set<ActionId> actions{ActionId{"test.echo.reply"}};
  EXPECT_TRUE(actions.contains(ActionId{"test.echo.reply"}));
}

TEST(BotIdsTest, RejectsInvalidSyntaxWithoutNormalization) {
  check_invalid_ids<SurfaceId>();
  check_invalid_ids<ActionId>();
  EXPECT_NO_THROW((void)SurfaceId{std::string(128, 'a')});
  EXPECT_NO_THROW((void)ActionId{"abcdefghijklmnopqrstuvwxyz0123456789._-"});
}

TEST(BotIdsTest, DecodesContainersWithoutDefaultValues) {
  const std::vector<ActionId> actions{ActionId{"z.call"}, ActionId{"a.call"}};
  EXPECT_EQ(json(actions).get<std::vector<ActionId>>(), actions);
  SurfaceId value{"original.surface"};
  EXPECT_THROW(json("Invalid").get_to(value), std::invalid_argument);
  EXPECT_EQ(value, SurfaceId{"original.surface"});
  json("replacement.surface").get_to(value);
  EXPECT_EQ(value, SurfaceId{"replacement.surface"});
  EXPECT_LT(ActionId{"a.call"}, ActionId{"z.call"});
}

TEST(BotIdsTest, InvalidDiagnosticDoesNotEchoInput) {
  try {
    (void)ActionId{"secret=value"};
    FAIL() << "invalid ID was accepted";
  } catch (const std::invalid_argument &error) {
    EXPECT_EQ(std::string_view{error.what()}.find("secret=value"),
              std::string_view::npos);
  }
}

} // namespace
