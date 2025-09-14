#include "DatabaseManager.hpp"
#include "common/Logger.hpp"
#include <fmt/format.h>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sstream>

namespace obcx::storage {

DatabaseManager::DatabaseManager(const std::string &db_path)
    : db_path_(db_path), db_(nullptr) {
  OBCX_DEBUG("DatabaseManager constructed with path: {}", db_path_);
}

DatabaseManager::~DatabaseManager() {
  if (db_) {
    sqlite3_close(db_);
    OBCX_DEBUG("Database closed");
  }
}

bool DatabaseManager::initialize() {
  std::lock_guard lock(db_mutex_);

  int rc = sqlite3_open(db_path_.c_str(), &db_);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Cannot open database: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_INFO("Database opened successfully: {}", db_path_);

  // 启用外键约束
  if (!execute_sql("PRAGMA foreign_keys = ON;")) {
    return false;
  }

  return create_tables();
}

bool DatabaseManager::create_tables() {
  // 创建消息表
  const std::string create_messages_table = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            platform TEXT NOT NULL,
            message_id TEXT NOT NULL,
            group_id TEXT NOT NULL,
            user_id TEXT NOT NULL,
            content TEXT NOT NULL,
            raw_message TEXT,
            message_type TEXT DEFAULT 'text',
            timestamp INTEGER NOT NULL,
            reply_to_message_id TEXT,
            forwarded_to_platform TEXT,
            forwarded_message_id TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(platform, message_id, group_id)
        );
    )";

  if (!execute_sql(create_messages_table)) {
    return false;
  }

  // 创建用户表
  const std::string create_users_table = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            platform TEXT NOT NULL,
            user_id TEXT NOT NULL,
            group_id TEXT NOT NULL DEFAULT '',
            username TEXT,
            nickname TEXT,
            title TEXT,
            first_name TEXT,
            last_name TEXT,
            last_updated DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(platform, user_id, group_id)
        );
    )";

  if (!execute_sql(create_users_table)) {
    return false;
  }

  // 创建消息ID映射表
  const std::string create_mappings_table = R"(
        CREATE TABLE IF NOT EXISTS message_mappings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_platform TEXT NOT NULL,
            source_message_id TEXT NOT NULL,
            target_platform TEXT NOT NULL,
            target_message_id TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(source_platform, source_message_id, target_platform)
        );
    )";

  if (!execute_sql(create_mappings_table)) {
    return false;
  }

  // 创建表情包缓存表
  const std::string create_sticker_cache_table = R"(
        CREATE TABLE IF NOT EXISTS sticker_cache (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            platform TEXT NOT NULL,
            sticker_id TEXT NOT NULL,
            sticker_hash TEXT NOT NULL,
            original_name TEXT,
            file_type TEXT NOT NULL,
            mime_type TEXT,
            original_file_path TEXT NOT NULL,
            converted_file_path TEXT,
            container_path TEXT NOT NULL,
            file_size INTEGER,
            conversion_status TEXT DEFAULT 'none',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            last_used_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(platform, sticker_hash)
        );
    )";

  if (!execute_sql(create_sticker_cache_table)) {
    return false;
  }

  // QQ表情包映射表
  const std::string create_qq_sticker_mapping_table = R"(
    CREATE TABLE IF NOT EXISTS qq_sticker_mapping (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      qq_sticker_hash TEXT NOT NULL UNIQUE,
      telegram_file_id TEXT NOT NULL,
      file_type TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      last_used_at INTEGER NOT NULL,
      is_gif INTEGER DEFAULT NULL,
      content_type TEXT DEFAULT NULL,
      last_checked_at INTEGER DEFAULT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_qq_sticker_hash ON qq_sticker_mapping(qq_sticker_hash);
  )";

  if (!execute_sql(create_qq_sticker_mapping_table)) {
    return false;
  }

  // 创建消息重试队列表
  const std::string create_message_retry_table = R"(
        CREATE TABLE IF NOT EXISTS message_retry_queue (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source_platform TEXT NOT NULL,
            target_platform TEXT NOT NULL,
            source_message_id TEXT NOT NULL,
            message_content TEXT NOT NULL,
            group_id TEXT NOT NULL,
            source_group_id TEXT,
            target_topic_id INTEGER DEFAULT -1,
            retry_count INTEGER NOT NULL DEFAULT 0,
            max_retry_count INTEGER NOT NULL DEFAULT 5,
            failure_reason TEXT,
            retry_type TEXT NOT NULL DEFAULT 'message_send',
            next_retry_at INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            last_attempt_at INTEGER NOT NULL,
            UNIQUE(source_platform, source_message_id, target_platform)
        );
    )";

  if (!execute_sql(create_message_retry_table)) {
    return false;
  }

  // 创建媒体下载重试队列表
  const std::string create_media_retry_table = R"(
        CREATE TABLE IF NOT EXISTS media_download_retry_queue (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            platform TEXT NOT NULL,
            file_id TEXT NOT NULL,
            file_type TEXT NOT NULL,
            download_url TEXT NOT NULL,
            local_path TEXT NOT NULL,
            retry_count INTEGER NOT NULL DEFAULT 0,
            max_retry_count INTEGER NOT NULL DEFAULT 3,
            failure_reason TEXT,
            use_proxy INTEGER NOT NULL DEFAULT 1,
            next_retry_at INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            last_attempt_at INTEGER NOT NULL,
            UNIQUE(platform, file_id)
        );
    )";

  if (!execute_sql(create_media_retry_table)) {
    return false;
  }

  // 创建索引以提高查询性能
  const std::string create_retry_indexes = R"(
        CREATE INDEX IF NOT EXISTS idx_message_retry_next_retry ON message_retry_queue(next_retry_at);
        CREATE INDEX IF NOT EXISTS idx_media_retry_next_retry ON media_download_retry_queue(next_retry_at);
    )";

  if (!execute_sql(create_retry_indexes)) {
    return false;
  }

  // 创建平台心跳表
  const std::string create_heartbeat_table = R"(
        CREATE TABLE IF NOT EXISTS platform_heartbeats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            platform TEXT NOT NULL UNIQUE,
            last_heartbeat_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now'))
        );
    )";
  if (!execute_sql(create_heartbeat_table)) {
    return false;
  }

  OBCX_INFO("Database tables created successfully");
  return true;
}

