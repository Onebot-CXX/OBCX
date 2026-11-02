#pragma once

#include "core/bot/native_reference_codec.hpp"

#include <deque>
#include <mutex>

namespace obcx::core {

// Installation-owned evidence, never populated by persistence restoration or
// actor requests. Evidence belongs to the connected account generation;
// reconnect/account fences invalidate it before new admission. No independent
// capacity or expiry configuration is imposed on application startup.
class ReplyEvidenceStore final : public bot::binding::ReplyEvidenceVerifier {
public:
  explicit ReplyEvidenceStore(bot::BotInstallationRef installation);
  [[nodiscard]] auto generation() const -> std::optional<std::uint64_t>;
  [[nodiscard]] auto admit(const bot::MessageReceived &message,
                           std::uint64_t expected_generation) -> bool;
  [[nodiscard]] auto admit_confirmation(const bot::MessageRef &message,
                                        const bot::Sender &author,
                                        std::uint64_t expected_generation)
      -> bool;
  [[nodiscard]] auto admit_confirmation_conflict(
      const bot::MessageRef &message, std::uint64_t expected_generation)
      -> bool;
  [[nodiscard]] auto admits(const bot::ReplyRef &reply) const -> bool override;
  [[nodiscard]] auto find(const bot::ReplyRef &reply) const
      -> std::optional<bot::ReplyInfo>;
  void invalidate() noexcept;
  void close() noexcept;

private:
  struct Entry {
    // A relation entry's author belongs to its target, never its source.
    bot::MessageRef message;
    std::optional<bot::Sender> author;
    std::optional<bot::ReplyInfo> reply;
    bool author_conflicted;
  };
  [[nodiscard]] auto conflicts(const Entry &entry) const -> bool;
  void insert(Entry entry);
  bot::BotInstallationRef installation_;
  mutable std::mutex mutex_;
  std::deque<Entry> entries_;
  std::uint64_t generation_{};
  bool closed_{};
};

} // namespace obcx::core
