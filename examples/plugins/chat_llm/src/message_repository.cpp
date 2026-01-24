#include <chat_llm/message_repository.hpp>
#include <common/logger.hpp>
#include <filesystem>
#include <sqlite3.h>

namespace plugins::chat_llm {

static constexpr int SCHEMA_VERSION_V2 = 2;

MessageRepository::MessageRepository(const std::string &db_path)
    : db_path_(db_path), db_(nullptr) {}

MessageRepository::~MessageRepository() {
  if (db_) {
    sqlite3_close(static_cast<sqlite3 *>(db_));
  }
}

auto MessageRepository::initialize() -> bool {
  // Ensure parent directory exists
  std::filesystem::path db_path(db_path_);
  std::filesystem::path parent_dir = db_path.parent_path();
  if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
    std::filesystem::create_directories(parent_dir);
  }

  // Open database
  sqlite3 *db = nullptr;
  int rc = sqlite3_open_v2(
      db_path.string().c_str(), reinterpret_cast<sqlite3 **>(&db_),
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository",
                 "Failed to open database: {} (error: {})", db_path_,
                 sqlite3_errmsg(static_cast<sqlite3 *>(db_)));
    if (db_) {
      sqlite3_close(static_cast<sqlite3 *>(db_));
      db_ = nullptr;
    }
    return false;
  }

  // Check current schema version
  int current_version = get_schema_version();

  if (current_version == 0) {
    // Fresh install - create v2 schema
    if (!create_schema_v2()) {
      return false;
    }
  } else if (current_version == 1) {
    // Migrate v1 -> v2
    if (!migrate_v1_to_v2()) {
      return false;
    }
  } else if (current_version != SCHEMA_VERSION_V2) {
    PLUGIN_ERROR("message_repository",
                 "Unknown schema version: {}, expected {}", current_version,
                 SCHEMA_VERSION_V2);
    return false;
  }

  PLUGIN_INFO("message_repository",
              "MessageRepository initialized (v2 schema)");
  return true;
}

auto MessageRepository::create_schema_v2() -> bool {
  const std::string create_table_sql = R"(
    CREATE TABLE IF NOT EXISTS messages_v2 (
      platform TEXT NOT NULL,
      group_id TEXT NOT NULL,
      message_id TEXT NOT NULL,
      user_id TEXT NOT NULL,
      content TEXT NOT NULL,
      timestamp_ms INTEGER NOT NULL,
      is_bot INTEGER NOT NULL DEFAULT 0,
      is_command INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY (platform, group_id, message_id)
    );
  )";

  const std::string create_index_ctx_sql = R"(
    CREATE INDEX IF NOT EXISTS idx_context
    ON messages_v2(platform, group_id, timestamp_ms DESC);
  )";

  const std::string set_version_sql = R"(
    PRAGMA user_version = 2;
  )";

  char *err_msg = nullptr;
  int rc = sqlite3_exec(static_cast<sqlite3 *>(db_), create_table_sql.c_str(),
                        nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Failed to create table: {}", err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  rc = sqlite3_exec(static_cast<sqlite3 *>(db_), create_index_ctx_sql.c_str(),
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_WARN("message_repository", "Failed to create index: {}", err_msg);
    sqlite3_free(err_msg);
  }

  rc = sqlite3_exec(static_cast<sqlite3 *>(db_), set_version_sql.c_str(),
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Failed to set schema version: {}",
                 err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  return true;
}