bool DatabaseManager::execute_sql(const std::string &sql) {
  char *error_msg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("SQL error: {}", error_msg);
    sqlite3_free(error_msg);
    return false;
  }

  return true;
}

bool DatabaseManager::save_message(const MessageInfo &message_info) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        INSERT OR REPLACE INTO messages
        (platform, message_id, group_id, user_id, content, raw_message, message_type,
         timestamp, reply_to_message_id, forwarded_to_platform, forwarded_message_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  // 绑定参数
  sqlite3_bind_text(stmt, 1, message_info.platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, message_info.message_id.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, message_info.group_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, message_info.user_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, message_info.content.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, message_info.raw_message.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, message_info.message_type.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 8, time_point_to_timestamp(message_info.timestamp));

  if (message_info.reply_to_message_id.has_value()) {
    sqlite3_bind_text(stmt, 9, message_info.reply_to_message_id->c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 9);
  }

  if (message_info.forwarded_to_platform.has_value()) {
    sqlite3_bind_text(stmt, 10, message_info.forwarded_to_platform->c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 10);
  }

  if (message_info.forwarded_message_id.has_value()) {
    sqlite3_bind_text(stmt, 11, message_info.forwarded_message_id->c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 11);
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to insert message: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_DEBUG("Message saved: {}:{}", message_info.platform,
             message_info.message_id);
  return true;
}

std::optional<MessageInfo> DatabaseManager::get_message(
    const std::string &platform, const std::string &message_id) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        SELECT platform, message_id, group_id, user_id, content, raw_message, message_type,
               timestamp, reply_to_message_id, forwarded_to_platform, forwarded_message_id, created_at
        FROM messages WHERE platform = ? AND message_id = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, message_id.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    MessageInfo msg_info;
    msg_info.platform =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    msg_info.message_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    msg_info.group_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    msg_info.user_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    msg_info.content =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    msg_info.raw_message =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    msg_info.message_type =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    msg_info.timestamp = timestamp_to_time_point(sqlite3_column_int64(stmt, 7));

    const char *reply_to =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8));
    if (reply_to) {
      msg_info.reply_to_message_id = reply_to;
    }

    const char *forwarded_to =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));
    if (forwarded_to) {
      msg_info.forwarded_to_platform = forwarded_to;
    }

    const char *forwarded_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));
    if (forwarded_id) {
      msg_info.forwarded_message_id = forwarded_id;
    }

    // created_at处理（如果需要的话）

    sqlite3_finalize(stmt);
    return msg_info;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

bool DatabaseManager::update_message_forwarding(
    const std::string &platform, const std::string &message_id,
    const std::string &forwarded_to_platform,
    const std::string &forwarded_message_id) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        UPDATE messages
        SET forwarded_to_platform = ?, forwarded_message_id = ?
        WHERE platform = ? AND message_id = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, forwarded_to_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, forwarded_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, message_id.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update message forwarding: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_DEBUG("Message forwarding updated: {}:{} -> {}:{}", platform, message_id,
             forwarded_to_platform, forwarded_message_id);
  return true;
}

bool DatabaseManager::save_or_update_user(const UserInfo &user_info) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        INSERT OR REPLACE INTO users
        (platform, user_id, group_id, username, nickname, title, first_name, last_name, last_updated)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, user_info.platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, user_info.user_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, user_info.group_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, user_info.username.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, user_info.nickname.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, user_info.title.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, user_info.first_name.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 8, user_info.last_name.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to save user: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_DEBUG("User saved: {}:{}:{}", user_info.platform, user_info.user_id,
             user_info.group_id);
  return true;
}

std::optional<UserInfo> DatabaseManager::get_user(const std::string &platform,
                                                  const std::string &user_id,
                                                  const std::string &group_id) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        SELECT platform, user_id, group_id, username, nickname, title, first_name, last_name, last_updated
        FROM users WHERE platform = ? AND user_id = ? AND group_id = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, user_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, group_id.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    UserInfo user_info;
    user_info.platform =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    user_info.user_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    user_info.group_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

    const char *username =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    if (username)
      user_info.username = username;

    const char *nickname =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    if (nickname)
      user_info.nickname = nickname;

    const char *title =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    if (title)
      user_info.title = title;

    const char *first_name =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    if (first_name)
      user_info.first_name = first_name;

    const char *last_name =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    if (last_name)
      user_info.last_name = last_name;

    sqlite3_finalize(stmt);
    return user_info;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

