#pragma once
#include "core/bot/domain_queries.hpp"
#include "core/bot/send_gateway_codec.hpp"
namespace obcx::telegram::bot {
struct SendStickerRequest {
  inline static const obcx::bot::ActionId action{"telegram.sticker.send"};
  obcx::bot::ConversationRef target;
  obcx::bot::MediaRef sticker;
  std::optional<obcx::bot::MessageRef> reply_to;
  std::size_t maximum_bytes;
  void validate() const {
    target.validate();
    sticker.validate();
    if (target.installation.surface !=
            obcx::bot::SurfaceId{"telegram.bot_api"} ||
        sticker.installation != target.installation || !maximum_bytes ||
        maximum_bytes > obcx::bot::detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid sticker scope or budget");
    if (reply_to) {
      reply_to->validate();
      if (reply_to->conversation != target)
        throw std::invalid_argument("sticker reply scope mismatch");
    }
  }
};
void validate_sticker_outcome(const SendStickerRequest &,
                              const obcx::bot::SendOutcome &);
obcx::bot::SendOutcome decode_sticker_outcome(
    const obcx::bot::Json &, const SendStickerRequest &,
    const obcx::bot::SendOutcomeEnvelope &);
obcx::bot::SendOutcome normalize_sticker_outcome(
    const SendStickerRequest &, obcx::bot::SendOutcome,
    const obcx::bot::SendOutcomeEnvelope &);
struct EditTextRequest {
  inline static const obcx::bot::ActionId action{"telegram.message.edit_text"};
  obcx::bot::MessageRef message;
  obcx::bot::RichText text;
  void validate() const {
    message.validate();
    obcx::bot::validate_rich_text(text, message.conversation.installation);
  }
};
// Telegram-only formatting. Do not extend the portable TextStyle enum.
struct StrikethroughText {
  std::string content;
  void validate() const {
    if (content.empty() ||
        content.size() > obcx::bot::detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid Telegram strikethrough text");
    obcx::bot::detail::validate_utf8(content);
  }
};
using FormattedText = std::vector<
    std::variant<obcx::bot::Text, obcx::bot::Mention, StrikethroughText>>;
struct EditFormattedTextRequest {
  inline static const obcx::bot::ActionId action{
      "telegram.message.edit_formatted_text"};
  obcx::bot::MessageRef message;
  FormattedText text;
  void validate() const {
    message.validate();
    if (message.conversation.installation.surface !=
            obcx::bot::SurfaceId{"telegram.bot_api"} ||
        text.empty() || text.size() > obcx::bot::detail::domain_maximum_items)
      throw std::invalid_argument("invalid Telegram formatted edit");
    for (const auto &part : text)
      std::visit(
          [&](const auto &value) {
            value.validate();
            if constexpr (std::same_as<std::decay_t<decltype(value)>,
                                       obcx::bot::Mention>) {
              if (value.user.installation != message.conversation.installation)
                throw std::invalid_argument(
                    "Telegram edit mention scope mismatch");
            }
          },
          part);
  }
};
struct EditTextReceipt {
  obcx::bot::MessageRef message;
  void validate() const { message.validate(); }
};
struct CreateTopicRequest {
  inline static const obcx::bot::ActionId action{"telegram.topic.create"};
  obcx::bot::ConversationRef parent;
  std::string name;
  void validate() const {
    parent.validate();
    obcx::bot::detail::validate_identifier(name, "topic name", 128);
  }
};
struct TopicReceipt {
  obcx::bot::ConversationRef conversation;
  std::string name;
  void validate() const {
    conversation.validate();
    obcx::bot::detail::validate_identifier(name, "topic name", 128);
  }
};
// Owning-platform relation check; no native address components escape to
// actors.
void validate_topic_result(const CreateTopicRequest &, const TopicReceipt &);
} // namespace obcx::telegram::bot
namespace obcx::bot {
OBCX_DOMAIN_FIELDS(telegram::bot::SendStickerRequest, OBCX_DOMAIN_FIELD(target),
                   OBCX_DOMAIN_FIELD(sticker), OBCX_DOMAIN_FIELD(reply_to),
                   OBCX_DOMAIN_FIELD(maximum_bytes));
template <>
struct OperationTraits<telegram::bot::SendStickerRequest>
    : OperationContract<telegram::bot::SendStickerRequest, SendOutcome, true> {
  inline static constexpr bool requires_outcome_client = true;
  static bool supports_surface(const SurfaceId &s) {
    return s == SurfaceId{"telegram.bot_api"};
  }
  static const BotInstallationRef &installation(const request_type &r) {
    return r.target.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    telegram::bot::validate_sticker_outcome(r, v);
  }
};
OBCX_DOMAIN_FIELDS(telegram::bot::EditTextRequest, OBCX_DOMAIN_FIELD(message),
                   OBCX_DOMAIN_FIELD(text));
OBCX_DOMAIN_FIELDS(telegram::bot::EditTextReceipt, OBCX_DOMAIN_FIELD(message));
OBCX_DOMAIN_FIELDS(telegram::bot::StrikethroughText,
                   OBCX_DOMAIN_FIELD(content));
OBCX_DOMAIN_FIELDS(telegram::bot::EditFormattedTextRequest,
                   OBCX_DOMAIN_FIELD(message), OBCX_DOMAIN_FIELD(text));
template <>
struct detail::DomainEncodePreflight<telegram::bot::EditFormattedTextRequest> {
  static void validate(const telegram::bot::EditFormattedTextRequest &value) {
    detail::DomainWireBudget budget;
    detail::inspect_send_wire_value(value, budget);
  }
};
template <>
struct OperationTraits<telegram::bot::EditFormattedTextRequest>
    : OperationContract<telegram::bot::EditFormattedTextRequest,
                        telegram::bot::EditTextReceipt, true> {
  static bool supports_surface(const SurfaceId &s) {
    return s == SurfaceId{"telegram.bot_api"};
  }
  static const BotInstallationRef &installation(const request_type &r) {
    return r.message.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.message != r.message)
      throw std::invalid_argument("formatted edit receipt scope mismatch");
  }
};
OBCX_DOMAIN_FIELDS(telegram::bot::CreateTopicRequest, OBCX_DOMAIN_FIELD(parent),
                   OBCX_DOMAIN_FIELD(name));
OBCX_DOMAIN_FIELDS(telegram::bot::TopicReceipt, OBCX_DOMAIN_FIELD(conversation),
                   OBCX_DOMAIN_FIELD(name));
template <>
struct OperationTraits<telegram::bot::EditTextRequest>
    : OperationContract<telegram::bot::EditTextRequest,
                        telegram::bot::EditTextReceipt, true> {
  static bool supports_surface(const SurfaceId &s) {
    return s == SurfaceId{"telegram.bot_api"};
  }
  static const BotInstallationRef &installation(const request_type &r) {
    return r.message.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.message != r.message)
      throw std::invalid_argument("edit receipt scope mismatch");
  }
};
template <>
struct OperationTraits<telegram::bot::CreateTopicRequest>
    : OperationContract<telegram::bot::CreateTopicRequest,
                        telegram::bot::TopicReceipt, true> {
  static bool supports_surface(const SurfaceId &s) {
    return s == SurfaceId{"telegram.bot_api"};
  }
  static const BotInstallationRef &installation(const request_type &r) {
    return r.parent.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    telegram::bot::validate_topic_result(r, v);
  }
};
} // namespace obcx::bot
