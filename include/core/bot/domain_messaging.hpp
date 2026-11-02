#pragma once
#include "core/bot/domain_media.hpp"
#include "core/bot/operation_result.hpp"
#include "core/bot/operation_traits.hpp"
#include <algorithm>
#include <set>

namespace obcx::bot {
enum class TextStyle : std::uint8_t { Bold, Italic, Code };
struct Text {
  std::string content;
  std::vector<TextStyle> styles;
  void validate() const {
    if (content.empty() || content.size() > detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid text length");
    detail::validate_utf8(content);
    std::vector<TextStyle> seen;
    for (auto style : styles) {
      if (style > TextStyle::Code ||
          std::ranges::find(seen, style) != seen.end())
        throw std::invalid_argument("invalid or duplicate text style");
      seen.push_back(style);
    }
  }
};
struct Mention {
  UserRef user;
  void validate() const { user.validate(); }
};
using RichText = std::vector<std::variant<Text, Mention>>;
inline void validate_rich_text(const RichText &text,
                               const BotInstallationRef &installation) {
  if (text.size() > detail::domain_maximum_items)
    throw std::invalid_argument("rich text exceeds bound");
  for (const auto &item : text)
    std::visit(
        [&](const auto &v) {
          v.validate();
          if constexpr (std::same_as<std::decay_t<decltype(v)>, Mention>) {
            if (v.user.installation != installation)
              throw std::invalid_argument("mention installation mismatch");
          }
        },
        item);
}
inline void validate_attachment(const MediaSource &source,
                                const RichText &caption) {
  std::visit([](const auto &v) { v.validate(); }, source);
  for (const auto &span : caption)
    std::visit([](const auto &v) { v.validate(); }, span);
}
struct Image {
  MediaSource source;
  RichText caption;
  void validate() const { validate_attachment(source, caption); }
};
struct Animation {
  MediaSource source;
  RichText caption;
  void validate() const { validate_attachment(source, caption); }
};
struct Video {
  MediaSource source;
  RichText caption;
  void validate() const { validate_attachment(source, caption); }
};
enum class AudioKind : std::uint8_t { Audio, Voice };
struct Audio {
  MediaSource source;
  AudioKind kind;
  RichText caption;
  void validate() const {
    validate_attachment(source, caption);
    if (kind > AudioKind::Voice)
      throw std::invalid_argument("invalid audio kind");
  }
};
struct File {
  MediaSource source;
  std::string filename;
  RichText caption;
  void validate() const {
    validate_attachment(source, caption);
    detail::validate_identifier(filename, "filename", 1024);
  }
};
using MessageBlock =
    std::variant<RichText, Image, Animation, Video, Audio, File>;
using MessageBody = std::vector<MessageBlock>;
enum class DeliveryPolicy : std::uint8_t { SingleMessageOnly, AllowSplit };
struct SendLimits {
  std::size_t maximum_messages;
  std::size_t maximum_media_item_bytes;
  std::size_t maximum_media_total_bytes;
  void validate() const {
    if (!maximum_messages || maximum_messages > detail::domain_maximum_items ||
        maximum_media_item_bytes > maximum_media_total_bytes ||
        maximum_media_total_bytes > detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid explicit send limits");
  }
};
// Borrows the body so callers can account for several bodies without copying
// media. A failure may leave the supplied total charged for a validated prefix.
inline void validate_message_body(const ConversationRef &target,
                                  const MessageBody &body,
                                  const SendLimits &limits,
                                  std::size_t &known_media_bytes) {
  target.validate();
  limits.validate();
  if (body.empty() || body.size() > detail::domain_maximum_items)
    throw std::invalid_argument("invalid message body");
  if (known_media_bytes > limits.maximum_media_total_bytes)
    throw std::invalid_argument("known media exceeds explicit total budget");
  for (const auto &block : body)
    std::visit(
        [&](const auto &v) {
          using V = std::decay_t<decltype(v)>;
          if constexpr (std::same_as<V, RichText>) {
            if (v.empty())
              throw std::invalid_argument("empty text block");
            validate_rich_text(v, target.installation);
          } else {
            validate_media_source(v.source, target.installation);
            validate_rich_text(v.caption, target.installation);
            if (!limits.maximum_media_item_bytes)
              throw std::invalid_argument(
                  "media requires positive byte budgets");
            if constexpr (std::same_as<V, Audio>) {
              if (v.kind > AudioKind::Voice)
                throw std::invalid_argument("invalid audio kind");
            }
            if constexpr (std::same_as<V, File>)
              detail::validate_identifier(v.filename, "filename", 1024);
            std::visit(
                [&](const auto &source) {
                  using S = std::decay_t<decltype(source)>;
                  std::optional<std::uint64_t> size;
                  if constexpr (std::same_as<S, BytesSource>)
                    size = source.bytes.size();
                  else if constexpr (!std::same_as<S, MediaRef>)
                    size = source.metadata.size_bytes;
                  if (size) {
                    if (*size > limits.maximum_media_item_bytes ||
                        *size > limits.maximum_media_total_bytes -
                                    known_media_bytes)
                      throw std::invalid_argument(
                          "media exceeds explicit budget");
                    known_media_bytes += *size;
                  }
                },
                v.source);
          }
        },
        block);
}
struct SendMessageRequest {
  inline static const ActionId action{"message.send"};
  ConversationRef target;
  MessageBody body;
  std::optional<MessageRef> reply_to;
  DeliveryPolicy delivery;
  SendLimits limits;
  void validate() const {
    target.validate();
    limits.validate();
    if (body.empty() || body.size() > detail::domain_maximum_items ||
        delivery > DeliveryPolicy::AllowSplit)
      throw std::invalid_argument("invalid message body or delivery policy");
    if (reply_to) {
      reply_to->validate();
      if (reply_to->conversation != target)
        throw std::invalid_argument("reply target scope mismatch");
    }
    std::size_t total = 0;
    validate_message_body(target, body, limits, total);
  }
};
struct DeleteMessageRequest {
  inline static const ActionId action{"message.delete"};
  MessageRef message;
  void validate() const { message.validate(); }
};
struct DeleteReceipt {
  MessageRef message;
  void validate() const { message.validate(); }
};
// UTF-8 byte ranges into one Text inline, never provider entity offsets.
// For an attachment the inline is in its caption. Null text means whole block.
struct TextRange {
  std::size_t inline_index;
  std::size_t utf8_offset;
  std::size_t utf8_length;
};
struct ContentSlice {
  std::size_t block_index;
  std::optional<TextRange> text;
};
struct PublishedMedia {
  MediaRef media;
  std::optional<MediaIdentity> identity;
  std::optional<std::size_t> block_index;
  void validate() const {
    media.validate();
    if (identity) {
      identity->validate();
      if (identity->installation != media.installation)
        throw std::invalid_argument("media identity scope mismatch");
    }
  }
};
struct SendOutputDetails {
  MessageRef message;
  std::optional<std::vector<ContentSlice>> content;
  std::vector<PublishedMedia> reusable_media;
  void validate() const {
    message.validate();
    if (content && content->empty())
      throw std::invalid_argument("known output content cannot be empty");
    for (const auto &media : reusable_media) {
      media.validate();
      if (media.media.installation != message.conversation.installation)
        throw std::invalid_argument("published media scope mismatch");
    }
  }
};
inline void validate_output_details(
    const std::vector<MessageRef> &messages,
    const std::vector<SendOutputDetails> &details) {
  if (messages.size() != details.size())
    throw std::invalid_argument(
        "output details must correspond to every confirmed reference");
  detail::DomainWireBudget work;
  work.nodes(messages.size());
  work.nodes(details.size());
  std::set<MessageId> seen;
  for (std::size_t i = 0; i < messages.size(); ++i) {
    messages[i].validate();
    details[i].message.validate();
    work.inspect(Json(messages[i]));
    work.inspect(Json(details[i].message));
    if (details[i].content)
      work.nodes(details[i].content->size());
    work.nodes(details[i].reusable_media.size());
    for (const auto &media : details[i].reusable_media) {
      media.validate();
      work.inspect(detail::domain_encode(media.media));
      if (media.identity)
        work.inspect(detail::domain_encode(*media.identity));
    }
    details[i].validate();
    if (messages[i] != details[i].message ||
        messages[i].conversation != messages.front().conversation ||
        !seen.insert(messages[i].id).second)
      throw std::invalid_argument("invalid output reference sequence");
  }
}
struct SendReceipt {
  std::vector<MessageRef> messages;
  std::vector<SendOutputDetails> output_details;
  void validate() const {
    if (messages.empty())
      throw std::invalid_argument("send receipt cannot be empty");
    validate_output_details(messages, output_details);
  }
};
struct SendFailure {
  BotOperationError error;
  std::vector<MessageRef> confirmed_messages;
  std::vector<SendOutputDetails> output_details;
  bool may_have_unconfirmed_messages;
  void validate() const {
    error.validate();
    validate_output_details(confirmed_messages, output_details);
    if ((!confirmed_messages.empty() || may_have_unconfirmed_messages) &&
        (error.submission_safety != SubmissionSafety::PossiblySubmitted ||
         error.retryable))
      throw std::invalid_argument(
          "partial or unknown send cannot be retried as a whole");
    if (confirmed_messages.empty() &&
        error.submission_safety == SubmissionSafety::PossiblySubmitted &&
        !may_have_unconfirmed_messages)
      throw std::invalid_argument(
          "unconfirmed possible send must mark uncertainty");
  }
};
using SendOutcome = std::variant<SendReceipt, SendFailure>;
inline auto send_failure(BotOperationError error,
                         std::vector<SendOutputDetails> confirmed, bool unknown)
    -> SendOutcome {
  std::vector<MessageRef> messages;
  for (const auto &detail : confirmed)
    messages.push_back(detail.message);
  unknown = unknown ||
            (messages.empty() &&
             error.submission_safety == SubmissionSafety::PossiblySubmitted);
  if (!messages.empty() || unknown) {
    error.submission_safety = SubmissionSafety::PossiblySubmitted;
    error.retryable = false;
  }
  SendFailure result{std::move(error), std::move(messages),
                     std::move(confirmed), unknown};
  result.validate();
  return result;
}
namespace detail {
inline auto content_slice_text(const ContentSlice &slice,
                               const SendMessageRequest &request)
    -> std::optional<std::string_view> {
  if (slice.block_index >= request.body.size())
    throw std::invalid_argument("output block association out of range");
  if (!slice.text)
    return std::nullopt;
  const RichText *text = std::visit(
      [](const auto &block) -> const RichText * {
        if constexpr (std::same_as<std::decay_t<decltype(block)>, RichText>)
          return &block;
        else
          return &block.caption;
      },
      request.body[slice.block_index]);
  const auto &range = *slice.text;
  if (range.inline_index >= text->size())
    throw std::invalid_argument("output text association out of range");
  const auto *span = std::get_if<Text>(&(*text)[range.inline_index]);
  if (!span || !range.utf8_length || range.utf8_offset > span->content.size() ||
      range.utf8_length > span->content.size() - range.utf8_offset)
    throw std::invalid_argument("invalid output text association");
  return std::string_view(span->content)
      .substr(range.utf8_offset, range.utf8_length);
}
} // namespace detail
inline void validate_content_slice(const ContentSlice &slice,
                                   const SendMessageRequest &request) {
  if (const auto text = detail::content_slice_text(slice, request))
    detail::validate_utf8(*text);
}
inline void validate_send_outcome(const SendMessageRequest &request,
                                  const SendOutcome &outcome) {
  std::visit(
      [&](const auto &value) {
        value.validate();
        detail::DomainWireBudget content_work;
        for (const auto &detail : value.output_details) {
          if (detail.message.conversation != request.target)
            throw std::invalid_argument("send result target mismatch");
          if (detail.content)
            for (const auto &slice : *detail.content) {
              content_work.nodes();
              if (const auto text =
                      detail::content_slice_text(slice, request)) {
                content_work.bytes(text->size());
                detail::validate_utf8(*text);
              }
            }
          for (const auto &media : detail.reusable_media)
            if (media.block_index) {
              if (*media.block_index >= request.body.size() ||
                  std::holds_alternative<RichText>(
                      request.body[*media.block_index]))
                throw std::invalid_argument(
                    "invalid published media association");
            }
        }
        if constexpr (std::same_as<std::decay_t<decltype(value)>,
                                   SendReceipt>) {
          if (value.messages.size() > request.limits.maximum_messages ||
              (request.delivery == DeliveryPolicy::SingleMessageOnly &&
               value.messages.size() != 1))
            throw std::invalid_argument(
                "send receipt violates delivery bounds");
        }
      },
      outcome);
}
template <> struct detail::DomainRecordPreflight<File> {
  static void validate(const Json &d) {
    (void)detail::require_identifier(d, "filename", "File", 1024);
  }
};

OBCX_DOMAIN_FIELDS(Text, OBCX_DOMAIN_FIELD(content), OBCX_DOMAIN_FIELD(styles));
OBCX_DOMAIN_FIELDS(Mention, OBCX_DOMAIN_FIELD(user));
OBCX_DOMAIN_FIELDS(Image, OBCX_DOMAIN_FIELD(source),
                   OBCX_DOMAIN_FIELD(caption));
OBCX_DOMAIN_FIELDS(Animation, OBCX_DOMAIN_FIELD(source),
                   OBCX_DOMAIN_FIELD(caption));
OBCX_DOMAIN_FIELDS(Video, OBCX_DOMAIN_FIELD(source),
                   OBCX_DOMAIN_FIELD(caption));
OBCX_DOMAIN_FIELDS(Audio, OBCX_DOMAIN_FIELD(source), OBCX_DOMAIN_FIELD(kind),
                   OBCX_DOMAIN_FIELD(caption));
OBCX_DOMAIN_FIELDS(File, OBCX_DOMAIN_FIELD(source), OBCX_DOMAIN_FIELD(filename),
                   OBCX_DOMAIN_FIELD(caption));
OBCX_DOMAIN_FIELDS(SendLimits, OBCX_DOMAIN_FIELD(maximum_messages),
                   OBCX_DOMAIN_FIELD(maximum_media_item_bytes),
                   OBCX_DOMAIN_FIELD(maximum_media_total_bytes));
OBCX_DOMAIN_FIELDS(SendMessageRequest, OBCX_DOMAIN_FIELD(target),
                   OBCX_DOMAIN_FIELD(body), OBCX_DOMAIN_FIELD(reply_to),
                   OBCX_DOMAIN_FIELD(delivery), OBCX_DOMAIN_FIELD(limits));
OBCX_DOMAIN_FIELDS(DeleteMessageRequest, OBCX_DOMAIN_FIELD(message));
OBCX_DOMAIN_FIELDS(DeleteReceipt, OBCX_DOMAIN_FIELD(message));
OBCX_DOMAIN_FIELDS(TextRange, OBCX_DOMAIN_FIELD(inline_index),
                   OBCX_DOMAIN_FIELD(utf8_offset),
                   OBCX_DOMAIN_FIELD(utf8_length));
OBCX_DOMAIN_FIELDS(ContentSlice, OBCX_DOMAIN_FIELD(block_index),
                   OBCX_DOMAIN_FIELD(text));
OBCX_DOMAIN_FIELDS(PublishedMedia, OBCX_DOMAIN_FIELD(media),
                   OBCX_DOMAIN_FIELD(identity), OBCX_DOMAIN_FIELD(block_index));
OBCX_DOMAIN_FIELDS(SendOutputDetails, OBCX_DOMAIN_FIELD(message),
                   OBCX_DOMAIN_FIELD(content),
                   OBCX_DOMAIN_FIELD(reusable_media));
OBCX_DOMAIN_FIELDS(SendReceipt, OBCX_DOMAIN_FIELD(messages),
                   OBCX_DOMAIN_FIELD(output_details));
OBCX_DOMAIN_FIELDS(SendFailure, OBCX_DOMAIN_FIELD(error),
                   OBCX_DOMAIN_FIELD(confirmed_messages),
                   OBCX_DOMAIN_FIELD(output_details),
                   OBCX_DOMAIN_FIELD(may_have_unconfirmed_messages));

namespace detail {
// Mirrors domain_encode's wire shape without allocating a JSON tree or copying
// payloads. Custom reference encodings deliberately have no record schema key.
// New leaf types must be accounted explicitly rather than serialized to
// inspect.
template <class T>
void inspect_send_wire_value(const T &value, DomainWireBudget &budget,
                             unsigned depth = 0) {
  if (depth > 32)
    throw DomainBudgetExceeded{};
  if constexpr (IsOptional<T>::value) {
    if (value)
      inspect_send_wire_value(*value, budget, depth);
    else
      budget.nodes();
  } else {
    budget.nodes();
    [[maybe_unused]] const auto field = [&](std::string_view name,
                                            const auto &item) {
      budget.bytes(name.size());
      inspect_send_wire_value(item, budget, depth + 1);
    };
    if constexpr (DomainRecord<T>) {
      field("schema", 1U);
      std::apply(
          [&](auto... member) {
            (field(member.first, value.*member.second), ...);
          },
          DomainFields<T>::fields());
    } else if constexpr (std::same_as<T, std::vector<std::uint8_t>>) {
      budget.bytes(value.size());
    } else if constexpr (IsVector<T>::value) {
      for (const auto &item : value)
        inspect_send_wire_value(item, budget, depth + 1);
    } else if constexpr (IsVariant<T>::value) {
      field("alternative", value.index());
      std::visit([&](const auto &item) { field("value", item); }, value);
    } else if constexpr (std::same_as<T, std::string>) {
      budget.bytes(value.size());
    } else if constexpr (std::same_as<T, BotInstallationRef>) {
      field("installation_id", value.installation_id);
      field("surface", value.surface);
    } else if constexpr (std::same_as<T, ConversationRef> ||
                         std::same_as<T, UserRef>) {
      field("installation", value.installation);
      field("id", value.id);
    } else if constexpr (std::same_as<T, MessageRef>) {
      field("conversation", value.conversation);
      field("id", value.id);
    } else if constexpr (std::same_as<T, SurfaceId>) {
      budget.bytes(value.value().size());
    } else if constexpr (std::same_as<T, ConversationId> ||
                         std::same_as<T, UserId> ||
                         std::same_as<T, MessageId> ||
                         std::same_as<T, MediaId>) {
      budget.bytes(value.serialized().size());
    } else {
      static_assert(std::is_integral_v<T> || std::is_enum_v<T>,
                    "send wire budget must model every leaf encoding");
    }
  }
}
} // namespace detail

template <> struct detail::DomainEncodePreflight<SendMessageRequest> {
  static void validate(const SendMessageRequest &value) {
    detail::DomainWireBudget budget;
    detail::inspect_send_wire_value(value, budget);
  }
};

template <> struct detail::DomainDecodePreflight<SendMessageRequest> {
  static void validate(const Json &payload) {
    detail::validate_domain_wire(payload);
    const auto limits = detail::domain_decode<SendLimits>(payload.at("limits"));
    detail::validate_binary_budget(payload, limits.maximum_media_item_bytes,
                                   limits.maximum_media_total_bytes);
  }
};

// Nested records enter domain_decode directly, without a top-level gateway or
// ADL preflight. Run the identical bounded caller policy before member copies.
template <>
struct detail::DomainRecordPreflight<SendMessageRequest>
    : detail::DomainDecodePreflight<SendMessageRequest> {};

template <> struct GatewayCodec<SendMessageRequest> {
  static auto encode(SendMessageRequest &value) -> Json {
    value.validate();
    auto d = detail::domain_encode(value);
    detail::validate_binary_budget(d, value.limits.maximum_media_item_bytes,
                                   value.limits.maximum_media_total_bytes);
    return d;
  }
  static auto decode(const Json &payload) -> SendMessageRequest {
    detail::DomainDecodePreflight<SendMessageRequest>::validate(payload);
    return detail::domain_decode<SendMessageRequest>(payload);
  }
};

inline auto domain_supports_surface(const SurfaceId &surface) -> bool {
  return surface == SurfaceId{"onebot11.qq"} ||
         surface == SurfaceId{"telegram.bot_api"};
}
template <>
struct OperationTraits<SendMessageRequest>
    : OperationContract<SendMessageRequest, SendOutcome, true> {
  static constexpr bool requires_outcome_client = true;
  static auto supports_surface(const SurfaceId &s) -> bool {
    return domain_supports_surface(s);
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.target.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    validate_send_outcome(r, v);
  }
};
template <>
struct OperationTraits<DeleteMessageRequest>
    : OperationContract<DeleteMessageRequest, DeleteReceipt, true> {
  static auto supports_surface(const SurfaceId &s) -> bool {
    return domain_supports_surface(s);
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.message.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (r.message != v.message)
      throw std::invalid_argument("delete receipt scope mismatch");
  }
};
} // namespace obcx::bot
