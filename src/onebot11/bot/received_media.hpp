#pragma once

#include "core/bot/bot_event_components.hpp"
#include "network/http_call.hpp"
#include "onebot11/bot/domain_binding.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace obcx::onebot11::bot::detail {

[[nodiscard]] auto is_received_media_kind(std::string_view kind) -> bool;
void validate_received_media_token(std::string_view token);

// Installation-local FIFO indexed payload bound, not an outstanding-I/O or RSS
// bound. Readers own retired entries while draining, but cannot return success.
class ReceivedMediaStore final {
public:
  ReceivedMediaStore(obcx::bot::BotInstallationRef installation,
                     std::shared_ptr<core::SelfIdentityCache> identity,
                     std::weak_ptr<core::BotEventCapability> ingress,
                     std::shared_ptr<const QqDomainBinding> binding,
                     std::size_t maximum_entries,
                     std::size_t maximum_retained_text_bytes);
  ~ReceivedMediaStore();

  auto normalize(const QqDomainBinding &binding,
                 const obcx::bot::BotInstallationRef &installation,
                 const common::MessageEvent &event,
                 obcx::bot::TimePoint received_at,
                 std::string_view observation_id,
                 const core::SelfIdentityOrigin &origin)
      -> obcx::bot::MessageReceived;
  // Process-only: called from a completed origin-bound forward query, never
  // from archive restoration or the synthetic event used for body parsing.
  // selector is the query wrapper, NOT a node MessageRef/file origin group.
  auto retain_forward_media(const obcx::bot::MessageRef &selector,
                            std::size_t node_index, std::size_t segment_ordinal,
                            std::string_view native_type,
                            std::string_view actual_url,
                            obcx::bot::MediaMetadata metadata,
                            const core::SelfIdentityOrigin &origin)
      -> obcx::bot::ReceivedMedia;
  auto read(const obcx::bot::MediaRef &media, std::uint64_t maximum_bytes,
            network::detail::HttpDeadline deadline)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<obcx::bot::MediaBlob>>;

private:
  struct Receipt;
  auto capture(std::string_view kind, std::string_view url,
               const core::SelfIdentityOrigin &origin,
               const std::optional<obcx::bot::UserRef> &observed_user,
               const obcx::bot::MessageRef &source, std::size_t ordinal,
               std::optional<std::uint64_t> declared_size,
               std::optional<obcx::bot::MediaMetadata> query_metadata,
               std::optional<std::size_t> query_node) -> std::string;
  static auto read_owned(std::shared_ptr<Receipt> receipt,
                         std::shared_ptr<core::SelfIdentityCache> identity,
                         std::weak_ptr<core::BotEventCapability> ingress,
                         std::uint64_t maximum_bytes,
                         network::detail::HttpDeadline deadline)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<obcx::bot::MediaBlob>>;
  void evict_oldest();

  const obcx::bot::BotInstallationRef installation_;
  const std::shared_ptr<core::SelfIdentityCache> identity_;
  const std::weak_ptr<core::BotEventCapability> ingress_;
  const std::shared_ptr<const QqDomainBinding> binding_;
  const std::size_t maximum_entries_;
  const std::size_t maximum_retained_text_bytes_;
  const std::string store_uuid_;
  mutable std::mutex mutex_;
  std::uint64_t sequence_{};
  std::size_t retained_text_bytes_{};
  std::map<std::string, std::shared_ptr<Receipt>, std::less<>> receipts_;
  std::deque<std::shared_ptr<Receipt>> fifo_;
};
} // namespace obcx::onebot11::bot::detail
