#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/configuration_error.hpp"
#include "core/bot/platform_catalog.hpp"

#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <type_traits>

namespace {
using namespace obcx::core;
using obcx::bot::ActionId;
using obcx::bot::SurfaceId;
using obcx::common::BotInstallationMetadata;

static_assert(!std::is_default_constructible_v<BotInstallationMetadata>);

auto input() -> BotInstallationInput {
  return {"echo-main", false, SurfaceId{"test.echo"}, "memory"};
}
auto metadata(const BotInstallationInput &value) -> BotInstallationMetadata {
  return {value.installation_id, value.enabled, value.surface,
          value.transport,       "echo",        ""};
}
auto recipe(const BotInstallationInput &value) -> BotRecipeDescription {
  return {"test.echo.memory",
          value.surface,
          value.transport,
          {},
          {ActionId{"test.echo.reply"}}};
}
auto empty_factory() -> BotInstallationPlan::ComponentFactory {
  return [](boost::asio::io_context &) {
    return std::vector<std::unique_ptr<BotComponent>>{};
  };
}
auto plan(const BotInstallationInput &value)
    -> std::shared_ptr<const BotInstallationPlan> {
  return std::make_shared<BotInstallationPlan>(metadata(value), recipe(value),
                                               std::string(64, 'a'),
                                               empty_factory(), nullptr);
}
auto registration() -> BotRecipeRegistration {
  return {SurfaceId{"test.echo"}, "memory", "echo",
          [](const BotInstallationInput &value, const toml::table &,
             std::string_view) { return plan(value); }};
}

TEST(BotPlatformCatalogTest, RequiresSealingBeforeAnyRuntimeLookup) {
  BotPlatformCatalog catalog;
  catalog.register_recipe(registration());
  EXPECT_THROW((void)catalog.supports(SurfaceId{"test.echo"}),
               std::logic_error);
  EXPECT_THROW((void)catalog.parse(input(), {}, "bots.echo-main"),
               std::logic_error);
  EXPECT_THROW((void)catalog.recipes(), std::logic_error);
  catalog.seal();
  EXPECT_TRUE(catalog.supports(SurfaceId{"test.echo"}, "memory"));
  EXPECT_THROW(catalog.register_recipe(registration()), std::logic_error);
  EXPECT_THROW(catalog.seal(), std::logic_error);
}

TEST(BotPlatformCatalogTest, RejectsDuplicateAndIncompleteRegistration) {
  BotPlatformCatalog catalog;
  auto invalid = registration();
  invalid.parse = {};
  EXPECT_THROW(catalog.register_recipe(invalid), std::invalid_argument);
  invalid = registration();
  invalid.transport.clear();
  EXPECT_THROW(catalog.register_recipe(invalid), std::invalid_argument);
  invalid = registration();
  invalid.ingress_platform.clear();
  EXPECT_THROW(catalog.register_recipe(invalid), std::invalid_argument);
  catalog.register_recipe(registration());
  EXPECT_THROW(catalog.register_recipe(registration()), std::invalid_argument);
  auto another = registration();
  another.transport = "alternate";
  catalog.register_recipe(another);
  catalog.seal();
  const auto keys = catalog.recipes();
  ASSERT_EQ(keys.size(), 2);
  EXPECT_EQ(keys[0].second, "alternate");
  EXPECT_EQ(keys[1].second, "memory");
}

TEST(BotPlatformCatalogTest, UnknownRoutesNeverCallParser) {
  int calls = 0;
  auto entry = registration();
  entry.parse = [&](const auto &value, const auto &, auto) {
    ++calls;
    return plan(value);
  };
  BotPlatformCatalog catalog;
  catalog.register_recipe(std::move(entry));
  catalog.seal();
  for (const auto &[surface, transport, expected] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"unknown.surface", "memory", "unsupported_bot_surface"},
           {"test.echo", "unknown", "unsupported_bot_transport"}}) {
    auto value = input();
    value.surface = SurfaceId{surface};
    value.transport = transport;
    try {
      (void)catalog.parse(value, {}, "bots.echo-main");
      FAIL() << "unknown route accepted";
    } catch (const BotConfigurationError &error) {
      EXPECT_EQ(error.code(), expected);
      EXPECT_TRUE(error.path().starts_with("bots.echo-main."));
    }
  }
  EXPECT_EQ(calls, 0);
}