std::string DatabaseManager::get_user_display_name(
    const std::string &platform, const std::string &user_id,
    const std::string &group_id) {
  auto user_info = get_user(platform, user_id, group_id);
  if (!user_info.has_value()) {
    return user_id; // 如果没找到用户信息，返回用户ID
  }

  const auto &user = user_info.value();

  // Telegram用户优先显示真实姓名而不是username
  if (platform == "telegram") {
    // 对于Telegram：优先级为 昵称 > 名字+姓氏 > 用户名 > 用户ID
    if (!user.nickname.empty()) {
      return user.nickname;
    }

    if (!user.first_name.empty()) {
      std::string display_name = user.first_name;
      if (!user.last_name.empty()) {
        display_name += " " + user.last_name;
      }
      return display_name;
    }

    if (!user.username.empty()) {
      return user.username;
    }
  } else if (platform == "qq") {
    // QQ平台：nickname字段已经存储了最优先的显示名称（群名片 > 群头衔 >
    // 一般昵称） 如果nickname为空，则回退到其他字段
    if (!user.nickname.empty()) {
      return user.nickname;
    }

    if (!user.username.empty()) {
      return user.username;
    }

    if (!user.first_name.empty()) {
      std::string display_name = user.first_name;
      if (!user.last_name.empty()) {
        display_name += " " + user.last_name;
      }
      return display_name;
    }
  } else {
    // 其他平台：优先级为 昵称 > 用户名 > 名字+姓氏 > 用户ID
    if (!user.nickname.empty()) {
      return user.nickname;
    }

    if (!user.username.empty()) {
      return user.username;
    }

    if (!user.first_name.empty()) {
      std::string display_name = user.first_name;
      if (!user.last_name.empty()) {
        display_name += " " + user.last_name;
      }
      return display_name;
    }
  }

  return user_id;
}

bool DatabaseManager::should_fetch_user_info(const std::string &platform,
                                             const std::string &user_id,
                                             const std::string &group_id) {
  auto user_info = get_user(platform, user_id, group_id);

  // 如果用户不存在，需要获取
  if (!user_info.has_value()) {
    return true;
  }

  const auto &user = user_info.value();

  // 对于QQ用户，如果没有昵称信息，需要获取
  if (platform == "qq") {
    return user.nickname.empty();
  }

  // 对于Telegram用户，如果没有用户名和名字，需要获取
  if (platform == "telegram") {
    return user.username.empty() && user.first_name.empty();
  }

  return false;
}

bool DatabaseManager::add_message_mapping(const MessageMapping &mapping) {
  std::lock_guard lock(db_mutex_);

  // 验证消息ID不为空
  if (mapping.source_message_id.empty() || mapping.target_message_id.empty()) {
    OBCX_ERROR(
        "Invalid message mapping - empty message IDs: source={}, target={}",
        mapping.source_message_id, mapping.target_message_id);
    return false;
  }

  OBCX_DEBUG("Adding message mapping: {}:{} -> {}:{}", mapping.source_platform,
             mapping.source_message_id, mapping.target_platform,
             mapping.target_message_id);

  const std::string sql = R"(
        INSERT OR REPLACE INTO message_mappings
        (source_platform, source_message_id, target_platform, target_message_id)
        VALUES (?, ?, ?, ?);
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, mapping.source_platform.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, mapping.source_message_id.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, mapping.target_platform.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, mapping.target_message_id.c_str(), -1,
                    SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to add message mapping: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_DEBUG("Message mapping added: {}:{} -> {}:{}", mapping.source_platform,
             mapping.source_message_id, mapping.target_platform,
             mapping.target_message_id);
  return true;
}

std::optional<std::string> DatabaseManager::get_target_message_id(
    const std::string &source_platform, const std::string &source_message_id,
    const std::string &target_platform) {
  std::lock_guard lock(db_mutex_);

  // 验证参数不为空
  if (source_message_id.empty()) {
    OBCX_DEBUG("Empty source message ID for query: {}:{} -> {}",
               source_platform, source_message_id, target_platform);
    return std::nullopt;
  }

  OBCX_DEBUG("Querying target message ID: {}:{} -> {}", source_platform,
             source_message_id, target_platform);

  const std::string sql = R"(
        SELECT target_message_id FROM message_mappings
        WHERE source_platform = ? AND source_message_id = ? AND target_platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, source_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, source_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, target_platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    std::string result =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    OBCX_DEBUG("Found target message ID: {}", result);
    return result;
  }

  sqlite3_finalize(stmt);
  OBCX_DEBUG("No target message ID found");
  return std::nullopt;
}

std::optional<std::string> DatabaseManager::get_source_message_id(
    const std::string &target_platform, const std::string &target_message_id,
    const std::string &source_platform) {
  std::lock_guard lock(db_mutex_);

  // 验证参数不为空
  if (target_message_id.empty()) {
    OBCX_DEBUG("Empty target message ID for query: {}:{} <- {}",
               target_platform, target_message_id, source_platform);
    return std::nullopt;
  }

  OBCX_DEBUG("Querying source message ID: {}:{} <- {}", target_platform,
             target_message_id, source_platform);

  const std::string sql = R"(
        SELECT source_message_id FROM message_mappings
        WHERE target_platform = ? AND target_message_id = ? AND source_platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, target_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, target_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, source_platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    std::string result =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    OBCX_DEBUG("Found source message ID: {}", result);
    return result;
  }

  sqlite3_finalize(stmt);
  OBCX_DEBUG("No source message ID found");
  return std::nullopt;
}

bool DatabaseManager::delete_message_mapping(
    const std::string &source_platform, const std::string &source_message_id,
    const std::string &target_platform) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        DELETE FROM message_mappings
        WHERE source_platform = ? AND source_message_id = ? AND target_platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare delete statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, source_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, source_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, target_platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE) {
    OBCX_DEBUG("消息映射删除成功: {}:{} -> {}", source_platform,
               source_message_id, target_platform);
    return true;
  } else {
    OBCX_ERROR("Failed to delete message mapping: {}", sqlite3_errmsg(db_));
    return false;
  }
}

