#include "common/component_manager.hpp"
#include "common/i18n_log_messages.hpp"
#include "common/plugin_manager.hpp"
#include "core/qq_bot.hpp"
#include "core/tg_bot.hpp"
#include "interfaces/bot.hpp"
#include "onebot11/adapter/protocol_adapter.hpp"
#include "telegram/adapter/protocol_adapter.hpp"

#include <cstdint>

namespace {
const uint16_t DEFAULT_PORT = 8080;
}

namespace obcx::common {

auto obcx::common::ComponentManager::instance()
    -> obcx::common::ComponentManager & {
  static ComponentManager instance;
  return instance;
}

auto obcx::common::ComponentManager::create_bot(
    const BotConfig &config,
    const std::shared_ptr<::obcx::core::TaskScheduler> &task_scheduler)
    -> std::unique_ptr<::obcx::core::IBot> {
  if (config.type == "qq") {
    return std::make_unique<::obcx::core::QQBot>(
        ::obcx::adapter::onebot11::ProtocolAdapter{}, task_scheduler);
  }
  if (config.type == "telegram") {
    return std::make_unique<::obcx::core::TGBot>(
        ::obcx::adapter::telegram::ProtocolAdapter{}, task_scheduler);
  }

  OBCX_I18N_ERROR(common::LogMessageKey::UNKNOWN_BOT_TYPE, config.type);
  return nullptr;
}

auto ComponentManager::get_connection_type(const std::string &type,
                                           const std::string &bot_type)
    -> ::obcx::network::ConnectionManagerFactory::ConnectionType {
  if (bot_type == "qq") {
    if (type == "websocket" || type == "ws") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          Onebot11WebSocket;
    }
    if (type == "http") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          Onebot11HTTP;
    }
  } else if (bot_type == "telegram") {
    if (type == "websocket" || type == "ws") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          TelegramWebsocket;
    }
    if (type == "http") {
      return ::obcx::network::ConnectionManagerFactory::ConnectionType::
          TelegramHTTP;
    }
  }

  OBCX_I18N_ERROR(common::LogMessageKey::UNKNOWN_CONNECTION_TYPE, type,
                  bot_type);
  return ::obcx::network::ConnectionManagerFactory::ConnectionType::
      Onebot11HTTP;
}

auto ComponentManager::create_connection_config(const toml::table &conn_table)
    -> ConnectionConfig {
  ConnectionConfig config;

  if (const auto *host = conn_table.get("host")) {
    config.host = host->value_or<std::string>("localhost");
  } else {
    config.host = "localhost";
  }

  if (const auto *port = conn_table.get("port")) {
    config.port = port->value_or(DEFAULT_PORT);
  } else {
    config.port = DEFAULT_PORT;
  }

  if (const auto *token = conn_table.get("access_token")) {
    config.access_token = token->value_or<std::string>("");
  } else {
    config.access_token = "";
  }

  if (const auto *secret = conn_table.get("secret")) {
    config.secret = secret->value_or<std::string>("");
  } else {
    config.secret = "";
  }

  if (const auto *ssl = conn_table.get("use_ssl")) {
    config.use_ssl = ssl->value_or<bool>(false);
  } else {
    config.use_ssl = false;
  }

  if (const auto *timeout = conn_table.get("timeout")) {
    if (auto timeout_ms = timeout->value<int64_t>()) {
      config.timeout = std::chrono::milliseconds(*timeout_ms);
    }
  }

  if (const auto *poll_timeout = conn_table.get("poll_timeout")) {
    if (auto poll_timeout_ms = poll_timeout->value<int64_t>()) {
      config.poll_timeout = std::chrono::milliseconds(*poll_timeout_ms);
    }
  }

  if (const auto *poll_force_close = conn_table.get("poll_force_close")) {
    if (auto poll_force_close_ms = poll_force_close->value<int64_t>()) {
      config.poll_force_close = std::chrono::milliseconds(*poll_force_close_ms);
    }
  }

  if (const auto *poll_retry_interval = conn_table.get("poll_retry_interval")) {
    if (auto poll_retry_interval_ms = poll_retry_interval->value<int64_t>()) {
      config.poll_retry_interval =
          std::chrono::milliseconds(*poll_retry_interval_ms);
    }
  }

  if (const auto *heartbeat_interval = conn_table.get("heartbeat_interval")) {
    if (auto interval_ms = heartbeat_interval->value<int64_t>()) {
      config.heartbeat_interval = std::chrono::milliseconds(*interval_ms);
    }
  }

  // Proxy configuration
  if (const auto *proxy_host = conn_table.get("proxy_host")) {
    config.proxy_host = proxy_host->value_or<std::string>("");
  } else {
    config.proxy_host = "";
  }

  if (const auto *proxy_port = conn_table.get("proxy_port")) {
    config.proxy_port = proxy_port->value_or<uint16_t>(0);
  } else {
    config.proxy_port = 0;
  }

  if (const auto *proxy_type = conn_table.get("proxy_type")) {
    config.proxy_type = proxy_type->value_or<std::string>("http");
  } else {
    config.proxy_type = "http";
  }

  // Debug logging for proxy configuration
  OBCX_I18N_INFO(common::LogMessageKey::PROXY_CONFIG_INFO, config.proxy_host,
                 config.proxy_port, config.proxy_type);

  if (const auto *proxy_username = conn_table.get("proxy_username")) {
    config.proxy_username = proxy_username->value_or<std::string>("");
  } else {
    config.proxy_username = "";
  }

  if (const auto *proxy_password = conn_table.get("proxy_password")) {
    config.proxy_password = proxy_password->value_or<std::string>("");
  } else {
    config.proxy_password = "";
  }

  return config;
}

auto ComponentManager::setup_bot(::obcx::core::IBot &bot,
                                 const BotConfig &config,
                                 PluginManager &plugin_manager) -> bool {
  try {
    // Sort plugins by priority and dependencies
    auto sorted_plugins =
        plugin_manager.sort_plugins_by_priority_and_dependencies(
            config.plugins);

    // Log the sorted plugin order
    if (!sorted_plugins.empty()) {
      std::string order_info;
      for (size_t i = 0; i < sorted_plugins.size(); ++i) {
        order_info += sorted_plugins[i];
        if (i < sorted_plugins.size() - 1) {
          order_info += " -> ";
        }
      }
      OBCX_I18N_INFO(common::LogMessageKey::PLUGIN_LOAD_ORDER_INFO, order_info);
    }

    // Load and initialize plugins in sorted order
    for (const auto &plugin_name : sorted_plugins) {
      if (!plugin_manager.load_plugin(plugin_name)) {
        OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_LOAD_WARN, plugin_name);
        continue;
      }

      if (!plugin_manager.initialize_plugin(plugin_name)) {
        OBCX_I18N_WARN(common::LogMessageKey::PLUGIN_INIT_WARN, plugin_name);
        continue;
      }
    }

    // Setup connection
    auto connection_config = create_connection_config(config.connection);
    std::string conn_type =
        config.connection.get("type")->value_or<std::string>("http");

    bot.connect(get_connection_type(conn_type, config.type), connection_config);

    OBCX_I18N_INFO(common::LogMessageKey::BOT_SETUP_SUCCESS);
    return true;

  } catch (const std::exception &e) {
    OBCX_I18N_ERROR(common::LogMessageKey::BOT_SETUP_FAILED, e.what());
    return false;
  }
}

} // namespace obcx::common
