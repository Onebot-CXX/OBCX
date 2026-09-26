#include "core/bot/bot_installation_assembler.hpp"
#include "core/bot/bot_operation_dispatcher.hpp"
#include "core/runtime/process_configuration.hpp"
#include "fixtures/echo_module/contract.hpp"
#include "fixtures/echo_module/module.hpp"
#include "support/sdk_gateway_fixture.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <unistd.h>

namespace {
namespace echo = obcx::tests::echo;
using namespace obcx::core;

class EchoRuntimeTest : public ::testing::Test {
protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("obcx-echo-runtime-" + std::to_string(::getpid()) + ".toml");
    observations_ = std::make_shared<echo::Observations>();
    auto catalog = std::make_shared<BotPlatformCatalog>();
    echo::register_module(*catalog, observations_);
    catalog->seal();
    catalog_ = std::move(catalog);
  }
  void TearDown() override { std::filesystem::remove(path_); }
  auto parse(std::string secret, bool enabled)
      -> obcx::common::RuntimeConfigBuildResult {
    std::ofstream file{path_};
    file << "[bots.echo]\nenabled = " << (enabled ? "true" : "false") << R"(
surface = "test.echo"
transport = "memory"
[bots.echo.connection]
prefix = "echo:"
secret = ")"
         << secret << "\"\n";
    file.close();
    return obcx::common::ConfigLoader::build_snapshot(path_.string(), catalog_);
  }
  std::filesystem::path path_;
  std::shared_ptr<echo::Observations> observations_;
  std::shared_ptr<const BotPlatformCatalog> catalog_;
};

TEST_F(EchoRuntimeTest,
       ParsesDescribesAssemblesInvokesAndStopsWithoutProductionModules) {
  const auto built = parse("PRIVATE_ECHO_SECRET", true);
  ASSERT_TRUE(built) << (built.diagnostic ? built.diagnostic->message : "");
  ASSERT_EQ(observations_->parses.load(), 1U);
  EXPECT_EQ(observations_->constructions.load(), 0U);
  const auto &plan = *ProcessConfigAccess::plans(*built.snapshot).front();
  EXPECT_EQ(BotInstallationAssembler::describe(plan).recipe_id,
            "test.echo.memory");
  EXPECT_EQ(BotInstallationAssembler::validate(plan).lifecycle_order,
            (std::vector<std::size_t>{0, 1}));
  EXPECT_EQ(observations_->constructions.load(), 0U);
  EXPECT_FALSE(built.snapshot->get_section("bots.echo.connection"));
  std::ostringstream public_values;
  public_values << *built.snapshot->get_section("bots");
  EXPECT_EQ(public_values.str().find("PRIVATE_ECHO_SECRET"), std::string::npos);
  EXPECT_FALSE(catalog_->supports(obcx::bot::SurfaceId{"onebot11.qq"}));
  EXPECT_FALSE(catalog_->supports(obcx::bot::SurfaceId{"telegram.bot_api"}));

  auto installation = BotInstallationAssembler::assemble(plan);
  ASSERT_EQ(observations_->constructions.load(), 2U);
  EXPECT_EQ(observations_->prepares.load(), 0U);
  EXPECT_EQ(observations_->starts.load(), 0U);
  installation->start();
  EXPECT_EQ(observations_->prepares.load(), 2U);
  EXPECT_EQ(observations_->starts.load(), 2U);
  BotOperationDispatcher gateway{[catalog = catalog_](const auto &surface) {
    return catalog->supports(surface);
  }};
  gateway.register_endpoint(installation->capability<OperationRegistry>(
      CapabilityId{"bot.operations"}));
  gateway.seal_registrations();
  const obcx::bot::BotInstallationRef identity{"echo", echo::surface};
  const auto supported = gateway.supported_actions(identity);
  ASSERT_TRUE(supported.ok());
  EXPECT_EQ(supported.value->actions, plan.recipe().advertised_actions);
  const auto result = obcx::tests::await_sdk(
      obcx::bot::invoke(gateway, echo::Request{identity, "hello"}));
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value->text, "echo:hello");
  EXPECT_EQ(observations_->calls.load(), 1U);

  auto unknown = obcx::tests::await_sdk(
      gateway.invoke({identity, obcx::bot::ActionId{"test.echo.unknown"},
                      obcx::bot::Json::object()}));
  ASSERT_FALSE(unknown.ok());
  EXPECT_EQ(unknown.error->code,
            obcx::bot::BotOperationErrorCode::UnsupportedAction);
  const obcx::bot::Json forged =
      echo::Request{{"another", echo::surface}, "hello"};
  const auto conflict = obcx::tests::await_sdk(
      gateway.invoke({identity, echo::Request::action, forged}));
  EXPECT_FALSE(conflict.ok());
  EXPECT_EQ(observations_->calls.load(), 1U);
  installation->stop();
  EXPECT_EQ(observations_->stopped,
            (std::vector<std::string>{"operations", "connection"}));
  EXPECT_FALSE(obcx::tests::await_sdk(
                   obcx::bot::invoke(gateway, echo::Request{identity, "late"}))
                   .ok());
  EXPECT_EQ(observations_->calls.load(), 1U);
  installation.reset();
  EXPECT_EQ(observations_->stopped.size(), 2U);
}

TEST_F(
    EchoRuntimeTest,
    DisabledPrivateConfigurationIsValidatedAndFingerprintedWithoutConstruction) {
  const auto first = parse("FIRST_SECRET", false);
  const auto second = parse("SECOND_SECRET", false);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(observations_->parses.load(), 2U);
  EXPECT_EQ(observations_->constructions.load(), 0U);
  EXPECT_EQ(first.snapshot->get_bot_configs(),
            second.snapshot->get_bot_configs());
  EXPECT_NE(first.snapshot->process_owned_fingerprint({1, 1, 1}),
            second.snapshot->process_owned_fingerprint({1, 1, 1}));
  const auto invalid = parse("", false);
  ASSERT_FALSE(invalid);
  ASSERT_TRUE(invalid.diagnostic);
  EXPECT_EQ(invalid.diagnostic->path, "bots.echo.connection.secret");
  EXPECT_EQ(observations_->constructions.load(), 0U);
}
} // namespace
