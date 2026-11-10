#pragma once

// Process/platform boundary; not an ordinary actor SDK service.
#include "common/message_type.hpp"
#include "core/bot/conversation_resolver.hpp"
#include "core/bot/native_reference_codec.hpp"
#include <optional>

namespace obcx::core {
class PlatformAdapter {
public:
  virtual ~PlatformAdapter() = default;
  [[nodiscard]] virtual auto codec() const
      -> const bot::binding::NativeReferenceCodec & = 0;
  [[nodiscard]] virtual auto normalize_message(const bot::BotInstallationRef &,
                                               const common::MessageEvent &,
                                               bot::TimePoint,
                                               std::string_view) const
      -> bot::MessageReceived = 0;
  [[nodiscard]] virtual auto normalize_deleted_notice(
      const bot::BotInstallationRef &, const common::NoticeEvent &,
      bot::TimePoint) const -> std::optional<bot::MessageDeleted> {
    return std::nullopt;
  }
  [[nodiscard]] virtual auto observed_self(const bot::BotInstallationRef &,
                                           const common::MessageEvent &) const
      -> std::optional<bot::UserRef> = 0;
  [[nodiscard]] virtual auto configured_conversation(
      const bot::BotInstallationRef &,
      const bot::ConfiguredConversation &) const -> bot::ConversationRef = 0;
};
} // namespace obcx::core
