#pragma once

#include "core/bot/domain_codec.hpp"
#include "core/bot/operation_result.hpp"
#include "core/bot/references.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace obcx::bot {

// Configuration input only, never a message address or an archive. The kind
// and native ID are explicit: no historical chat:/group: key parsing.
enum class ConfiguredConversationKind : std::uint8_t {
  Private,
  Group,
  Channel
};
struct ConfiguredConversation {
  ConfiguredConversationKind kind;
  std::string native_id;
  std::optional<std::int64_t> topic;

  void validate() const {
    detail::validate_identifier(native_id, "configured conversation", 1024);
    detail::validate_utf8(native_id);
    if (kind != ConfiguredConversationKind::Private &&
        kind != ConfiguredConversationKind::Group &&
        kind != ConfiguredConversationKind::Channel)
      throw std::invalid_argument("invalid configured conversation kind");
    if (topic && (kind != ConfiguredConversationKind::Group || *topic <= 0 ||
                  *topic > INT32_MAX))
      throw std::invalid_argument("invalid configured conversation topic");
  }
};

template <> struct detail::DomainRecordPreflight<ConfiguredConversation> {
  static void validate(const Json &value) {
    (void)detail::bounded_unsigned(value.at("kind"), 2);
    (void)detail::require_identifier(value, "native_id",
                                     "configured conversation", 1024);
  }
};
OBCX_DOMAIN_FIELDS(ConfiguredConversation, OBCX_DOMAIN_FIELD(kind),
                   OBCX_DOMAIN_FIELD(native_id), OBCX_DOMAIN_FIELD(topic));

// Resolves explicit current configuration for one enabled installation. No
// archive input, identity extraction, historical ID import or provider I/O.
class ConversationResolver {
public:
  virtual ~ConversationResolver() = default;
  [[nodiscard]] virtual auto resolve(const BotInstallationRef &installation,
                                     const ConfiguredConversation &target) const
      -> BotOperationResult<ConversationRef> = 0;
};

} // namespace obcx::bot
