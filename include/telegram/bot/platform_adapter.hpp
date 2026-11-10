#pragma once
#include "core/bot/platform_adapter.hpp"

namespace obcx::telegram::bot {
// Process-only codec and native projection. Actor extensions are in
// bot_views.hpp.
class TelegramReferenceCodec final
    : public obcx::bot::binding::NativeReferenceCodec {
public:
  obcx::bot::SurfaceId surface() const override;
  void validate_native(std::string_view tag, std::string_view kind,
                       std::span<const std::string> components) const override;
  std::string normalize_username(std::string_view username) const override;
};
const TelegramReferenceCodec &reference_codec();
std::shared_ptr<const core::PlatformAdapter> platform_adapter();
obcx::bot::ConversationRef native_conversation(
    const obcx::bot::BotInstallationRef &, const obcx::bot::Json &message);
obcx::bot::MessageRef native_message_reference(
    const obcx::bot::BotInstallationRef &, const obcx::bot::Json &);
obcx::bot::UserRef native_user(const obcx::bot::BotInstallationRef &,
                               std::string id);
// Same selected-field precedence as ingress. Malformed selected identity
// throws; send proof callers isolate that failure without falling back.
obcx::bot::Sender native_sender(const obcx::bot::BotInstallationRef &,
                                const obcx::bot::Json &message);
obcx::bot::MessageReceived normalize_native_message(
    const obcx::bot::BotInstallationRef &, const obcx::bot::Json &,
    obcx::bot::TimePoint, std::string_view observation);
std::optional<obcx::bot::ReceivedMedia> native_media(
    const obcx::bot::BotInstallationRef &, const obcx::bot::Json &message);
} // namespace obcx::telegram::bot
