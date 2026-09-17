#include "common/config_snapshot.hpp"
#include "onebot11/bot/configuration.hpp"
#include "support/bot_platform_fixture.hpp"
#include "telegram/bot/configuration.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace fs = std::filesystem;
using BotInstallationConfig = obcx::common::BotInstallationMetadata;
using obcx::common::ConfigLoader;
using obcx::common::RuntimeConfigBuildResult;

class BotInstallationConfigTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto *test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    ASSERT_NE(test_info, nullptr);
    root_ = fs::temp_directory_path() / "obcx_bot_installation_config_test" /
            test_info->name();
    fs::remove_all(root_);
    fs::create_directories(root_);
  }

  void TearDown() override { fs::remove_all(root_); }

  auto parse(std::string_view document) -> RuntimeConfigBuildResult {
    const auto path = root_ / "config.toml";
    std::ofstream output(path);
    output << document;
    output.close();
    return ConfigLoader::build_snapshot(path.string(),
                                        obcx::test::bot_platform_catalog());
  }

  fs::path root_;
};

auto find_installation(const std::vector<BotInstallationConfig> &configs,
                       const std::string_view id)
    -> const BotInstallationConfig * {
  const auto found =
      std::ranges::find(configs, id, &BotInstallationConfig::installation_id);
  return found == configs.end() ? nullptr : &*found;
}

TEST_F(BotInstallationConfigTest, ParsesAllSupportedModulePlans) {
  const std::string document = R"(
[bots.qq_ws]
enabled = true
surface = "onebot11.qq"
transport = "websocket"

[bots.qq_ws.connection]
host = "127.0.0.1"
port = 3001
access_token = ""
connect_timeout_ms = 4000
action_timeout_ms = 25000

[bots.qq_http]
enabled = false
surface = "onebot11.qq"
transport = "http"

[bots.qq_http.connection]
host = "onebot.internal"
port = 8443
access_token = ""
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 750

[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"

[bots.telegram.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_BOT_TOKEN"
bot_username = "fixture_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
proxy_host = "127.0.0.1"
proxy_port = 1080
proxy_type = "socks5"
proxy_username = "fixture"
proxy_password = "YOUR_PROXY_PASSWORD"
poll_timeout_ms = 20000
poll_force_close_ms = 25000
poll_retry_interval_ms = 1200
)";
  const auto built = parse(document);
  const auto raw = toml::parse(document);
  ASSERT_TRUE(built) << (built.diagnostic ? built.diagnostic->code : "");
  const auto configs = built.snapshot->get_bot_configs();
  ASSERT_EQ(configs.size(), 3U);

  const auto *qq_ws = find_installation(configs, "qq_ws");
  ASSERT_NE(qq_ws, nullptr);
  EXPECT_EQ(qq_ws->surface, obcx::bot::SurfaceId{"onebot11.qq"});
  EXPECT_EQ(qq_ws->transport, "websocket");
  const auto &websocket = obcx::onebot11::configuration::parse_websocket(
      *raw.at_path("bots.qq_ws.connection").as_table(),
      "bots.qq_ws.connection");
  EXPECT_EQ(websocket.host, "127.0.0.1");
  EXPECT_EQ(websocket.port, 3001);
  EXPECT_EQ(websocket.connect_timeout, std::chrono::milliseconds{4000});
  EXPECT_EQ(websocket.action_timeout, std::chrono::milliseconds{25000});

  const auto *qq_http = find_installation(configs, "qq_http");
  ASSERT_NE(qq_http, nullptr);
  EXPECT_FALSE(qq_http->enabled);
  const auto &onebot_http = obcx::onebot11::configuration::parse_http(
      *raw.at_path("bots.qq_http.connection").as_table(),
      "bots.qq_http.connection");
  EXPECT_TRUE(onebot_http.use_tls);
  EXPECT_EQ(onebot_http.poll_interval, std::chrono::milliseconds{750});

  const auto *telegram = find_installation(configs, "telegram");
  ASSERT_NE(telegram, nullptr);
  EXPECT_EQ(telegram->surface, obcx::bot::SurfaceId{"telegram.bot_api"});
  EXPECT_EQ(telegram->transport, "http");
  const auto &telegram_http = obcx::telegram::configuration::parse_http(
      *raw.at_path("bots.telegram.connection").as_table(),
      "bots.telegram.connection");
  EXPECT_EQ(telegram_http.host, "api.telegram.org");
  EXPECT_EQ(telegram_http.port, 443);
  EXPECT_TRUE(telegram_http.use_tls);
  EXPECT_EQ(telegram_http.bot_username, "fixture_bot");
  ASSERT_TRUE(telegram_http.proxy.has_value());
  EXPECT_EQ(telegram_http.proxy->type,
            obcx::telegram::configuration::ProxyType::Socks5);
  EXPECT_EQ(telegram_http.proxy->port, 1080);
}

TEST_F(BotInstallationConfigTest,
       RejectsOmittedConnectionOptionsInsteadOfApplyingDefaults) {
  for (const auto &[document, path] :
       std::vector<std::pair<std::string, std::string>>{
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"
[bots.qq.connection]
)",
            "bots.qq.connection.host"},
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq.connection]
)",
            "bots.qq.connection.host"},
           {R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = "YOUR_TOKEN"
)",
            "bots.telegram.connection.host"}}) {
    const auto built = parse(document);
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic.has_value());
    EXPECT_EQ(built.diagnostic->code, "missing_bot_configuration_value");
    EXPECT_EQ(built.diagnostic->path, path);
  }
}

