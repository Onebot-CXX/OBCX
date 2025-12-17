#include "tg_to_qq_plugin.hpp"
#include "common/logger.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include "../dependency/bridge_bot/config.hpp"
#include "../dependency/bridge_bot/database_manager.hpp"
#include "../dependency/bridge_bot/retry_queue_manager.hpp"
#include "../dependency/bridge_bot/telegram_handler.hpp"

namespace plugins {
TGToQQPlugin::TGToQQPlugin() {
  PLUGIN_DEBUG("tg_to_qq", "TGToQQPlugin constructor called");
}

TGToQQPlugin::~TGToQQPlugin() {
  shutdown();
  PLUGIN_DEBUG("tg_to_qq", "TGToQQPlugin destructor called");
}

std::string TGToQQPlugin::get_name() const { return "tg_to_qq"; }

std::string TGToQQPlugin::get_version() const { return "1.0.0"; }

std::string TGToQQPlugin::get_description() const {
  return "Telegram to QQ message forwarding plugin (simplified version)";
}

bool TGToQQPlugin::initialize() {
  try {
    PLUGIN_INFO(get_name(), "Initializing TG to QQ Plugin...");

    // Initialize bridge configuration system
    bridge::initialize_config();

    // Load configuration
    if (!load_configuration()) {
      PLUGIN_ERROR(get_name(), "Failed to load plugin configuration");
      return false;
    }

    // Initialize database manager
    db_manager_ =
        std::make_shared<obcx::storage::DatabaseManager>(config_.database_file);
    if (!db_manager_->initialize()) {
      PLUGIN_ERROR(get_name(), "Failed to initialize database");
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
          PLUGIN_INFO(
              get_name(),
              "Registered Telegram message callback for TG to QQ plugin");
          break;
        }
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Failed to register callbacks: {}", e.what());
      return false;
    }

    PLUGIN_INFO(get_name(), "TG to QQ Plugin initialized successfully");
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(),
                 "Exception during TG to QQ Plugin initialization: {}",
                 e.what());
    return false;
  }
}

void TGToQQPlugin::deinitialize() {
  try {
    PLUGIN_INFO(get_name(), "Deinitializing TG to QQ Plugin...");
    // Note: Bot callbacks will be automatically cleaned up when plugin is
    // unloaded If needed, specific cleanup can be added here
    PLUGIN_INFO(get_name(), "TG to QQ Plugin deinitialized successfully");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(),
                 "Exception during TG to QQ Plugin deinitialization: {}",
                 e.what());
  }
}

void TGToQQPlugin::shutdown() {
  try {
    PLUGIN_INFO(get_name(), "Shutting down TG to QQ Plugin...");
    PLUGIN_INFO(get_name(), "TG to QQ Plugin shutdown complete");
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Exception during TG to QQ Plugin shutdown: {}",
                 e.what());
  }
}

boost::asio::awaitable<void> TGToQQPlugin::handle_tg_message(
    obcx::core::IBot &bot, const obcx::common::MessageEvent &event) {
  // 确保这是Telegram bot的消息
  if (auto *tg_bot = dynamic_cast<obcx::core::TGBot *>(&bot)) {
    PLUGIN_INFO(get_name(),
                "TG to QQ Plugin: Processing Telegram message from chat {}",
                event.group_id.value_or("unknown"));

    try {
      // 获取所有bot实例的带锁访问
      if (qq_bot_ == nullptr) {
        auto [lock, bots] = get_bots();

        // 找到QQ bot
        for (auto &bot_ptr : bots) {
          if (auto *qq = dynamic_cast<obcx::core::QQBot *>(bot_ptr.get())) {
            qq_bot_ = qq;
            break;
          }
        }
      }

      if (qq_bot_ != nullptr && telegram_handler_) {
        // Check if this is an edited message
        bool is_edited = (event.sub_type == "edited") ||
                         (event.data.contains("is_edited") &&
                          event.data["is_edited"].get<bool>());

        if (is_edited) {
          PLUGIN_INFO(get_name(),
                      "Detected edited message, handling as edit event");
          co_await telegram_handler_->handle_message_edited(*tg_bot, *qq_bot_,
                                                            event);
        } else {
          PLUGIN_INFO(
              get_name(),
              "Found QQ bot, performing TG->QQ message forwarding using "
              "TelegramHandler");
          co_await telegram_handler_->forward_to_qq(*tg_bot, *qq_bot_, event);
        }
      } else {
        PLUGIN_WARN(
            get_name(),
            "QQ bot or TelegramHandler not found for TG->QQ forwarding");
      }
    } catch (const std::exception &e) {
      PLUGIN_ERROR(get_name(), "Error accessing bot list: {}", e.what());
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

    PLUGIN_INFO(get_name(),
                "TG to QQ configuration loaded: database={}, retry_queue={}",
                config_.database_file, config_.enable_retry_queue);
    return true;
  } catch (const std::exception &e) {
    PLUGIN_ERROR(get_name(), "Failed to load TG to QQ configuration: {}",
                 e.what());
    return false;
  }
}

} // namespace plugins

// Export the plugin
OBCX_PLUGIN_EXPORT(plugins::TGToQQPlugin)