bool DatabaseManager::update_message_mapping(
    const std::string &source_platform, const std::string &source_message_id,
    const std::string &target_platform,
    const std::string &new_target_message_id) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        UPDATE message_mappings
        SET target_message_id = ?, created_at = datetime('now')
        WHERE source_platform = ? AND source_message_id = ? AND target_platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update statement: {}", sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, new_target_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, source_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, source_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, target_platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE) {
    OBCX_DEBUG("消息映射更新成功: {}:{} -> {}:{}", source_platform,
               source_message_id, target_platform, new_target_message_id);
    return true;
  } else {
    OBCX_ERROR("Failed to update message mapping: {}", sqlite3_errmsg(db_));
    return false;
  }
}

bool DatabaseManager::save_message_from_event(const common::MessageEvent &event,
                                              const std::string &platform) {
  MessageInfo msg_info;
  msg_info.platform = platform;
  msg_info.message_id = event.message_id;
  msg_info.group_id = event.group_id.value_or("");
  msg_info.user_id = event.user_id;
  msg_info.content = event.raw_message;
  msg_info.timestamp = event.time;

  // 将消息序列化为JSON存储
  nlohmann::json raw_json;
  raw_json["type"] = event.type;
  raw_json["post_type"] = event.post_type;
  raw_json["message_type"] = event.message_type;
  raw_json["raw_message"] = event.raw_message;
  raw_json["message"] = nlohmann::json::array();

  for (const auto &segment : event.message) {
    nlohmann::json segment_json;
    segment_json["type"] = segment.type;
    segment_json["data"] = segment.data;
    raw_json["message"].push_back(segment_json);
  }

  msg_info.raw_message = raw_json.dump();

  // 确定消息类型
  if (!event.message.empty()) {
    msg_info.message_type = event.message[0].type;
  }

  // 检查是否有回复消息
  for (const auto &segment : event.message) {
    if (segment.type == "reply" && segment.data.contains("id")) {
      msg_info.reply_to_message_id = segment.data["id"];
      break;
    }
  }

  return save_message(msg_info);
}

bool DatabaseManager::save_user_from_event(const common::MessageEvent &event,
                                           const std::string &platform) {
  UserInfo user_info;
  user_info.platform = platform;
  user_info.user_id = event.user_id;
  // 只有QQ用户使用群组特定的昵称，Telegram用户始终使用空的group_id
  user_info.group_id = (platform == "qq") ? event.group_id.value_or("") : "";
  user_info.last_updated = std::chrono::system_clock::now();

  // 尝试从event.data中提取更多用户信息
  if (event.data.is_object()) {
    if (platform == "telegram") {
      // 对于Telegram，从from字段提取用户信息
      if (event.data.contains("from") && event.data["from"].is_object()) {
        auto from = event.data["from"];
        if (from.contains("username") && from["username"].is_string()) {
          user_info.username = from["username"];
        }
        if (from.contains("first_name") && from["first_name"].is_string()) {
          user_info.first_name = from["first_name"];
        }
        if (from.contains("last_name") && from["last_name"].is_string()) {
          user_info.last_name = from["last_name"];
        }
      }
    } else if (platform == "qq") {
      // 对于QQ，从sender字段提取用户信息
      if (event.data.contains("sender") && event.data["sender"].is_object()) {
        auto sender = event.data["sender"];
        if (sender.contains("nickname") && sender["nickname"].is_string()) {
          user_info.nickname = sender["nickname"];
        }
        if (sender.contains("card") && sender["card"].is_string()) {
          std::string card = sender["card"];
          if (!card.empty()) {
            user_info.nickname = card; // QQ群名片作为昵称
          }
        }
      }
    }
  }

  return save_or_update_user(user_info);
}

// 时间戳转换辅助函数
int64_t DatabaseManager::time_point_to_timestamp(
    const std::chrono::system_clock::time_point &tp) {
  return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch())
      .count();
}

std::chrono::system_clock::time_point DatabaseManager::timestamp_to_time_point(
    int64_t timestamp) {
  return std::chrono::system_clock::time_point(std::chrono::seconds(timestamp));
}

// === 表情包缓存相关操作实现 ===

