#include "core/infrastructure/db_manager.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace obcx::core {
namespace {

class Statement {
public:
  Statement(sqlite3 *db, const std::string &sql) : db_(db) {
    const auto rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
      throw std::runtime_error(sqlite3_errmsg(db_));
    }
  }

  ~Statement() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }

  Statement(const Statement &) = delete;
  auto operator=(const Statement &) -> Statement & = delete;

  [[nodiscard]] auto get() const -> sqlite3_stmt * { return stmt_; }

private:
  sqlite3 *db_;
  sqlite3_stmt *stmt_ = nullptr;
};

void bind_value(sqlite3_stmt *stmt, const int index, const DbValue &value) {
  std::visit(
      [stmt, index](const auto &typed_value) {
        using Value = std::decay_t<decltype(typed_value)>;
        if constexpr (std::is_same_v<Value, std::nullptr_t>) {
          sqlite3_bind_null(stmt, index);
        } else if constexpr (std::is_same_v<Value, std::int64_t>) {
          sqlite3_bind_int64(stmt, index, typed_value);
        } else if constexpr (std::is_same_v<Value, double>) {
          sqlite3_bind_double(stmt, index, typed_value);
        } else if constexpr (std::is_same_v<Value, std::string>) {
          sqlite3_bind_text(stmt, index, typed_value.c_str(), -1,
                            SQLITE_TRANSIENT);
        }
      },
      value);
}

void bind_params(sqlite3_stmt *stmt, const DbParams &params) {
  for (size_t index = 0; index < params.size(); ++index) {
    bind_value(stmt, static_cast<int>(index + 1), params[index]);
  }
}

auto column_value(sqlite3_stmt *stmt, const int column) -> DbValue {
  switch (sqlite3_column_type(stmt, column)) {
  case SQLITE_INTEGER:
    return static_cast<std::int64_t>(sqlite3_column_int64(stmt, column));
  case SQLITE_FLOAT:
    return sqlite3_column_double(stmt, column);
  case SQLITE_TEXT: {
    const auto *text = sqlite3_column_text(stmt, column);
    return text == nullptr ? std::string{}
                           : std::string{reinterpret_cast<const char *>(text)};
  }
  case SQLITE_NULL:
    return nullptr;
  case SQLITE_BLOB: {
    const auto *blob = sqlite3_column_blob(stmt, column);
    const auto size = sqlite3_column_bytes(stmt, column);
    if (blob == nullptr || size <= 0) {
      return std::string{};
    }
    return std::string{static_cast<const char *>(blob),
                       static_cast<size_t>(size)};
  }
  default:
    return nullptr;
  }
}

class SQLiteDbConnection final : public IDbConnection {
public:
  explicit SQLiteDbConnection(std::filesystem::path path)
      : path_(std::move(path)) {
    open();
    execute("PRAGMA foreign_keys = ON;");
    execute("PRAGMA busy_timeout = 5000;");
    execute("PRAGMA journal_mode = WAL;");
    start_writer();
  }

  ~SQLiteDbConnection() override {
    stop_writer();
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

  void execute(const std::string &sql, const DbParams &params = {}) override {
    std::scoped_lock lock(mutex_);
    Statement stmt(db_, sql);
    bind_params(stmt.get(), params);

    while (true) {
      const auto rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        return;
      }
      if (rc == SQLITE_ROW) {
        continue;
      }
      throw std::runtime_error(sqlite3_errmsg(db_));
    }
  }