TEST(BotPlatformCatalogTest, DisabledInstallationIsParsedOnceWithoutAssembly) {
  int parses = 0;
  int constructions = 0;
  auto entry = registration();
  entry.parse = [&](const auto &value, const toml::table &connection,
                    std::string_view path) {
    ++parses;
    EXPECT_FALSE(value.enabled);
    EXPECT_EQ(path, "bots.echo-main.connection");
    EXPECT_EQ(connection["value"].value<int>(), 42);
    return std::make_shared<BotInstallationPlan>(
        metadata(value), recipe(value), std::string(64, 'a'),
        [&](boost::asio::io_context &) {
          ++constructions;
          return std::vector<std::unique_ptr<BotComponent>>{};
        },
        nullptr);
  };
  BotPlatformCatalog catalog;
  catalog.register_recipe(std::move(entry));
  catalog.seal();
  const auto parsed =
      catalog.parse(input(), toml::table{{"value", 42}}, "bots.echo-main");
  EXPECT_EQ(parses, 1);
  EXPECT_EQ(parsed->metadata().installation_id, "echo-main");
  EXPECT_FALSE(parsed->metadata().enabled);
  EXPECT_EQ(parsed->recipe().advertised_actions.size(), 1);
  EXPECT_EQ(constructions, 0);
  boost::asio::io_context executor;
  EXPECT_TRUE(parsed->create_components(executor).empty());
  EXPECT_EQ(parses, 1);
  EXPECT_EQ(constructions, 1);
}

TEST(BotPlatformCatalogTest, RejectsParserIdentityConflictsAndNullPlans) {
  for (int change = 0; change < 6; ++change) {
    auto entry = registration();
    entry.parse = [change](const auto &value, const auto &,
                           auto) -> std::shared_ptr<const BotInstallationPlan> {
      auto edited = value;
      if (change == 0) {
        return {};
      }
      if (change == 1) {
        edited.installation_id = "different";
      }
      if (change == 2) {
        edited.enabled = !edited.enabled;
      }
      if (change == 3) {
        edited.surface = SurfaceId{"another.surface"};
      }
      if (change == 4) {
        edited.transport = "other";
      }
      auto meta = metadata(edited);
      if (change == 5) {
        meta.ingress_platform = "other";
      }
      return std::make_shared<BotInstallationPlan>(
          meta, recipe(edited), std::string(64, 'a'), empty_factory(), nullptr);
    };
    BotPlatformCatalog catalog;
    catalog.register_recipe(std::move(entry));
    catalog.seal();
    EXPECT_THROW((void)catalog.parse(input(), {}, "bots.echo-main"),
                 BotConfigurationError);
  }
}

TEST(BotPlatformCatalogTest, PlanRejectsInvalidManifestAndMissingBehavior) {
  const auto value = input();
  auto desc = recipe(value);
  EXPECT_THROW((void)BotInstallationPlan(metadata(value), desc,
                                         std::string(64, 'a'), {}, nullptr),
               std::invalid_argument);
  EXPECT_THROW((void)BotInstallationPlan(metadata(value), desc, "not-a-digest",
                                         empty_factory(), nullptr),
               std::invalid_argument);
  desc.surface = SurfaceId{"another.surface"};
  EXPECT_THROW((void)BotInstallationPlan(metadata(value), desc,
                                         std::string(64, 'a'), empty_factory(),
                                         nullptr),
               std::invalid_argument);
  desc = recipe(value);
  desc.advertised_actions.push_back(desc.advertised_actions.front());
  EXPECT_THROW((void)BotInstallationPlan(metadata(value), desc,
                                         std::string(64, 'a'), empty_factory(),
                                         nullptr),
               std::invalid_argument);
  desc = recipe(value);
  desc.components.push_back(
      {ComponentId{"consumer"}, {}, {CapabilityId{"missing"}}});
  EXPECT_THROW((void)BotInstallationPlan(metadata(value), desc,
                                         std::string(64, 'a'), empty_factory(),
                                         nullptr),
               BotComponentRuntimeError);
}

