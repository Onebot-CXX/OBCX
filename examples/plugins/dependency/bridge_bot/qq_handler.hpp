#pragma once

#include "common/message_type.hpp"
#include "database_manager.hpp"
#include "interfaces/bot.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace bridge {

// Forward declarations
class RetryQueueManager;

/**
 * @brief QQ消息处理器
 *
 * 处理从QQ到Telegram的消息转发
 */
class QQHandler {
public:
  /**
   * @brief 构造函数
   * @param db_manager 数据库管理器的引用
   * @param retry_manager 重试队列管理器（可选）
   */
  explicit QQHandler(
      std::shared_ptr<obcx::storage::DatabaseManager> db_manager,
      std::shared_ptr<RetryQueueManager> retry_manager = nullptr);

  /**
   * @brief 将QQ消息转发到Telegram
   * @param telegram_bot Telegram机器人实例
   * @param qq_bot QQ机器人实例
   * @param event QQ消息事件
   * @return 处理结果的awaitable
   */
  auto forward_to_telegram(obcx::core::IBot &telegram_bot,
                           obcx::core::IBot &qq_bot,
                           obcx::common::MessageEvent event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 处理QQ撤回事件
   * @param telegram_bot Telegram机器人实例
   * @param qq_bot QQ机器人实例
   * @param event QQ撤回事件
   * @return 处理结果的awaitable
   */
  auto handle_recall_event(obcx::core::IBot &telegram_bot,
                           obcx::core::IBot &qq_bot, obcx::common::Event event)
      -> boost::asio::awaitable<void>;

  /**
   * @brief 处理 /checkalive 命令
   * @param telegram_bot Telegram机器人实例
   * @param qq_bot QQ机器人实例
   * @param event 包含 /checkalive 命令的消息事件
   * @param telegram_group_id 对应的Telegram群ID
   * @return 处理结果的awaitable
   */
  auto handle_checkalive_command(obcx::core::IBot &telegram_bot,
                                 obcx::core::IBot &qq_bot,
                                 obcx::common::MessageEvent event,
                                 const std::string &telegram_group_id)
      -> boost::asio::awaitable<void>;

private:
  std::shared_ptr<obcx::storage::DatabaseManager> db_manager_;
  std::shared_ptr<RetryQueueManager> retry_manager_;
};

} // namespace bridge