  [[nodiscard]] auto query(const std::string &sql, const DbParams &params = {})
      -> std::vector<DbRow> override {
    std::scoped_lock lock(mutex_);
    Statement stmt(db_, sql);
    bind_params(stmt.get(), params);

    std::vector<DbRow> rows;
    while (true) {
      const auto rc = sqlite3_step(stmt.get());
      if (rc == SQLITE_DONE) {
        return rows;
      }
      if (rc != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(db_));
      }

      DbRow row;
      const auto columns = sqlite3_column_count(stmt.get());
      for (int column = 0; column < columns; ++column) {
        const auto *name = sqlite3_column_name(stmt.get(), column);
        row[name == nullptr ? std::to_string(column) : std::string{name}] =
            column_value(stmt.get(), column);
      }
      rows.push_back(std::move(row));
    }
  }

  void run_write_task(std::function<void(IDbConnection &)> work) override {
    if (is_writer_thread()) {
      work(*this);
      return;
    }

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    {
      std::scoped_lock lock(writer_mutex_);
      if (stop_writer_) {
        throw std::runtime_error("SQLite writer is stopped");
      }
      write_queue_.push_back([this, work = std::move(work), promise]() mutable {
        try {
          work(*this);
          promise->set_value();
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
    }

    writer_cv_.notify_one();
    future.get();
  }

  void run_transaction_task(
      std::function<void(IDbConnection &)> work) override {
    run_write_task(
        [this, work = std::move(work)](IDbConnection &base_connection) mutable {
          std::scoped_lock db_lock(mutex_);
          bool transaction_started = false;
          try {
            execute("BEGIN IMMEDIATE;");
            transaction_started = true;
            work(base_connection);
            execute("COMMIT;");
            transaction_started = false;
          } catch (...) {
            if (transaction_started) {
              try {
                execute("ROLLBACK;");
              } catch (...) {
              }
            }
            throw;
          }
        });
  }

  void with_migration_lock(const std::string &namespace_name,
                           std::function<void(IDbConnection &)> work) override {
    if (namespace_name.empty()) {
      throw std::invalid_argument("DB migration namespace cannot be empty");
    }

    run_transaction_task([namespace_name, work = std::move(work)](
                             IDbConnection &connection) mutable {
      connection.execute("CREATE TABLE IF NOT EXISTS obcx_migration_locks ("
                         "namespace TEXT PRIMARY KEY NOT NULL,"
                         "locked_at INTEGER NOT NULL"
                         ");");

      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      connection.execute("INSERT OR REPLACE INTO obcx_migration_locks "
                         "(namespace, locked_at) VALUES (?, ?);",
                         {namespace_name, static_cast<std::int64_t>(now)});
      work(connection);
    });
  }

private:
  void open() {
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    const auto rc = sqlite3_open(path_.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
      std::string message =
          db_ != nullptr ? sqlite3_errmsg(db_) : "failed to open sqlite DB";
      if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
      throw std::runtime_error(message);
    }
  }

  void start_writer() {
    writer_ = std::thread([this]() { writer_loop(); });
  }

  void stop_writer() {
    {
      std::scoped_lock lock(writer_mutex_);
      stop_writer_ = true;
    }
    writer_cv_.notify_one();

    if (writer_.joinable()) {
      writer_.join();
    }
  }

  [[nodiscard]] auto is_writer_thread() const -> bool {
    std::scoped_lock lock(writer_mutex_);
    return writer_thread_id_ == std::this_thread::get_id();
  }

  void writer_loop() {
    {
      std::scoped_lock lock(writer_mutex_);
      writer_thread_id_ = std::this_thread::get_id();
    }

    while (true) {
      std::function<void()> task;
      {
        std::unique_lock lock(writer_mutex_);
        writer_cv_.wait(
            lock, [this]() { return stop_writer_ || !write_queue_.empty(); });

        if (stop_writer_ && write_queue_.empty()) {
          return;
        }

        task = std::move(write_queue_.front());
        write_queue_.pop_front();
      }

      task();
    }
  }

  std::filesystem::path path_;
  sqlite3 *db_ = nullptr;
  std::recursive_mutex mutex_;
  std::thread writer_;
  std::thread::id writer_thread_id_;
  mutable std::mutex writer_mutex_;
  std::condition_variable writer_cv_;
  std::deque<std::function<void()>> write_queue_;
  bool stop_writer_ = false;
};

class SQLiteDbProvider final : public IDbProvider {
public:
  void validate_config(const common::DbInstanceConfig &config) const override {
    if (config.path.empty()) {
      throw std::invalid_argument("SQLite DB instance requires path");
    }
  }

  [[nodiscard]] auto create_connection(const common::DbInstanceConfig &config)
      -> std::shared_ptr<IDbConnection> override {
    validate_config(config);
    return std::make_shared<SQLiteDbConnection>(config.path);
  }
};

struct SharedManagerPool {
  std::mutex mutex;
  std::unordered_map<std::string, std::weak_ptr<DbManager>> managers;
};

auto shared_manager_pool() -> SharedManagerPool & {
  static SharedManagerPool pool;
  return pool;
}

auto shared_manager_key(std::vector<common::DbInstanceConfig> configs)
    -> std::string {
  std::ranges::sort(configs, {}, &common::DbInstanceConfig::name);

  std::string key;
  for (const auto &config : configs) {
    key.append(config.name);
    key.push_back('\x1f');
    key.append(config.type);
    key.push_back('\x1f');
    key.append(config.path);
    key.push_back('\x1f');
    key.append(config.dsn);
    key.push_back('\x1e');
  }
  return key;
}

} // namespace

DbManager::DbManager() {
  register_provider("sqlite", std::make_shared<SQLiteDbProvider>());
}

auto DbManager::shared_manager(std::vector<common::DbInstanceConfig> configs)
    -> std::shared_ptr<DbManager> {
  const auto key = shared_manager_key(configs);
  auto &pool = shared_manager_pool();
  std::scoped_lock lock(pool.mutex);

  if (const auto existing = pool.managers[key].lock()) {
    return existing;
  }

  auto manager = std::make_shared<DbManager>();
  manager->configure(configs);
  pool.managers[key] = manager;
  return manager;
}

void DbManager::reset_shared_managers_for_tests() {
  auto &pool = shared_manager_pool();
  std::scoped_lock lock(pool.mutex);
  pool.managers.clear();
}

void DbManager::register_provider(std::string type,
                                  std::shared_ptr<IDbProvider> provider) {
  if (type.empty()) {
    throw std::invalid_argument("DB provider type cannot be empty");
  }
  if (!provider) {
    throw std::invalid_argument("DB provider cannot be null");
  }
  providers_[std::move(type)] = std::move(provider);
}

void DbManager::configure(
    const std::vector<common::DbInstanceConfig> &configs) {
  instances_.clear();

  for (const auto &config : configs) {
    if (config.name.empty()) {
      throw std::invalid_argument("DB instance name cannot be empty");
    }
    if (config.type.empty()) {
      throw std::invalid_argument("DB instance type cannot be empty");
    }

    const auto provider = providers_.find(config.type);
    if (provider == providers_.end()) {
      throw std::invalid_argument("Unsupported DB instance type: " +
                                  config.type);
    }

    auto connection = provider->second->create_connection(config);
    if (!connection) {
      throw std::runtime_error("DB provider returned null connection for: " +
                               config.name);
    }
    instances_[config.name] = std::move(connection);
  }
}

auto DbManager::validate_configs(
    const std::vector<common::DbInstanceConfig> &configs) const
    -> std::vector<std::string> {
  std::vector<std::string> errors;
  for (const auto &config : configs) {
    if (config.name.empty()) {
      errors.emplace_back("DB instance name cannot be empty");
      continue;
    }
    if (config.type.empty()) {
      errors.emplace_back("DB instance " + config.name +
                          " has an empty provider type");
      continue;
    }
    const auto provider = providers_.find(config.type);
    if (provider == providers_.end()) {
      errors.emplace_back("Unsupported DB instance type: " + config.type);
      continue;
    }
    try {
      provider->second->validate_config(config);
    } catch (const std::exception &error) {
      errors.emplace_back("Invalid DB instance " + config.name + ": " +
                          error.what());
    } catch (...) {
      errors.emplace_back("Invalid DB instance " + config.name);
    }
  }
  return errors;
}

auto DbManager::connection(const std::string &instance_name) const
    -> std::shared_ptr<IDbConnection> {
  const auto it = instances_.find(instance_name);
  if (it == instances_.end()) {
    throw std::out_of_range("DB instance not configured: " + instance_name);
  }
  return it->second;
}

void DbManager::with_migration_lock(
    const std::string &instance_name, const std::string &namespace_name,
    std::function<void(IDbConnection &)> work) const {
  connection(instance_name)
      ->with_migration_lock(namespace_name, std::move(work));
}

} // namespace obcx::core