bool DatabaseManager::save_sticker_cache(const StickerCacheInfo &cache_info) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        INSERT OR REPLACE INTO sticker_cache (
            platform, sticker_id, sticker_hash, original_name, file_type, mime_type,
            original_file_path, converted_file_path, container_path, file_size,
            conversion_status, created_at, last_used_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare save sticker cache statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  int idx = 1;
  sqlite3_bind_text(stmt, idx++, cache_info.platform.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, idx++, cache_info.sticker_id.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, idx++, cache_info.sticker_hash.c_str(), -1,
                    SQLITE_STATIC);

  if (cache_info.original_name.has_value()) {
    sqlite3_bind_text(stmt, idx++, cache_info.original_name->c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, idx++);
  }

  sqlite3_bind_text(stmt, idx++, cache_info.file_type.c_str(), -1,
                    SQLITE_STATIC);

  if (cache_info.mime_type.has_value()) {
    sqlite3_bind_text(stmt, idx++, cache_info.mime_type->c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, idx++);
  }

  sqlite3_bind_text(stmt, idx++, cache_info.original_file_path.c_str(), -1,
                    SQLITE_STATIC);

  if (cache_info.converted_file_path.has_value()) {
    sqlite3_bind_text(stmt, idx++, cache_info.converted_file_path->c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, idx++);
  }

  sqlite3_bind_text(stmt, idx++, cache_info.container_path.c_str(), -1,
                    SQLITE_STATIC);

  if (cache_info.file_size.has_value()) {
    sqlite3_bind_int64(stmt, idx++, cache_info.file_size.value());
  } else {
    sqlite3_bind_null(stmt, idx++);
  }

  sqlite3_bind_text(stmt, idx++, cache_info.conversion_status.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_int64(stmt, idx++,
                     time_point_to_timestamp(cache_info.created_at));
  sqlite3_bind_int64(stmt, idx++,
                     time_point_to_timestamp(cache_info.last_used_at));

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to save sticker cache: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_DEBUG("Sticker cache saved successfully: {} - {}", cache_info.platform,
             cache_info.sticker_hash);
  return true;
}

std::optional<StickerCacheInfo> DatabaseManager::get_sticker_cache(
    const std::string &platform, const std::string &sticker_hash) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        SELECT platform, sticker_id, sticker_hash, original_name, file_type, mime_type,
               original_file_path, converted_file_path, container_path, file_size,
               conversion_status, created_at, last_used_at
        FROM sticker_cache
        WHERE platform = ? AND sticker_hash = ?
    )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare get sticker cache statement: {}",
               sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, sticker_hash.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  StickerCacheInfo cache_info;
  int idx = 0;

  cache_info.platform =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));
  cache_info.sticker_id =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));
  cache_info.sticker_hash =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));

  const char *original_name =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));
  if (original_name) {
    cache_info.original_name = original_name;
  }

  cache_info.file_type =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));

  const char *mime_type =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));
  if (mime_type) {
    cache_info.mime_type = mime_type;
  }

  cache_info.original_file_path =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));

  const char *converted_file_path =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));
  if (converted_file_path) {
    cache_info.converted_file_path = converted_file_path;
  }

  cache_info.container_path =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));

  if (sqlite3_column_type(stmt, idx) != SQLITE_NULL) {
    cache_info.file_size = sqlite3_column_int64(stmt, idx);
  }
  idx++;

  cache_info.conversion_status =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx++));
  cache_info.created_at =
      timestamp_to_time_point(sqlite3_column_int64(stmt, idx++));
  cache_info.last_used_at =
      timestamp_to_time_point(sqlite3_column_int64(stmt, idx++));

  sqlite3_finalize(stmt);

  OBCX_DEBUG("Sticker cache found: {} - {}", platform, sticker_hash);
  return cache_info;
}

bool DatabaseManager::update_sticker_last_used(
    const std::string &platform, const std::string &sticker_hash) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        UPDATE sticker_cache
        SET last_used_at = ?
        WHERE platform = ? AND sticker_hash = ?
    )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update sticker last used statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  auto now = std::chrono::system_clock::now();
  sqlite3_bind_int64(stmt, 1, time_point_to_timestamp(now));
  sqlite3_bind_text(stmt, 2, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, sticker_hash.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update sticker last used: {}", sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

auto DatabaseManager::update_sticker_conversion(
    const std::string &platform, const std::string &sticker_hash,
    const std::string &conversion_status,
    const std::optional<std::string> &converted_file_path) -> bool {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        UPDATE sticker_cache
        SET conversion_status = ?, converted_file_path = ?
        WHERE platform = ? AND sticker_hash = ?
    )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update sticker conversion statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, conversion_status.c_str(), -1, SQLITE_STATIC);

  if (converted_file_path.has_value()) {
    sqlite3_bind_text(stmt, 2, converted_file_path->c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 2);
  }

  sqlite3_bind_text(stmt, 3, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, sticker_hash.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update sticker conversion: {}", sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

// === QQ表情包映射相关操作 ===

bool DatabaseManager::save_qq_sticker_mapping(const QQStickerMapping &mapping) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
    INSERT OR REPLACE INTO qq_sticker_mapping 
    (qq_sticker_hash, telegram_file_id, file_type, created_at, last_used_at, is_gif, content_type, last_checked_at) 
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare save qq sticker mapping statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, mapping.qq_sticker_hash.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, mapping.telegram_file_id.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, mapping.file_type.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 4, time_point_to_timestamp(mapping.created_at));
  sqlite3_bind_int64(stmt, 5, time_point_to_timestamp(mapping.last_used_at));

  // 处理可选字段
  if (mapping.is_gif.has_value()) {
    sqlite3_bind_int(stmt, 6, mapping.is_gif.value() ? 1 : 0);
  } else {
    sqlite3_bind_null(stmt, 6);
  }

  if (mapping.content_type.has_value()) {
    sqlite3_bind_text(stmt, 7, mapping.content_type.value().c_str(), -1,
                      SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 7);
  }

  if (mapping.last_checked_at.has_value()) {
    sqlite3_bind_int64(
        stmt, 8, time_point_to_timestamp(mapping.last_checked_at.value()));
  } else {
    sqlite3_bind_null(stmt, 8);
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to save qq sticker mapping: {}", sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

auto DatabaseManager::get_qq_sticker_mapping(const std::string &qq_sticker_hash)
    -> std::optional<QQStickerMapping> {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
    SELECT qq_sticker_hash, telegram_file_id, file_type, created_at, last_used_at, is_gif, content_type, last_checked_at 
    FROM qq_sticker_mapping 
    WHERE qq_sticker_hash = ?
  )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare get qq sticker mapping statement: {}",
               sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, qq_sticker_hash.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    QQStickerMapping mapping;
    mapping.qq_sticker_hash =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    mapping.telegram_file_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    mapping.file_type =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    mapping.created_at = timestamp_to_time_point(sqlite3_column_int64(stmt, 3));
    mapping.last_used_at =
        timestamp_to_time_point(sqlite3_column_int64(stmt, 4));

    // 处理可选字段
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
      mapping.is_gif = sqlite3_column_int(stmt, 5) == 1;
    }

    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
      mapping.content_type =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    }

    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
      mapping.last_checked_at =
          timestamp_to_time_point(sqlite3_column_int64(stmt, 7));
    }

    sqlite3_finalize(stmt);
    return mapping;
  } else if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to get qq sticker mapping: {}", sqlite3_errmsg(db_));
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