TEST_F(BotInstallationConfigTest, EveryTelegramOptionRemainsExplicit) {
  const std::string connection = R"(
[bots.telegram.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_BOT_TOKEN"
bot_username = "fixture_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 20000
poll_force_close_ms = 25000
poll_retry_interval_ms = 1200
proxy_host = "127.0.0.1"
proxy_port = 1080
proxy_type = "socks5"
proxy_username = ""
proxy_password = ""
)";
  for (const auto enabled : {true, false}) {
    const auto header =
        std::string{"[bots.telegram]\nenabled = "} +
        (enabled ? "true" : "false") +
        "\nsurface = \"telegram.bot_api\"\ntransport = \"http\"\n";
    ASSERT_TRUE(parse(header + connection));
    for (const auto field :
         {"host", "port", "access_token", "bot_username", "use_tls",
          "connect_timeout_ms", "action_timeout_ms", "poll_timeout_ms",
          "poll_force_close_ms", "poll_retry_interval_ms", "proxy_type",
          "proxy_username", "proxy_password"}) {
      SCOPED_TRACE(std::string{field} + (enabled ? " enabled" : " disabled"));
      auto missing = connection;
      const auto start = missing.find(std::string{"\n"} + field + " = ");
      ASSERT_NE(start, std::string::npos);
      const auto end = missing.find('\n', start + 1);
      missing.erase(start, end - start);
      const auto built = parse(header + missing);
      ASSERT_FALSE(built);
      ASSERT_TRUE(built.diagnostic.has_value());
      // Preserve the existing boolean diagnostic as part of this baseline.
      EXPECT_EQ(built.diagnostic->code,
                std::string_view{field} == "use_tls"
                    ? "invalid_bot_configuration_value"
                    : "missing_bot_configuration_value");
      EXPECT_EQ(built.diagnostic->path,
                std::string{"bots.telegram.connection."} + field);
    }
  }
}

TEST_F(BotInstallationConfigTest, RejectsUnknownKeyByExactPath) {
  const auto built = parse(R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "websocket"
[bots.qq.connection]
misspelled_timeout_ms = 10
)");
  ASSERT_FALSE(built);
  ASSERT_TRUE(built.diagnostic.has_value());
  EXPECT_EQ(built.diagnostic->code, "unknown_bot_configuration_key");
  EXPECT_EQ(built.diagnostic->path, "bots.qq.connection.misspelled_timeout_ms");
}

TEST_F(BotInstallationConfigTest, RejectsUnsupportedSurfacesAndTransports) {
  for (const auto &[document, code] :
       std::vector<std::pair<std::string, std::string>>{
           {R"(
[bots.official]
enabled = true
surface = "qq.official"
transport = "http"
[bots.official.connection]
)",
            "unsupported_bot_surface"},
           {R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "websocket"
[bots.telegram.connection]
access_token = "YOUR_TOKEN"
)",
            "unsupported_bot_surface_transport"},
           {R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "ws"
[bots.qq.connection]
)",
            "unsupported_bot_transport"}}) {
    const auto built = parse(document);
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic.has_value());
    EXPECT_EQ(built.diagnostic->code, code);
  }
}

TEST_F(BotInstallationConfigTest,
       RejectsInvalidCredentialsDurationsAndProxyCombinations) {
  for (const auto &document : std::vector<std::string>{
           R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
host = "api.telegram.org"
port = 443
access_token = ""
bot_username = "fixture_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000
)",
           R"(
[bots.qq]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq.connection]
host = "localhost"
port = 3000
access_token = ""
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 0
poll_interval_ms = 1000
)",
           R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TOKEN"
bot_username = "fixture_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000
proxy_type = "socks5"
)",
           R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TOKEN"
bot_username = "fixture_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 30000
poll_force_close_ms = 20000
poll_retry_interval_ms = 3000
)"}) {
    const auto built = parse(document);
    ASSERT_FALSE(built);
    ASSERT_TRUE(built.diagnostic.has_value());
    EXPECT_TRUE(built.diagnostic->code.starts_with("invalid_bot"));
  }
}

TEST_F(BotInstallationConfigTest, SchemaDiagnosticsNeverContainCredentials) {
  constexpr std::string_view credential =
      "123456789:THIS_VALUE_MUST_NEVER_APPEAR_IN_A_DIAGNOSTIC";
  const auto built = parse(R"(
[bots.telegram]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.telegram.connection]
access_token = "123456789:THIS_VALUE_MUST_NEVER_APPEAR_IN_A_DIAGNOSTIC"
ignored = true
)");
  ASSERT_FALSE(built);
  ASSERT_TRUE(built.diagnostic.has_value());
  EXPECT_EQ(built.diagnostic->code.find(credential), std::string::npos);
  EXPECT_EQ(built.diagnostic->path.find(credential), std::string::npos);
  EXPECT_EQ(built.diagnostic->message.find(credential), std::string::npos);
}

