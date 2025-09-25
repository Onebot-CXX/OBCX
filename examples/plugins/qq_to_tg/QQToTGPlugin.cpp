#include "QQToTGPlugin.hpp"
#include "common/Logger.hpp"
#include "core/QQBot.hpp"
#include "core/TGBot.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include "../BridgeBot/Config.hpp"
#include "../BridgeBot/DatabaseManager.hpp"
#include "../BridgeBot/QQHandler.hpp"
#include "../BridgeBot/RetryQueueManager.hpp"
#include "common/ConfigLoader.hpp"

namespace plugins {
QQToTGPlugin::QQToTGPlugin() { OBCX_DEBUG("QQToTGPlugin constructor called"); }

QQToTGPlugin::~QQToTGPlugin() {
  shutdown();
  OBCX_DEBUG("QQToTGPlugin destructor called");
}

std::string QQToTGPlugin::get_name() const { return "qq_to_tg"; }

std::string QQToTGPlugin::get_version() const { return "1.0.0"; }

std::string QQToTGPlugin::get_description() const {
  return "QQ to Telegram message forwarding plugin (simplified version)";
}

bool QQToTGPlugin::initialize() {
  try {
    OBCX_INFO("Initializing QQ to TG Plugin...");

    // Initialize bridge configuration system
    bridge::initialize_config();

    // Load configuration
    if (!load_configuration()) {
      OBCX_ERROR("Failed to load plugin configuration");
      return false;
    }

    // Initialize database manager
    db_manager_ =
        std::make_shared<obcx::storage::DatabaseManager>(config_.database_file);
    if (!db_manager_->initialize()) {
      OBCX_ERROR("Failed to initialize database");
      return false;
    }

    // Initialize retry queue manager if enabled
    if (config_.enable_retry_queue) {
      // Create a dedicated io_context for retry queue
      static boost::asio::io_context retry_io_context;
      retry_manager_ = std::make_shared<bridge::RetryQueueManager>(
          db_manager_, retry_io_context);
    }

    // Create QQHandler instance
    qq_handler_ =
        std::make_unique<bridge::QQHandler>(db_manager_, retry_manager_);

    // Register event callbacks
    try {
      // 获取所有bot实例的带锁访问
      auto [lock, bots] = get_bots();

      // 找到QQ bot并注册消息回调
      for (auto &bot_ptr : bots) {
        if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
          qq_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_qq_message(bot, event);
              });
          OBCX_INFO("Registered QQ message callback for QQ to TG plugin");
          break;
        }
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("Failed to register callbacks: {}", e.what());
      return false;
    }

    OBCX_INFO("QQ to TG Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during QQ to TG Plugin initialization: {}", e.what());
    return false;
  }
}

void QQToTGPlugin::deinitialize() {
  try {
    OBCX_INFO("Deinitializing QQ to TG Plugin...");
    // Note: Bot callbacks will be automatically cleaned up when plugin is
    // unloaded If needed, specific cleanup can be added here
    OBCX_INFO("QQ to TG Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during QQ to TG Plugin deinitialization: {}",
               e.what());
  }
}

void QQToTGPlugin::shutdown() {
  try {
    OBCX_INFO("Shutting down QQ to TG Plugin...");
    OBCX_INFO("QQ to TG Plugin shutdown complete");
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during QQ to TG Plugin shutdown: {}", e.what());
  }
}

boost::asio::awaitable<void> QQToTGPlugin::handle_qq_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event) {
  // 确保这是QQ bot的消息
  if (auto *qq_bot = dynamic_cast<obcx::core::QQBot *>(&bot)) {
    OBCX_INFO("QQ to TG Plugin: Processing QQ message from group {}",
              event.group_id.value_or("unknown"));

    try {
      if (!tg_bot_) {
        auto [lock, bots] = get_bots();

        for (auto &bot_ptr : bots) {
          if (auto *tg = dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
            tg_bot_ = tg;
            break;
          }
        }
      }

      if (tg_bot_ && qq_handler_) {
        OBCX_INFO("Found Telegram bot, performing QQ->TG message forwarding "
                  "using QQHandler");
        co_await qq_handler_->forward_to_telegram(*tg_bot_, *qq_bot, event);
      } else {
        OBCX_WARN("Telegram bot or QQHandler not found for QQ->TG forwarding");
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("Error accessing bot list: {}", e.what());
    }
  }

  co_return;
}

bool QQToTGPlugin::load_configuration() {
  try {
    // 从插件配置加载设置
    config_.database_file = get_config_value<std::string>("database_file")
                                .value_or("bridge_bot.db");
    config_.enable_retry_queue =
        get_config_value<bool>("enable_retry_queue").value_or(false);

    OBCX_INFO("QQ to TG configuration loaded: database={}, retry_queue={}",
              config_.database_file, config_.enable_retry_queue);
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to load QQ to TG configuration: {}", e.what());
    return false;
  }
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::QQToTGPlugin)