bool DatabaseManager::update_qq_sticker_last_used(
    const std::string &qq_sticker_hash) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
    UPDATE qq_sticker_mapping 
    SET last_used_at = ? 
    WHERE qq_sticker_hash = ?
  )";

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update qq sticker last used statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  auto now = std::chrono::system_clock::now();
  sqlite3_bind_int64(stmt, 1, time_point_to_timestamp(now));
  sqlite3_bind_text(stmt, 2, qq_sticker_hash.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update qq sticker last used: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

std::string DatabaseManager::calculate_hash(const std::string &input) {
  EVP_MD_CTX *context = EVP_MD_CTX_new();
  if (!context) {
    OBCX_ERROR("无法创建EVP_MD_CTX");
    return "";
  }

  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
    OBCX_ERROR("EVP_DigestInit_ex失败");
    EVP_MD_CTX_free(context);
    return "";
  }

  if (EVP_DigestUpdate(context, input.c_str(), input.length()) != 1) {
    OBCX_ERROR("EVP_DigestUpdate失败");
    EVP_MD_CTX_free(context);
    return "";
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  if (EVP_DigestFinal_ex(context, hash, &hash_len) != 1) {
    OBCX_ERROR("EVP_DigestFinal_ex失败");
    EVP_MD_CTX_free(context);
    return "";
  }

  EVP_MD_CTX_free(context);

  std::ostringstream ss;
  for (unsigned int i = 0; i < hash_len; i++) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash[i]);
  }
  return ss.str();
}

int DatabaseManager::cleanup_old_image_type_cache(int max_age_days) {
  std::lock_guard lock(db_mutex_);

  if (!db_) {
    OBCX_ERROR("数据库连接未初始化");
    return -1;
  }

  try {
    // 计算截止时间戳（max_age_days 天前）
    auto cutoff_time = std::chrono::system_clock::now() -
                       std::chrono::hours(24 * max_age_days);
    int64_t cutoff_timestamp = time_point_to_timestamp(cutoff_time);

    // 删除过期的缓存记录（以last_used_at为准）
    const char *sql = R"(
      DELETE FROM qq_sticker_mapping 
      WHERE last_used_at IS NOT NULL 
      AND last_used_at < ?
    )";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      OBCX_ERROR("清理缓存SQL准备失败: {}", sqlite3_errmsg(db_));
      return -1;
    }

    sqlite3_bind_int64(stmt, 1, cutoff_timestamp);

    int result = sqlite3_step(stmt);
    int deleted_count = -1;

    if (result == SQLITE_DONE) {
      deleted_count = sqlite3_changes(db_);
      OBCX_INFO("清理了{}条超过{}天未使用的图片类型缓存记录", deleted_count,
                max_age_days);
    } else {
      OBCX_ERROR("清理缓存执行失败: {}", sqlite3_errmsg(db_));
    }

    sqlite3_finalize(stmt);
    return deleted_count;

  } catch (const std::exception &e) {
    OBCX_ERROR("清理图片类型缓存异常: {}", e.what());
    return -1;
  }
}

std::string DatabaseManager::get_cache_statistics() {
  std::lock_guard lock(db_mutex_);

  if (!db_) {
    return "数据库连接未初始化";
  }

  std::ostringstream stats;

  try {
    // 统计总记录数
    const char *count_sql = "SELECT COUNT(*) FROM qq_sticker_mapping";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db_, count_sql, -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        int total_count = sqlite3_column_int(stmt, 0);
        stats << "总缓存记录数: " << total_count << "\n";
      }
      sqlite3_finalize(stmt);
    }

    // 统计有图片类型信息的记录数
    const char *typed_sql = R"(
      SELECT COUNT(*) FROM qq_sticker_mapping 
      WHERE is_gif IS NOT NULL
    )";

    if (sqlite3_prepare_v2(db_, typed_sql, -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        int typed_count = sqlite3_column_int(stmt, 0);
        stats << "已检测类型的记录数: " << typed_count << "\n";
      }
      sqlite3_finalize(stmt);
    }

    // 统计GIF和非GIF的分布
    const char *gif_sql = R"(
      SELECT 
        SUM(CASE WHEN is_gif = 1 THEN 1 ELSE 0 END) as gif_count,
        SUM(CASE WHEN is_gif = 0 THEN 1 ELSE 0 END) as static_count
      FROM qq_sticker_mapping 
      WHERE is_gif IS NOT NULL
    )";

    if (sqlite3_prepare_v2(db_, gif_sql, -1, &stmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        int gif_count = sqlite3_column_int(stmt, 0);
        int static_count = sqlite3_column_int(stmt, 1);
        stats << "GIF图片数: " << gif_count << "\n";
        stats << "静态图片数: " << static_count << "\n";
      }
      sqlite3_finalize(stmt);
    }

    // 统计最近检测时间
    const char *recent_sql = R"(
      SELECT COUNT(*) FROM qq_sticker_mapping 
      WHERE last_checked_at IS NOT NULL 
      AND last_checked_at > ?
    )";

    auto recent_time =
        std::chrono::system_clock::now() - std::chrono::hours(24);
    int64_t recent_timestamp = time_point_to_timestamp(recent_time);

    if (sqlite3_prepare_v2(db_, recent_sql, -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, recent_timestamp);
      if (sqlite3_step(stmt) == SQLITE_ROW) {
        int recent_count = sqlite3_column_int(stmt, 0);
        stats << "24小时内检测的记录数: " << recent_count << "\n";
      }
      sqlite3_finalize(stmt);
    }

  } catch (const std::exception &e) {
    stats << "获取缓存统计异常: " << e.what() << "\n";
  }

  return stats.str();
}

