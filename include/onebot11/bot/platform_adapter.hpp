#pragma once

#include "core/bot/platform_adapter.hpp"

namespace obcx::onebot11::bot {
// Process-private: actors restore opaque values, never construct native tuples.
class QqNativeReferenceCodec final
    : public obcx::bot::binding::NativeReferenceCodec {
public:
  auto surface() const -> obcx::bot::SurfaceId override;
  void validate_native(std::string_view tag, std::string_view kind,
                       std::span<const std::string> components) const override;
  auto normalize_username(std::string_view username) const
      -> std::string override;
};

class QqPlatformAdapter final : public core::PlatformAdapter {
public:
  auto codec() const -> const QqNativeReferenceCodec & override;
  // Process-only observation helper; grants no read or filesystem authority.
  static auto file_metadata(const obcx::bot::Json &)
      -> obcx::bot::MediaMetadata;
  static auto attachment_metadata(std::string_view type,
                                  const obcx::bot::Json &)
      -> obcx::bot::MediaMetadata;
  auto normalize_message(const obcx::bot::BotInstallationRef &,
                         const common::MessageEvent &, obcx::bot::TimePoint,
                         std::string_view observation_id) const
      -> obcx::bot::MessageReceived override;
  auto normalize_deleted_notice(const obcx::bot::BotInstallationRef &,
                                const common::NoticeEvent &,
                                obcx::bot::TimePoint) const
      -> std::optional<obcx::bot::MessageDeleted> override;
  auto normalize_with_positions(const obcx::bot::BotInstallationRef &,
                                const common::MessageEvent &,
                                obcx::bot::TimePoint, std::string_view,
                                std::vector<std::size_t> &positions) const
      -> obcx::bot::MessageReceived;
  auto observed_self(const obcx::bot::BotInstallationRef &,
                     const common::MessageEvent &) const
      -> std::optional<obcx::bot::UserRef> override;
  auto configured_conversation(const obcx::bot::BotInstallationRef &,
                               const obcx::bot::ConfiguredConversation &) const
      -> obcx::bot::ConversationRef override;

private:
  static void capture_observations(const common::MessageEvent &,
                                   obcx::bot::MessageReceived &,
                                   const std::vector<std::size_t> &);
  QqNativeReferenceCodec codec_;
};
} // namespace obcx::onebot11::bot
