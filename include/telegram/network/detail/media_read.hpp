#pragma once

// Process-private native read admission. Not part of the installed actor SDK.
#include "core/bot/domain_queries.hpp"
#include "network/http_call.hpp"
#include "telegram/network/connection_manager.hpp"

namespace obcx::core {
class TelegramTransport;
}

namespace obcx::network::detail {

class TelegramMediaReadAccess final {
public:
  using Result = obcx::bot::BotOperationResult<obcx::bot::MediaBlob>;

  static auto available(const core::TelegramTransport &) -> bool;
  // Ordinary function: freeze manager/client/epoch/token before returning work.
  static auto read(const core::TelegramTransport &,
                   const std::optional<core::SelfIdentityOrigin> &,
                   const obcx::bot::ReadMediaRequest &, HttpDeadline)
      -> asio::awaitable<Result>;

private:
  static auto run(std::shared_ptr<TelegramConnectionManager>,
                  std::optional<TelegramReceiveOrigin>,
                  obcx::bot::ReadMediaRequest, HttpDeadline)
      -> asio::awaitable<Result>;
};

} // namespace obcx::network::detail