auto MessageRepository::migrate_v1_to_v2() -> bool {
  PLUGIN_INFO("message_repository", "Migrating schema v1 -> v2...");

  // v1 had: platform, message_id (PK), group_id, user_id, content, timestamp,
  // is_bot

  const std::string create_new_table_sql = R"(
    CREATE TABLE IF NOT EXISTS messages_v2 (
      platform TEXT NOT NULL,
      group_id TEXT NOT NULL,
      message_id TEXT NOT NULL,
      user_id TEXT NOT NULL,
      content TEXT NOT NULL,
      timestamp_ms INTEGER NOT NULL,
      is_bot INTEGER NOT NULL DEFAULT 0,
      is_command INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY (platform, group_id, message_id)
    );
  )";

  const std::string migrate_data_sql = R"(
    INSERT OR IGNORE INTO messages_v2 (platform, group_id, message_id, user_id, content, timestamp_ms, is_bot, is_command)
    SELECT platform, group_id, message_id, user_id, content, timestamp, is_bot, 0
    FROM messages
    WHERE message_type = 'text';
  )";

  const std::string backup_old_table_sql = R"(
    ALTER TABLE messages RENAME TO messages_v1_backup;
  )";

  const std::string set_version_sql = R"(
    PRAGMA user_version = 2;
  )";

  char *err_msg = nullptr;

  // Create new table
  int rc =
      sqlite3_exec(static_cast<sqlite3 *>(db_), create_new_table_sql.c_str(),
                   nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Migration failed (create table): {}",
                 err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  // Migrate data
  rc = sqlite3_exec(static_cast<sqlite3 *>(db_), migrate_data_sql.c_str(),
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Migration failed (migrate data): {}",
                 err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  // Backup old table
  rc = sqlite3_exec(static_cast<sqlite3 *>(db_), backup_old_table_sql.c_str(),
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_WARN("message_repository",
                "Migration warning (backup old table): {}", err_msg);
    sqlite3_free(err_msg);
  }

  // Set version
  rc = sqlite3_exec(static_cast<sqlite3 *>(db_), set_version_sql.c_str(),
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Migration failed (set version): {}",
                 err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  PLUGIN_INFO("message_repository", "Schema v1 -> v2 migration completed");
  return true;
}

auto MessageRepository::append_message(
    const MessageRecord &record, bool collect_enabled,
    const std::vector<std::string> &allowed_groups) -> bool {
  // Check whitelist
  if (!is_group_allowed(record.group_id, collect_enabled, allowed_groups)) {
    PLUGIN_DEBUG("message_repository",
                 "Group {} not in whitelist (collect_enabled={}, "
                 "allowed_groups_count={}), skipping save",
                 record.group_id, collect_enabled, allowed_groups.size());
    return true;
  }

  // Skip empty content
  if (record.content.empty()) {
    PLUGIN_DEBUG("message_repository",
                 "Skipping empty content message from group {} user {}",
                 record.group_id, record.user_id);
    return true;
  }

  PLUGIN_DEBUG("message_repository",
               "Inserting message: platform={}, group_id={}, user_id={}, "
               "is_bot={}, is_command={}, content_len={}, timestamp_ms={}",
               record.platform, record.group_id, record.user_id, record.is_bot,
               record.is_command, record.content.length(), record.timestamp_ms);

  const std::string sql = R"(
    INSERT OR IGNORE INTO messages_v2
      (platform, group_id, message_id, user_id, content, timestamp_ms, is_bot, is_command)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?);
  )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(static_cast<sqlite3 *>(db_), sql.c_str(), -1,
                              &stmt, nullptr);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Failed to prepare INSERT: {}",
                 sqlite3_errmsg(static_cast<sqlite3 *>(db_)));
    return false;
  }

  sqlite3_bind_text(stmt, 1, record.platform.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, record.group_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, record.message_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, record.user_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, record.content.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 6, record.timestamp_ms);
  sqlite3_bind_int(stmt, 7, record.is_bot ? 1 : 0);
  sqlite3_bind_int(stmt, 8, record.is_command ? 1 : 0);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    PLUGIN_ERROR("message_repository", "Failed to execute INSERT: {}",
                 sqlite3_errmsg(static_cast<sqlite3 *>(db_)));
    sqlite3_finalize(stmt);
    return false;
  }

  PLUGIN_DEBUG("message_repository",
               "Message inserted successfully (INSERT OR IGNORE) for "
               "group_id={} message_id={}",
               record.group_id, record.message_id);

  sqlite3_finalize(stmt);
  return true;
}

