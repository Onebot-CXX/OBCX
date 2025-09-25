#include "TGToQQPlugin.hpp"
#include "common/Logger.hpp"
#include "core/QQBot.hpp"
#include "core/TGBot.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include "../BridgeBot/Config.hpp"
#include "../BridgeBot/DatabaseManager.hpp"
#include "../BridgeBot/RetryQueueManager.hpp"
#include "../BridgeBot/TelegramHandler.hpp"

namespace plugins {
TGToQQPlugin::TGToQQPlugin() { OBCX_DEBUG("TGToQQPlugin constructor called"); }

TGToQQPlugin::~TGToQQPlugin() {
  shutdown();
  OBCX_DEBUG("TGToQQPlugin destructor called");
}

std::string TGToQQPlugin::get_name() const { return "tg_to_qq"; }

std::string TGToQQPlugin::get_version() const { return "1.0.0"; }

std::string TGToQQPlugin::get_description() const {
  return "Telegram to QQ message forwarding plugin (simplified version)";
}

bool TGToQQPlugin::initialize() {
  try {
    OBCX_INFO("Initializing TG to QQ Plugin...");

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

    // Create TelegramHandler instance
    telegram_handler_ =
        std::make_unique<bridge::TelegramHandler>(db_manager_, retry_manager_);

    // Register event callbacks
    try {
      // 获取所有bot实例的带锁访问
      auto [lock, bots] = get_bots();

      // 找到Telegram bot并注册消息回调
      for (auto &bot_ptr : bots) {
        if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(bot_ptr.get())) {
          tg_bot->on_event<obcx::common::MessageEvent>(
              [this](obcx::core::IBot &bot,
                     const obcx::common::MessageEvent &event)
                  -> boost::asio::awaitable<void> {
                co_await handle_tg_message(bot, event);
              });
          OBCX_INFO("Registered Telegram message callback for TG to QQ plugin");
          break;
        }
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("Failed to register callbacks: {}", e.what());
      return false;
    }

    OBCX_INFO("TG to QQ Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during TG to QQ Plugin initialization: {}", e.what());
    return false;
  }
}

void TGToQQPlugin::deinitialize() {
  try {
    OBCX_INFO("Deinitializing TG to QQ Plugin...");
    // Note: Bot callbacks will be automatically cleaned up when plugin is
    // unloaded If needed, specific cleanup can be added here
    OBCX_INFO("TG to QQ Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during TG to QQ Plugin deinitialization: {}",
               e.what());
  }
}

void TGToQQPlugin::shutdown() {
  try {
    OBCX_INFO("Shutting down TG to QQ Plugin...");
    OBCX_INFO("TG to QQ Plugin shutdown complete");
  } catch (const std::exception &e) {
    OBCX_ERROR("Exception during TG to QQ Plugin shutdown: {}", e.what());
  }
}

boost::asio::awaitable<void> TGToQQPlugin::handle_tg_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event) {
  // 确保这是Telegram bot的消息
  if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&bot)) {
    OBCX_INFO("TG to QQ Plugin: Processing Telegram message from chat {}",
              event.group_id.value_or("unknown"));

    try {
      // 获取所有bot实例的带锁访问
      if (!qq_bot_) {
        auto [lock, bots] = get_bots();

        // 找到QQ bot
        for (auto &bot_ptr : bots) {
          if (auto *qq = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
            qq_bot_ = qq;
            break;
          }
        }
      }

      if (qq_bot_ && telegram_handler_) {
        OBCX_INFO("Found QQ bot, performing TG->QQ message forwarding using "
                  "TelegramHandler");
        co_await telegram_handler_->forward_to_qq(*tg_bot, *qq_bot_, event);
      } else {
        OBCX_WARN("QQ bot or TelegramHandler not found for TG->QQ forwarding");
      }
    } catch (const std::exception &e) {
      OBCX_ERROR("Error accessing bot list: {}", e.what());
    }
  }

  co_return;
}

bool TGToQQPlugin::load_configuration() {
  try {
    config_.database_file = get_config_value<std::string>("database_file")
                                .value_or("bridge_bot.db");
    config_.enable_retry_queue =
        get_config_value<bool>("enable_retry_queue").value_or(false);

    OBCX_INFO("TG to QQ configuration loaded: database={}, retry_queue={}",
              config_.database_file, config_.enable_retry_queue);
    return true;
  } catch (const std::exception &e) {
    OBCX_ERROR("Failed to load TG to QQ configuration: {}", e.what());
    return false;
  }
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::TGToQQPlugin)
