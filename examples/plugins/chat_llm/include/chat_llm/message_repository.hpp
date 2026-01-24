#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace plugins::chat_llm {

/**
 * @brief Single message record for history storage
 */
struct MessageRecord {
  std::string platform;
  std::string group_id;
  std::string message_id;
  std::string user_id;
  std::string content;
  int64_t timestamp_ms;
  bool is_bot = false;
  bool is_command = false;
};

/**
 * @brief Query parameters for fetching context
 */
struct ContextQuery {
  std::string platform;
  std::string group_id;
  int64_t before_timestamp_ms;
  int limit;
};

/**
 * @brief SQLite database repository for message history
 *
 * Schema v2 fixes:
 * - Primary key includes group_id to fix Telegram cross-group conflicts
 * - Added is_command flag to filter command messages from context
 * - Supports TTL-based cleanup and group whitelist filtering
 */
class MessageRepository {
public:
  /**
   * @brief Open or create database at given path
   */
  explicit MessageRepository(const std::string &db_path);

  ~MessageRepository();

  /**
   * @brief Initialize schema and run migrations if needed
   */
  auto initialize() -> bool;

  /**
   * @brief Append a message to history (subject to whitelist rules)
   * @return true if message was saved, false if filtered out or error
   */
  auto append_message(const MessageRecord &record, bool collect_enabled,
                      const std::vector<std::string> &allowed_groups) -> bool;

  /**
   * @brief Fetch context messages for LLM prompt building
   * Filters out command messages (is_command=1) and empty content
   */
  [[nodiscard]] auto fetch_context(const ContextQuery &query)
      -> std::vector<MessageRecord>;

  /**
   * @brief Delete messages older than TTL (in days)
   */
  auto cleanup_ttl(int ttl_days) -> int;

  /**
   * @brief Get database schema version
   */
  [[nodiscard]] auto get_schema_version() const -> int;

private:
  std::string db_path_;
  void *db_ = nullptr; // sqlite3*

  /**
   * @brief Create v2 schema (current)
   */
  auto create_schema_v2() -> bool;

  /**
   * @brief Migrate from v1 to v2 schema
   */
  auto migrate_v1_to_v2() -> bool;

  /**
   * @brief Check if group is allowed for collection
   */
  [[nodiscard]] auto is_group_allowed(
      const std::string &group_id, bool collect_enabled,
      const std::vector<std::string> &allowed_groups) const -> bool;
};

} // namespace plugins::chat_llm