// === 消息重试队列相关操作 ===

bool DatabaseManager::add_message_retry(const MessageRetryInfo &retry_info) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        INSERT OR REPLACE INTO message_retry_queue
        (source_platform, target_platform, source_message_id, message_content, 
         group_id, source_group_id, target_topic_id, retry_count, max_retry_count, 
         failure_reason, retry_type, next_retry_at, created_at, last_attempt_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare message retry statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, retry_info.source_platform.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, retry_info.target_platform.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, retry_info.source_message_id.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, retry_info.message_content.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, retry_info.group_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, retry_info.source_group_id.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 7, retry_info.target_topic_id);
  sqlite3_bind_int(stmt, 8, retry_info.retry_count);
  sqlite3_bind_int(stmt, 9, retry_info.max_retry_count);
  sqlite3_bind_text(stmt, 10, retry_info.failure_reason.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 11, retry_info.retry_type.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 12,
                     time_point_to_timestamp(retry_info.next_retry_at));
  sqlite3_bind_int64(stmt, 13, time_point_to_timestamp(retry_info.created_at));
  sqlite3_bind_int64(stmt, 14,
                     time_point_to_timestamp(retry_info.last_attempt_at));

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to insert message retry: {}", sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

std::vector<MessageRetryInfo> DatabaseManager::get_pending_message_retries(
    int limit) {
  std::lock_guard lock(db_mutex_);
  std::vector<MessageRetryInfo> retries;

  const std::string sql = R"(
        SELECT source_platform, target_platform, source_message_id, message_content,
               group_id, source_group_id, target_topic_id, retry_count, max_retry_count, 
               failure_reason, retry_type, next_retry_at, created_at, last_attempt_at
        FROM message_retry_queue
        WHERE next_retry_at <= ? AND retry_count < max_retry_count
        ORDER BY next_retry_at ASC
        LIMIT ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare get pending message retries statement: {}",
               sqlite3_errmsg(db_));
    return retries;
  }

  int64_t current_time =
      time_point_to_timestamp(std::chrono::system_clock::now());
  sqlite3_bind_int64(stmt, 1, current_time);
  sqlite3_bind_int(stmt, 2, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MessageRetryInfo info;
    info.source_platform =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    info.target_platform =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    info.source_message_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    info.message_content =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    info.group_id =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));

    const char *source_group_id_ptr =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    info.source_group_id = source_group_id_ptr ? source_group_id_ptr : "";

    info.target_topic_id = sqlite3_column_int64(stmt, 6);
    info.retry_count = sqlite3_column_int(stmt, 7);
    info.max_retry_count = sqlite3_column_int(stmt, 8);

    const char *failure_reason =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9));
    info.failure_reason = failure_reason ? failure_reason : "";

    info.retry_type =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));
    info.next_retry_at =
        timestamp_to_time_point(sqlite3_column_int64(stmt, 11));
    info.created_at = timestamp_to_time_point(sqlite3_column_int64(stmt, 12));
    info.last_attempt_at =
        timestamp_to_time_point(sqlite3_column_int64(stmt, 13));

    retries.push_back(info);
  }

  sqlite3_finalize(stmt);
  return retries;
}

