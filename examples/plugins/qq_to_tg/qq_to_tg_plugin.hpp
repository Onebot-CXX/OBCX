#pragma once

#include "interfaces/plugin.hpp"
#include <memory>

#include "core/tg_bot.hpp"

// Forward declarations
namespace bridge {
class QQHandler;
}

namespace obcx::storage {
class DatabaseManager;
}

namespace bridge {
class RetryQueueManager;
}

namespace plugins {
/**
 * @brief QQ到Telegram转发插件
 *
 * 只处理QQ消息的转发到Telegram
 * 使用BridgeBot的QQHandler进行消息处理
 */
class QQToTGPlugin : public obcx::interface::IPlugin {
public:
  QQToTGPlugin();

  ~QQToTGPlugin() override;

  // IPlugin interface
  std::string get_name() const override;

  std::string get_version() const override;

  std::string get_description() const override;

  bool initialize() override;

  void deinitialize() override;

  void shutdown() override;

private:
  obcx::core::TGBot *tg_bot_{nullptr};

  struct Config {
    std::string database_file = "bridge_bot.db";
    bool enable_retry_queue = false;
  };

  bool load_configuration();

  boost::asio::awaitable<void> handle_qq_message(
      obcx::core::IBot &bot, const obcx::common::MessageEvent &event);

  boost::asio::awaitable<void> handle_qq_heartbeat(
      obcx::core::IBot &bot, const obcx::common::HeartbeatEvent &event);

  // Configuration
  Config config_;

  // Bridge components
  std::shared_ptr<obcx::storage::DatabaseManager> db_manager_;
  std::shared_ptr<bridge::RetryQueueManager> retry_manager_;
  std::unique_ptr<bridge::QQHandler> qq_handler_;
};
} // namespace plugins
