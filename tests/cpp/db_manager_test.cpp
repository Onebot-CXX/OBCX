#include "common/config_snapshot.hpp"
#include "core/infrastructure/db_manager.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using namespace obcx::common;
using namespace obcx::core;

namespace {

auto temp_db_path(const std::string &name) -> std::filesystem::path {
  return std::filesystem::temp_directory_path() /
         ("obcx_db_manager_" + name + "_" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()) +
          ".sqlite3");
}

auto sqlite_config(const std::string &name, const std::filesystem::path &path)
    -> DbInstanceConfig {
  DbInstanceConfig config;
  config.name = name;
  config.type = "sqlite";
  config.path = path.string();
  return config;
}

} // namespace

TEST(DbManagerTest, ConfiguresSqliteInstanceAndRunsReadWriteWork) {
  const auto db_path = temp_db_path("read_write");
  DbManager manager;
  manager.configure({sqlite_config("main", db_path)});

  const auto inserted =
      manager.run_write<int>("main", [](IDbConnection &connection) {
        connection.execute(
            "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL);");
        connection.execute("INSERT INTO items (name) VALUES (?);",
                           {std::string{"alpha"}});
        return 1;
      });

  EXPECT_EQ(inserted, 1);

  const auto name =
      manager.run_read<std::string>("main", [](IDbConnection &connection) {
        const auto rows = connection.query(
            "SELECT name FROM items WHERE id = ?;", {std::int64_t{1}});
        EXPECT_EQ(rows.size(), 1);
        return std::get<std::string>(rows[0].at("name"));
      });

  EXPECT_EQ(name, "alpha");
}

TEST(DbManagerTest, ReportsMissingInstances) {
  DbManager manager;

  EXPECT_THROW((void)manager.connection("missing"), std::out_of_range);
}

TEST(DbManagerTest, ValidatesProvidersWithoutOpeningConnections) {
  const auto db_path = temp_db_path("validation_only");
  DbManager manager;

  auto valid = sqlite_config("main", db_path);
  EXPECT_TRUE(manager.validate_configs({valid}).empty());
  EXPECT_FALSE(std::filesystem::exists(db_path));

  auto unsupported = valid;
  unsupported.type = "postgres";
  const auto unsupported_errors = manager.validate_configs({unsupported});
  ASSERT_EQ(unsupported_errors.size(), 1);
  EXPECT_EQ(unsupported_errors.front(),
            "Unsupported DB instance type: postgres");

  valid.path.clear();
  const auto invalid_errors = manager.validate_configs({valid});
  ASSERT_EQ(invalid_errors.size(), 1);
  EXPECT_NE(invalid_errors.front().find("SQLite DB instance requires path"),
            std::string::npos);
}

TEST(DbManagerTest, RunsWritesOnDedicatedWriterThread) {
  const auto db_path = temp_db_path("writer_thread");
  DbManager manager;
  manager.configure({sqlite_config("main", db_path)});

  const auto caller_thread = std::this_thread::get_id();
  const auto writer_thread =
      manager.run_write<std::thread::id>("main", [](IDbConnection &connection) {
        connection.execute("CREATE TABLE writer_probe (id INTEGER);");
        return std::this_thread::get_id();
      });

  EXPECT_NE(writer_thread, caller_thread);
}

TEST(DbManagerTest, TransactionsCommitOrRollBackAtomically) {
  const auto db_path = temp_db_path("transaction");
  DbManager manager;
  manager.configure({sqlite_config("main", db_path)});
  manager.run_write<void>("main", [](IDbConnection &connection) {
    connection.execute("CREATE TABLE transaction_probe (value INTEGER);");
  });

  const auto committed = manager.run_transaction<std::int64_t>(
      "main", [](IDbConnection &connection) {
        connection.execute("INSERT INTO transaction_probe(value) VALUES (?);",
                           {std::int64_t{1}});
        return std::int64_t{7};
      });
  EXPECT_EQ(committed, 7);

  EXPECT_THROW(manager.run_transaction<void>(
                   "main",
                   [](IDbConnection &connection) {
                     connection.execute(
                         "INSERT INTO transaction_probe(value) VALUES (?);",
                         {std::int64_t{2}});
                     throw std::runtime_error("rollback probe");
                   }),
               std::runtime_error);
  const auto rows = manager.run_read<std::vector<DbRow>>(
      "main", [](IDbConnection &connection) {
        return connection.query(
            "SELECT value FROM transaction_probe ORDER BY value;");
      });
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(std::get<std::int64_t>(rows.front().at("value")), 1);
}

