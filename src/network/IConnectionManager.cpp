#include "network/IConnectionManager.hpp"
#include "../../include/onebot11/adapter/ProtocolAdapter.hpp"
#include "../../include/telegram/adapter/ProtocolAdapter.hpp"
#include "../../include/telegram/network/ConnectionManager.hpp"
#include "interface/IProtolAdapter.hpp"
#include "onebot11/network/http/ConnectionManager.hpp"
#include "onebot11/network/websocket/ConnectionManager.hpp"

#include <boost/asio/io_context.hpp>

namespace obcx::network {

auto ConnectionManagerFactory::create(ConnectionType type,
                                      asio::io_context &ioc,
                                      adapter::BaseProtocolAdapter &adapter)
    -> std::unique_ptr<IConnectionManager> {

  switch (type) {
  case ConnectionType::Onebot11WebSocket:
    // Cast to OneBot11 adapter
    {
      auto ob11_adapter =
          dynamic_cast<adapter::onebot11::ProtocolAdapter *>(&adapter);
      if (!ob11_adapter) {
        throw std::invalid_argument(
            "WebSocket connection requires OneBot11 adapter");
      }
      return std::make_unique<WebSocketConnectionManager>(ioc, *ob11_adapter);
    }
  case ConnectionType::Onebot11HTTP:
    // Cast to OneBot11 adapter
    {
      auto ob11_adapter =
          dynamic_cast<adapter::onebot11::ProtocolAdapter *>(&adapter);
      if (!ob11_adapter) {
        throw std::invalid_argument(
            "HTTP connection requires OneBot11 adapter");
      }
      return std::make_unique<HttpConnectionManager>(ioc, *ob11_adapter);
    }
  case ConnectionType::TelegramHTTP:
    // Cast to Telegram adapter
    {
      auto telegram_adapter =
          dynamic_cast<adapter::telegram::ProtocolAdapter *>(&adapter);
      if (!telegram_adapter) {
        throw std::invalid_argument(
            "Telegram connection requires Telegram adapter");
      }
      return std::make_unique<TelegramConnectionManager>(ioc,
                                                         *telegram_adapter);
    }
  default:
    throw std::invalid_argument("Unknown connection type");
  }
}

} // namespace obcx::network