TEST_F(BotInstallationConfigTest, PublicViewsContainNoBotConnectionData) {
  auto connection = obcx::test::connection_fixture(
      obcx::bot::SurfaceId{"telegram.bot_api"}, "http");
  connection.insert_or_assign("access_token", "BOT_SECRET_VALUE");
  connection.insert("proxy_host", "private-proxy.example");
  connection.insert("proxy_port", 1080);
  connection.insert("proxy_type", "socks5");
  connection.insert("proxy_username", "PRIVATE_PROXY_USER");
  connection.insert("proxy_password", "PRIVATE_PROXY_PASSWORD");
  const toml::table document{
      {"bots",
       toml::table{{"telegram", toml::table{{"enabled", false},
                                            {"surface", "telegram.bot_api"},
                                            {"transport", "http"},
                                            {"connection", connection}}}}}};
  std::ostringstream encoded;
  encoded << document;
  const auto built = parse(encoded.str());
  ASSERT_TRUE(built) << (built.diagnostic ? built.diagnostic->message : "");
  const auto bots = built.snapshot->get_section("bots");
  ASSERT_TRUE(bots);
  std::ostringstream public_document;
  public_document << *bots;
  for (const auto secret :
       {"BOT_SECRET_VALUE", "PRIVATE_PROXY_USER", "PRIVATE_PROXY_PASSWORD",
        "private-proxy.example"}) {
    EXPECT_EQ(public_document.str().find(secret), std::string::npos);
  }
  EXPECT_FALSE(built.snapshot->get_section("bots.telegram.connection"));
  EXPECT_FALSE(built.snapshot->get_value<std::string>(
      "bots.telegram.connection.access_token"));
  obcx::common::ActorConfigView view{built.snapshot, "bridge"};
  EXPECT_FALSE(view.get_root_section("bots.telegram.connection"));
  const auto &plans = obcx::core::ProcessConfigAccess::plans(*built.snapshot);
  ASSERT_EQ(plans.size(), 1U);
  EXPECT_EQ(plans.front()->metadata(),
            built.snapshot->get_bot_configs().front());
  EXPECT_EQ(plans.front()->fingerprint().size(), 64U);
}

TEST_F(BotInstallationConfigTest,
       DisabledSecretChangesAlterOnlyPrivateFingerprint) {
  auto connection = obcx::test::connection_fixture(
      obcx::bot::SurfaceId{"telegram.bot_api"}, "http");
  auto snapshot_for = [&](std::string secret) {
    connection.insert_or_assign("access_token", secret);
    const toml::table document{
        {"bots",
         toml::table{{"telegram", toml::table{{"enabled", false},
                                              {"surface", "telegram.bot_api"},
                                              {"transport", "http"},
                                              {"connection", connection}}}}}};
    std::ostringstream encoded;
    encoded << document;
    return parse(encoded.str());
  };
  const auto first = snapshot_for("PRIVATE_FIRST_TOKEN");
  const auto second = snapshot_for("PRIVATE_SECOND_TOKEN");
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first.snapshot->get_bot_configs(),
            second.snapshot->get_bot_configs());
  const obcx::common::RuntimeThreadFingerprintInput budget{1, 1, 1};
  const auto before = first.snapshot->process_owned_fingerprint(budget);
  const auto after = second.snapshot->process_owned_fingerprint(budget);
  EXPECT_NE(before.bots, after.bots);
  EXPECT_EQ(obcx::common::changed_process_owned_domains(before, after),
            std::vector<std::string>{"bots"});
}

TEST_F(BotInstallationConfigTest, ActorOnlyViewsCannotBecomeProcessSnapshots) {
  const auto actor = obcx::common::ActorConfigSnapshotBuilder::build(
      toml::table{}, {}, "actor-fixture.toml");
  ASSERT_TRUE(actor);
  obcx::common::ConfigLoader loader{obcx::test::bot_platform_catalog()};
  EXPECT_THROW(loader.publish_snapshot(actor.snapshot), std::invalid_argument);
  EXPECT_THROW((void)obcx::core::ProcessConfigAccess::plans(*actor.snapshot),
               std::invalid_argument);
  EXPECT_THROW((void)actor.snapshot->process_owned_fingerprint({1, 1, 1}),
               std::invalid_argument);
}

TEST_F(BotInstallationConfigTest,
       CatalogMustBeExplicitAndSealedBeforeFileRead) {
  EXPECT_FALSE(ConfigLoader::build_snapshot("does-not-exist.toml", nullptr));
  auto unsealed = std::make_shared<obcx::core::BotPlatformCatalog>();
  const auto built =
      ConfigLoader::build_snapshot("does-not-exist.toml", unsealed);
  ASSERT_FALSE(built);
  ASSERT_TRUE(built.diagnostic);
  EXPECT_EQ(built.diagnostic->code, "bot_platform_catalog_unavailable");
  EXPECT_THROW((void)ConfigLoader(unsealed), std::invalid_argument);
}

} // namespace
