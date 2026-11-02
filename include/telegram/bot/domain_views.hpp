#pragma once
#include "core/bot/domain_events.hpp"

namespace obcx::telegram::bot {
// Current scoped display observation; never a native-ID import or lookup grant.
struct UserIdentityView {
  obcx::bot::UserRef subject;
  std::int64_t user_id;
};
UserIdentityView user_identity_view(const obcx::bot::UserRef &);

enum class StickerFormat : std::uint8_t { StaticWebp, VideoWebm, AnimatedTgs };
struct StickerView {
  obcx::bot::ReceivedMedia media;
  StickerFormat format;
  std::optional<std::string> emoji;
};
// Original platform file fields, not a cache key recipe or a read capability.
// Callers retain the complete references; no paths, credentials or URLs are
// exposed. A missing file_unique_id stays missing.
struct FileObservationView {
  obcx::bot::MediaRef media;
  std::optional<obcx::bot::MediaIdentity> identity;
  std::string file_id;
  std::optional<std::string> file_unique_id;
};
FileObservationView file_observation_view(const obcx::bot::ReceivedMedia &);

// A current typed update observation. Missing edit time remains unknown;
// equal platform timestamps do not prove an order between different contents.
struct MessageUpdateObservation {
  obcx::bot::MessageRef source;
  std::optional<obcx::bot::TimePoint> edited_at;
  std::string content_fingerprint;
};
std::optional<MessageUpdateObservation> message_update_observation(
    const obcx::bot::MessageReceived &);

// Pure validated projections, no I/O or native addressing API.
std::optional<StickerView> sticker_view(const obcx::bot::ReceivedMedia &);
std::optional<obcx::bot::ReceivedMedia> video_note_view(
    const obcx::bot::ReceivedMedia &);
std::optional<obcx::bot::MessageUpdated> message_update_view(
    const obcx::bot::MessageReceived &);
bool is_topic_conversation(const obcx::bot::ConversationRef &);
} // namespace obcx::telegram::bot