auto MessageRepository::fetch_context(const ContextQuery &query)
    -> std::vector<MessageRecord> {
  std::vector<MessageRecord> result;

  const std::string sql = R"(
    SELECT platform, group_id, message_id, user_id, content, timestamp_ms, is_bot
    FROM messages_v2
    WHERE platform = ? AND group_id = ? AND timestamp_ms < ?
    ORDER BY timestamp_ms DESC
    LIMIT ?;
  )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(static_cast<sqlite3 *>(db_), sql.c_str(), -1,
                              &stmt, nullptr);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Failed to prepare SELECT: {}",
                 sqlite3_errmsg(static_cast<sqlite3 *>(db_)));
    return result;
  }

  sqlite3_bind_text(stmt, 1, query.platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, query.group_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 3, query.before_timestamp_ms);
  sqlite3_bind_int64(stmt, 4, query.limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MessageRecord rec;
    rec.platform = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    rec.group_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    rec.message_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    rec.user_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const char *content_ptr =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    rec.content = content_ptr ? content_ptr : "";
    rec.timestamp_ms = sqlite3_column_int64(stmt, 5);
    rec.is_bot = sqlite3_column_int(stmt, 6) != 0;

    if (!rec.content.empty()) {
      result.push_back(rec);
    }
  }

  sqlite3_finalize(stmt);

  // Reverse to get oldest-first order (for LLM context)
  std::reverse(result.begin(), result.end());

  PLUGIN_DEBUG("message_repository", "Fetched {} context messages",
               result.size());
  return result;
}

auto MessageRepository::cleanup_ttl(int ttl_days) -> int {
  int64_t cutoff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count() -
                      (static_cast<int64_t>(ttl_days) * 24 * 60 * 60 * 1000);

  const std::string sql = R"(
    DELETE FROM messages_v2 WHERE timestamp_ms < ?;
  )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(static_cast<sqlite3 *>(db_), sql.c_str(), -1,
                              &stmt, nullptr);
  if (rc != SQLITE_OK) {
    PLUGIN_ERROR("message_repository", "Failed to prepare DELETE: {}",
                 sqlite3_errmsg(static_cast<sqlite3 *>(db_)));
    return 0;
  }

  sqlite3_bind_int64(stmt, 1, cutoff_ms);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    PLUGIN_ERROR("message_repository", "Failed to execute DELETE: {}",
                 sqlite3_errmsg(static_cast<sqlite3 *>(db_)));
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);

  int deleted = sqlite3_changes(static_cast<sqlite3 *>(db_));
  PLUGIN_INFO("message_repository", "Cleaned up {} old messages (TTL: {} days)",
              deleted, ttl_days);
  return deleted;
}

auto MessageRepository::get_schema_version() const -> int {
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(static_cast<sqlite3 *>(db_),
                              "PRAGMA user_version;", -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return 0;
  }

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    int version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return version;
  }

  sqlite3_finalize(stmt);
  return 0;
}

auto MessageRepository::is_group_allowed(
    const std::string &group_id, bool collect_enabled,
    const std::vector<std::string> &allowed_groups) const -> bool {
  if (!collect_enabled) {
    PLUGIN_DEBUG(
        "message_repository",
        "is_group_allowed: collect_enabled=false for group {}, returning false",
        group_id);
    return false;
  }

  if (allowed_groups.empty()) {
    // If whitelist is empty, allow all groups
    PLUGIN_DEBUG("message_repository",
                 "is_group_allowed: allowed_groups empty, allowing group {}",
                 group_id);
    return true;
  }

  for (const auto &allowed : allowed_groups) {
    if (group_id == allowed) {
      PLUGIN_DEBUG("message_repository",
                   "is_group_allowed: group {} matched in allowed_groups, "
                   "returning true",
                   group_id);
      return true;
    }
  }

  PLUGIN_DEBUG("message_repository",
               "is_group_allowed: group {} not found in allowed_groups "
               "(size={}), returning false",
               group_id, allowed_groups.size());
  return false;
}

} // namespace plugins::chat_llm
