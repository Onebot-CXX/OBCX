#pragma once

#include "interfaces/plugin.hpp"
#include <memory>

#include "core/qq_bot.hpp"

// Forward declarations
namespace bridge {
class TelegramHandler;
}
namespace obcx::storage {
class DatabaseManager;
}
namespace bridge {
class RetryQueueManager;
}

namespace plugins {

/**
 * @brief Telegram到QQ转发插件
 *
 * 只处理Telegram消息的转发到QQ
 * 使用BridgeBot的TelegramHandler进行消息处理
 */
class TGToQQPlugin : public obcx::interface::IPlugin {
public:
  TGToQQPlugin();
  ~TGToQQPlugin() override;

  // IPlugin interface
  std::string get_name() const override;
  std::string get_version() const override;
  std::string get_description() const override;
  bool initialize() override;
  void deinitialize() override;
  void shutdown() override;

private:
  obcx::core::QQBot *qq_bot_{nullptr};
  // 简化配置
  struct Config {
    std::string database_file = "bridge_bot.db";
    bool enable_retry_queue = false;
  };

  bool load_configuration();
  boost::asio::awaitable<void> handle_tg_message(
      obcx::core::IBot &bot, const obcx::common::MessageEvent &event);

  // Configuration
  Config config_;

  // Bridge components
  std::shared_ptr<obcx::storage::DatabaseManager> db_manager_;
  std::shared_ptr<bridge::RetryQueueManager> retry_manager_;
  std::unique_ptr<bridge::TelegramHandler> telegram_handler_;
};

} // namespace plugins