#pragma once

#include "core/bot/self_identity_cache.hpp"
#include "onebot11/bot/detail/file_query.hpp"
#include "onebot11/bot/detail/standard_call_state.hpp"
#include "standard_execution_codec.hpp"

#include <boost/asio/awaitable.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace obcx::core {
class OneBot11TransportCapability;
}

namespace obcx::onebot11::bot::standard::detail {

enum class StandardWireTransport { Http, WebSocket };

struct CapturedStandardSend {
  StandardWireTransport transport;
  boost::asio::awaitable<void> operation;
};

struct StandardTransportAccess final {
  // Capture the selected manager's owning operation before returning. The
  // facade must remain valid during this synchronous call, not across await.
  static auto send(core::OneBot11TransportCapability &transport,
                   NativeAction action, ExecutionMode mode,
                   std::string parameters, std::uint64_t echo,
                   std::shared_ptr<StandardCallState> call,
                   std::shared_ptr<core::SelfIdentityCache> identity,
                   core::SelfIdentityOrigin origin) -> CapturedStandardSend;
};

} // namespace obcx::onebot11::bot::standard::detail

namespace obcx::onebot11::bot::detail {
struct FileTransportAccess final {
  static auto send(core::OneBot11TransportCapability &transport,
                   FileQueryKind kind, std::string parameters,
                   std::uint64_t echo,
                   std::shared_ptr<standard::detail::StandardCallState> call,
                   std::shared_ptr<core::SelfIdentityCache> identity,
                   core::SelfIdentityOrigin origin)
      -> standard::detail::CapturedStandardSend;
};
} // namespace obcx::onebot11::bot::detail