TEST(DbManagerTest, RunsMigrationWorkUnderMigrationLock) {
  const auto db_path = temp_db_path("migration_lock");
  DbManager manager;
  manager.configure({sqlite_config("main", db_path)});

  manager.with_migration_lock(
      "main", "message_store", [](IDbConnection &connection) {
        connection.execute("CREATE TABLE message_store_schema "
                           "(version INTEGER NOT NULL);");
        connection.execute("INSERT INTO message_store_schema "
                           "(version) VALUES (?);",
                           {std::int64_t{1}});
      });

  const auto version =
      manager.run_read<std::int64_t>("main", [](IDbConnection &connection) {
        const auto rows =
            connection.query("SELECT version FROM message_store_schema;");
        return std::get<std::int64_t>(rows[0].at("version"));
      });

  EXPECT_EQ(version, 1);
}

namespace {

class TestProviderConnection final : public IDbConnection {
public:
  explicit TestProviderConnection(std::string dsn) : dsn_(std::move(dsn)) {}

  void execute(const std::string &, const DbParams & = {}) override {}

  [[nodiscard]] auto query(const std::string &, const DbParams & = {})
      -> std::vector<DbRow> override {
    return {DbRow{{"dsn", dsn_}}};
  }

  void run_write_task(std::function<void(IDbConnection &)> work) override {
    work(*this);
  }

  void run_transaction_task(
      std::function<void(IDbConnection &)> work) override {
    work(*this);
  }

  void with_migration_lock(const std::string &,
                           std::function<void(IDbConnection &)> work) override {
    work(*this);
  }

private:
  std::string dsn_;
};

class TestProvider final : public IDbProvider {
public:
  [[nodiscard]] auto create_connection(const DbInstanceConfig &config)
      -> std::shared_ptr<IDbConnection> override {
    opened_dsn = config.dsn;
    return std::make_shared<TestProviderConnection>(config.dsn);
  }

  std::string opened_dsn;
};

} // namespace

TEST(DbManagerTest, UsesRegisteredProviderForConfiguredDbType) {
  auto provider = std::make_shared<TestProvider>();

  DbInstanceConfig config;
  config.name = "analytics";
  config.type = "custom";
  config.dsn = "memory://analytics";

  DbManager manager;
  manager.register_provider("custom", provider);
  manager.configure({config});

  EXPECT_EQ(provider->opened_dsn, "memory://analytics");

  const auto dsn =
      manager.run_read<std::string>("analytics", [](IDbConnection &connection) {
        const auto rows = connection.query("SELECT dsn");
        return std::get<std::string>(rows.front().at("dsn"));
      });
  EXPECT_EQ(dsn, "memory://analytics");
}

TEST(DbManagerTest, ReusesSharedManagerForEquivalentConfigs) {
  DbManager::reset_shared_managers_for_tests();

  const auto db_path = temp_db_path("shared_manager");
  auto config = sqlite_config("main", db_path);

  auto first = DbManager::shared_manager({config});
  auto second = DbManager::shared_manager({config});

  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first, second);

  first->run_write<void>("main", [](IDbConnection &connection) {
    connection.execute("CREATE TABLE shared_probe (value TEXT NOT NULL);");
    connection.execute("INSERT INTO shared_probe (value) VALUES (?);",
                       {std::string{"same-manager"}});
  });

  const auto value =
      second->run_read<std::string>("main", [](IDbConnection &connection) {
        const auto rows =
            connection.query("SELECT value FROM shared_probe LIMIT 1;");
        return std::get<std::string>(rows.front().at("value"));
      });

  EXPECT_EQ(value, "same-manager");
  DbManager::reset_shared_managers_for_tests();
}