bool DatabaseManager::update_message_retry(
    const std::string &source_platform, const std::string &source_message_id,
    const std::string &target_platform, int retry_count,
    const std::chrono::system_clock::time_point &next_retry_at,
    const std::string &failure_reason) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        UPDATE message_retry_queue
        SET retry_count = ?, next_retry_at = ?, failure_reason = ?, last_attempt_at = ?
        WHERE source_platform = ? AND source_message_id = ? AND target_platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update message retry statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_int(stmt, 1, retry_count);
  sqlite3_bind_int64(stmt, 2, time_point_to_timestamp(next_retry_at));
  sqlite3_bind_text(stmt, 3, failure_reason.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 4,
                     time_point_to_timestamp(std::chrono::system_clock::now()));
  sqlite3_bind_text(stmt, 5, source_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, source_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, target_platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update message retry: {}", sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

bool DatabaseManager::remove_message_retry(const std::string &source_platform,
                                           const std::string &source_message_id,
                                           const std::string &target_platform) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        DELETE FROM message_retry_queue
        WHERE source_platform = ? AND source_message_id = ? AND target_platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare remove message retry statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, source_platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, source_message_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, target_platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to remove message retry: {}", sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

// === 媒体下载重试队列相关操作 ===

bool DatabaseManager::add_media_download_retry(
    const MediaDownloadRetryInfo &retry_info) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        INSERT OR REPLACE INTO media_download_retry_queue
        (platform, file_id, file_type, download_url, local_path,
         retry_count, max_retry_count, failure_reason, use_proxy,
         next_retry_at, created_at, last_attempt_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare media download retry statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, retry_info.platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, retry_info.file_id.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 3, retry_info.file_type.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 4, retry_info.download_url.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, retry_info.local_path.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 6, retry_info.retry_count);
  sqlite3_bind_int(stmt, 7, retry_info.max_retry_count);
  sqlite3_bind_text(stmt, 8, retry_info.failure_reason.c_str(), -1,
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 9, retry_info.use_proxy ? 1 : 0);
  sqlite3_bind_int64(stmt, 10,
                     time_point_to_timestamp(retry_info.next_retry_at));
  sqlite3_bind_int64(stmt, 11, time_point_to_timestamp(retry_info.created_at));
  sqlite3_bind_int64(stmt, 12,
                     time_point_to_timestamp(retry_info.last_attempt_at));

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to insert media download retry: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

std::vector<MediaDownloadRetryInfo>
DatabaseManager::get_pending_media_download_retries(int limit) {
  std::lock_guard lock(db_mutex_);
  std::vector<MediaDownloadRetryInfo> retries;

  const std::string sql = R"(
        SELECT platform, file_id, file_type, download_url, local_path,
               retry_count, max_retry_count, failure_reason, use_proxy,
               next_retry_at, created_at, last_attempt_at
        FROM media_download_retry_queue
        WHERE next_retry_at <= ? AND retry_count < max_retry_count
        ORDER BY next_retry_at ASC
        LIMIT ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR(
        "Failed to prepare get pending media download retries statement: {}",
        sqlite3_errmsg(db_));
    return retries;
  }

  int64_t current_time =
      time_point_to_timestamp(std::chrono::system_clock::now());
  sqlite3_bind_int64(stmt, 1, current_time);
  sqlite3_bind_int(stmt, 2, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    MediaDownloadRetryInfo info;
    info.platform =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    info.file_id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    info.file_type =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    info.download_url =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    info.local_path =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    info.retry_count = sqlite3_column_int(stmt, 5);
    info.max_retry_count = sqlite3_column_int(stmt, 6);

    const char *failure_reason =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));
    info.failure_reason = failure_reason ? failure_reason : "";

    info.use_proxy = sqlite3_column_int(stmt, 8) != 0;
    info.next_retry_at = timestamp_to_time_point(sqlite3_column_int64(stmt, 9));
    info.created_at = timestamp_to_time_point(sqlite3_column_int64(stmt, 10));
    info.last_attempt_at =
        timestamp_to_time_point(sqlite3_column_int64(stmt, 11));

    retries.push_back(info);
  }

  sqlite3_finalize(stmt);
  return retries;
}

bool DatabaseManager::update_media_download_retry(
    const std::string &platform, const std::string &file_id, int retry_count,
    const std::chrono::system_clock::time_point &next_retry_at,
    const std::string &failure_reason, bool use_proxy) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        UPDATE media_download_retry_queue
        SET retry_count = ?, next_retry_at = ?, failure_reason = ?, use_proxy = ?, last_attempt_at = ?
        WHERE platform = ? AND file_id = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update media download retry statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_int(stmt, 1, retry_count);
  sqlite3_bind_int64(stmt, 2, time_point_to_timestamp(next_retry_at));
  sqlite3_bind_text(stmt, 3, failure_reason.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, use_proxy ? 1 : 0);
  sqlite3_bind_int64(stmt, 5,
                     time_point_to_timestamp(std::chrono::system_clock::now()));
  sqlite3_bind_text(stmt, 6, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, file_id.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update media download retry: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

bool DatabaseManager::remove_media_download_retry(const std::string &platform,
                                                  const std::string &file_id) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        DELETE FROM media_download_retry_queue
        WHERE platform = ? AND file_id = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare remove media download retry statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, file_id.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to remove media download retry: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  return true;
}

// === 平台心跳管理相关操作 ===
bool DatabaseManager::update_platform_heartbeat(
    const std::string &platform,
    const std::chrono::system_clock::time_point &heartbeat_time) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        INSERT OR REPLACE INTO platform_heartbeats 
        (platform, last_heartbeat_at, updated_at)
        VALUES (?, ?, strftime('%s','now'));
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare update platform heartbeat statement: {}",
               sqlite3_errmsg(db_));
    return false;
  }

  auto heartbeat_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                 heartbeat_time.time_since_epoch())
                                 .count();

  sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, heartbeat_timestamp);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    OBCX_ERROR("Failed to update platform heartbeat: {}", sqlite3_errmsg(db_));
    return false;
  }

  OBCX_DEBUG("Platform heartbeat updated: {}", platform);
  return true;
}

std::optional<PlatformHeartbeatInfo> DatabaseManager::get_platform_heartbeat(
    const std::string &platform) {
  std::lock_guard lock(db_mutex_);

  const std::string sql = R"(
        SELECT platform, last_heartbeat_at, updated_at 
        FROM platform_heartbeats 
        WHERE platform = ?;
    )";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    OBCX_ERROR("Failed to prepare get platform heartbeat statement: {}",
               sqlite3_errmsg(db_));
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, platform.c_str(), -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    PlatformHeartbeatInfo info;
    info.platform =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));

    int64_t heartbeat_timestamp = sqlite3_column_int64(stmt, 1);
    info.last_heartbeat_at =
        std::chrono::system_clock::from_time_t(heartbeat_timestamp);

    int64_t updated_timestamp = sqlite3_column_int64(stmt, 2);
    info.updated_at = std::chrono::system_clock::from_time_t(updated_timestamp);

    sqlite3_finalize(stmt);
    return info;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

} // namespace obcx::storage
