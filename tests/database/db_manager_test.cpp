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