TEST(BotPlatformCatalogTest,
     PlanFingerprintIncludesIdentitySecretsAndManifest) {
  const auto value = input();
  const auto original = plan(value);
  for (int change = 0; change < 9; ++change) {
    auto meta = metadata(value);
    auto desc = recipe(value);
    std::string digest(64, 'a');
    if (change == 0) {
      meta.enabled = true;
    }
    if (change == 1) {
      meta.installation_id = "second";
    }
    if (change == 2) {
      meta.command_target = "different";
    }
    if (change == 3) {
      meta.ingress_platform = "different";
    }
    if (change == 4) {
      desc.recipe_id = "test.echo.revised";
    }
    if (change == 5) {
      desc.advertised_actions.push_back(ActionId{"test.echo.extra"});
    }
    if (change == 6) {
      digest[0] = 'b';
    }
    if (change == 7) {
      meta.transport = "alternate";
      desc.transport = meta.transport;
    }
    if (change == 8) {
      meta.surface = SurfaceId{"another.surface"};
      desc.surface = meta.surface;
    }
    const BotInstallationPlan changed(meta, desc, digest, empty_factory(),
                                      nullptr);
    EXPECT_NE(changed.fingerprint(), original->fingerprint());
  }
  EXPECT_EQ(plan(value)->fingerprint(), original->fingerprint());
}

TEST(BotPlatformCatalogTest, SealedCatalogSupportsConcurrentReadOnlyUse) {
  BotPlatformCatalog catalog;
  catalog.register_recipe(registration());
  catalog.seal();
  std::vector<std::future<void>> work;
  for (int index = 0; index < 8; ++index) {
    work.push_back(std::async(std::launch::async, [&] {
      for (int attempt = 0; attempt < 100; ++attempt) {
        EXPECT_TRUE(catalog.supports(SurfaceId{"test.echo"}));
        EXPECT_EQ(catalog.parse(input(), {}, "bots.echo-main")->metadata(),
                  metadata(input()));
      }
    }));
  }
  for (auto &task : work) {
    task.get();
  }
}

TEST(BotPlatformCatalogTest, EmptySealedCatalogDoesNotInventBuiltins) {
  BotPlatformCatalog catalog;
  catalog.seal();
  EXPECT_TRUE(catalog.recipes().empty());
  EXPECT_FALSE(catalog.supports(SurfaceId{"onebot11.qq"}));
  EXPECT_FALSE(catalog.supports(SurfaceId{"telegram.bot_api"}));
}

TEST(BotPlatformCatalogTest, PlanOwnsTypedFactoryStateBeyondCatalogLifetime) {
  std::weak_ptr<const int> state;
  std::shared_ptr<const BotInstallationPlan> retained;
  {
    auto typed = std::make_shared<const int>(42);
    state = typed;
    auto entry = registration();
    entry.parse = [typed](const auto &value, const auto &, auto) {
      return std::make_shared<BotInstallationPlan>(
          metadata(value), recipe(value), std::string(64, 'a'),
          [typed](boost::asio::io_context &) {
            EXPECT_EQ(*typed, 42);
            return std::vector<std::unique_ptr<BotComponent>>{};
          },
          nullptr);
    };
    BotPlatformCatalog catalog;
    catalog.register_recipe(std::move(entry));
    catalog.seal();
    retained = catalog.parse(input(), {}, "bots.echo-main");
  }
  EXPECT_FALSE(state.expired());
  boost::asio::io_context executor;
  EXPECT_TRUE(retained->create_components(executor).empty());
  retained.reset();
  EXPECT_TRUE(state.expired());
}
} // namespace
