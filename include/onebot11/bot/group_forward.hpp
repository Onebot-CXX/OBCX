#pragma once
#include "core/bot/send_gateway_codec.hpp"
#include "onebot11/bot/forward_preflight.hpp"
#include "onebot11/bot/types.hpp"
#include <span>
namespace obcx::onebot11::bot {
struct QqForwardRef {
  MessageRef source;
  std::string token;
  void validate() const {
    source.validate();
    detail::require_onebot(source.conversation.installation, "QQ forward");
    detail::validate_identifier(token, "QQ forward token", 512);
    // This is the provider's opaque resource ID, not our reference codec or
    // a URL-safe token. Preserve punctuation (including +, / and =) verbatim;
    // the native request serializer owns JSON escaping, not ID normalization.
    obcx::bot::detail::validate_utf8(token);
  }
  auto operator==(const QqForwardRef &) const -> bool = default;
};
struct ForwardNode {
  UserRef author;
  std::string display_name;
  obcx::bot::MessageBody body;
  void validate() const {
    author.validate();
    detail::require_onebot(author.installation, "QQ forward author");
    detail::validate_identifier(display_name, "QQ forward display name", 1024);
    if (body.empty() || body.size() > obcx::bot::detail::domain_maximum_items)
      throw std::invalid_argument("empty/oversized QQ forward body");
  }
};
// Explicit nonstandard provider behavior retained for existing generated
// forwards. Omits author/name on the wire; never claims cached-self or caller
// authorship. Unsupported providers return their normal operation failure.
// Standard authored nodes and the standard SDK composition API are unchanged.
struct AuthorlessForwardNode {
  obcx::bot::MessageBody body;
  void validate() const {
    if (body.empty() || body.size() > obcx::bot::detail::domain_maximum_items)
      throw std::invalid_argument("empty/oversized QQ forward body");
  }
};
using ForwardNodeValue =
    std::variant<ForwardNode, MessageRef, AuthorlessForwardNode>;

// Pure preparation check for callers that will send each node independently.
// Retains one native forward-envelope ceiling without treating all nodes as one
// media operation. No submission, I/O, identity creation or retry authority.
void validate_independent_forward_plan(
    const ConversationRef &target, std::span<const ForwardNodeValue> nodes,
    std::size_t maximum_envelope_bytes,
    const obcx::bot::SendLimits &per_message_limits);

struct SendOneBotGroupForwardMessageRequest {
  inline static const obcx::bot::ActionId action{"onebot11.group_forward.send"};
  ConversationRef target;
  // Append alternatives; keep existing encoded tags stable.
  std::vector<ForwardNodeValue> nodes;
  std::size_t maximum_envelope_bytes;
  obcx::bot::SendLimits limits;
  void validate() const {
    target.validate();
    detail::require_onebot(target.installation, "QQ forward target");
    limits.validate();
    if (nodes.empty() || nodes.size() > 99 || !maximum_envelope_bytes ||
        maximum_envelope_bytes > obcx::bot::detail::domain_maximum_bytes)
      throw std::invalid_argument("QQ forward node/envelope limit exceeded");
    detail::ForwardPreflight envelope(target, limits, maximum_envelope_bytes,
                                      detail::ForwardBudgetScope::WholeSend);
    std::size_t cumulative_known_bytes = 0;
    for (const auto &node : nodes)
      std::visit(
          [&](const auto &v) {
            v.validate();
            if constexpr (std::same_as<std::remove_cvref_t<decltype(v)>,
                                       ForwardNode>) {
              if (v.author.installation != target.installation)
                throw std::invalid_argument("QQ forward author scope mismatch");
              obcx::bot::validate_message_body(target, v.body, limits,
                                               cumulative_known_bytes);
              envelope.node(v.author, v.display_name, v.body);
            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(v)>,
                                              AuthorlessForwardNode>) {
              obcx::bot::validate_message_body(target, v.body, limits,
                                               cumulative_known_bytes);
              envelope.authorless_node(v.body);
            } else {
              throw std::invalid_argument("opaque QQ forward reference nodes "
                                          "have no bounded installed recipe");
            }
          },
          node);
  }
};
struct ForwardOutputDetails {
  MessageRef message;
  std::optional<std::vector<std::size_t>> node_indices;
  void validate() const {
    message.validate();
    if (node_indices && (node_indices->empty() || node_indices->size() > 99))
      throw std::invalid_argument("invalid QQ node provenance");
    if (node_indices)
      for (std::size_t i = 0; i < node_indices->size(); ++i)
        if ((*node_indices)[i] >= 99 ||
            (i && (*node_indices)[i - 1] >= (*node_indices)[i]))
          throw std::invalid_argument("invalid QQ node ordering");
  }
};
struct OneBotGroupForwardMessageResult {
  obcx::bot::SendOutcome outcome;
  std::optional<QqForwardRef> forward;
  std::vector<ForwardOutputDetails> output_details;
  void validate() const {
    std::visit([](const auto &value) { value.validate(); }, outcome);
    if (forward)
      forward->validate();
    for (const auto &v : output_details)
      v.validate();
  }
};
struct ReceivedForwardNode {
  obcx::bot::Sender sender;
  std::optional<std::string> display_name;
  std::optional<obcx::bot::TimePoint> sent_at;
  obcx::bot::ReceivedBody body;
};
struct GetOneBotForwardMessageRequest {
  inline static const auto &action = actions::get_forward_message;
  QqForwardRef forward;
  void validate() const { forward.validate(); }
};
struct OneBotForwardMessage {
  QqForwardRef forward;
  std::vector<ReceivedForwardNode> nodes;
  void validate() const {
    forward.validate();
    if (nodes.size() > 99)
      throw std::invalid_argument("QQ forward exceeds node limit");
    const auto &installation = forward.source.conversation.installation;
    for (const auto &node : nodes) {
      if (node.display_name) {
        // An observed empty nickname is not a missing nickname or an author.
        if (!node.display_name->empty())
          detail::validate_identifier(*node.display_name,
                                      "QQ forward sender name", 2048);
        obcx::bot::detail::validate_utf8(*node.display_name);
      }
      std::visit(
          [&](const auto &sender) {
            using T = std::remove_cvref_t<decltype(sender)>;
            if constexpr (std::same_as<T, obcx::bot::KnownUser>) {
              sender.validate();
              if (sender.user.installation != installation)
                throw std::invalid_argument("QQ forward sender scope mismatch");
            } else if constexpr (std::same_as<T,
                                              obcx::bot::ConversationSender>) {
              sender.validate();
              if (sender.conversation.installation != installation)
                throw std::invalid_argument("QQ forward sender scope mismatch");
            }
          },
          node.sender);
      for (const auto &block : node.body)
        std::visit(
            [&](const auto &value) {
              using T = std::remove_cvref_t<decltype(value)>;
              if constexpr (std::same_as<T, obcx::bot::ReceivedRichText>)
                obcx::bot::validate_received_text(value, installation);
              else {
                value.validate();
                if constexpr (std::same_as<T, obcx::bot::ReceivedMedia>) {
                  if (value.media.installation != installation)
                    throw std::invalid_argument(
                        "QQ forward media scope mismatch");
                  obcx::bot::validate_received_text(value.caption,
                                                    installation);
                }
              }
            },
            block);
    }
  }
};
} // namespace obcx::onebot11::bot
namespace obcx::bot {
template <> struct detail::DomainRecordPreflight<onebot11::bot::QqForwardRef> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "token", 512, false);
  }
};
template <> struct detail::DomainRecordPreflight<onebot11::bot::ForwardNode> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_text(d, "display_name", 1024, false);
    onebot11::bot::detail::preflight_array(d, "body",
                                           detail::domain_maximum_items, false);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::AuthorlessForwardNode> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_array(d, "body",
                                           detail::domain_maximum_items, false);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ForwardOutputDetails> {
  static void validate(const Json &d) {
    if (!d.at("node_indices").is_null())
      onebot11::bot::detail::preflight_array(d, "node_indices", 99, false);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ReceivedForwardNode> {
  static void validate(const Json &d) {
    if (!d.at("display_name").is_null())
      onebot11::bot::detail::preflight_text(d, "display_name", 2048, true);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::OneBotForwardMessage> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_array(d, "nodes", 99, true);
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::QqForwardRef, OBCX_DOMAIN_FIELD(source),
                   OBCX_DOMAIN_FIELD(token));
OBCX_DOMAIN_FIELDS(onebot11::bot::ForwardNode, OBCX_DOMAIN_FIELD(author),
                   OBCX_DOMAIN_FIELD(display_name), OBCX_DOMAIN_FIELD(body));
OBCX_DOMAIN_FIELDS(onebot11::bot::AuthorlessForwardNode,
                   OBCX_DOMAIN_FIELD(body));
OBCX_DOMAIN_FIELDS(onebot11::bot::SendOneBotGroupForwardMessageRequest,
                   OBCX_DOMAIN_FIELD(target), OBCX_DOMAIN_FIELD(nodes),
                   OBCX_DOMAIN_FIELD(maximum_envelope_bytes),
                   OBCX_DOMAIN_FIELD(limits));
OBCX_DOMAIN_FIELDS(onebot11::bot::ForwardOutputDetails,
                   OBCX_DOMAIN_FIELD(message), OBCX_DOMAIN_FIELD(node_indices));
OBCX_DOMAIN_FIELDS(onebot11::bot::OneBotGroupForwardMessageResult,
                   OBCX_DOMAIN_FIELD(outcome), OBCX_DOMAIN_FIELD(forward),
                   OBCX_DOMAIN_FIELD(output_details));
OBCX_DOMAIN_FIELDS(onebot11::bot::ReceivedForwardNode,
                   OBCX_DOMAIN_FIELD(sender), OBCX_DOMAIN_FIELD(display_name),
                   OBCX_DOMAIN_FIELD(sent_at), OBCX_DOMAIN_FIELD(body));
OBCX_DOMAIN_FIELDS(onebot11::bot::GetOneBotForwardMessageRequest,
                   OBCX_DOMAIN_FIELD(forward));
OBCX_DOMAIN_FIELDS(onebot11::bot::OneBotForwardMessage,
                   OBCX_DOMAIN_FIELD(forward), OBCX_DOMAIN_FIELD(nodes));
template <>
struct detail::DomainRecordPreflight<
    onebot11::bot::SendOneBotGroupForwardMessageRequest> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_array(d, "nodes", 99, false);
    const auto limits = detail::domain_decode<SendLimits>(d.at("limits"));
    const auto maximum = detail::bounded_unsigned(
        d.at("maximum_envelope_bytes"), detail::domain_maximum_bytes);
    const auto target = d.at("target").get<ConversationRef>();
    onebot11::bot::detail::ForwardPreflight preflight(
        target, limits, maximum,
        onebot11::bot::detail::ForwardBudgetScope::WholeSend);
    for (const auto &node : d.at("nodes")) {
      const auto alternative =
          detail::bounded_unsigned(node.at("alternative"), 2);
      if (alternative == 1)
        throw std::invalid_argument(
            "QQ opaque forward nodes have no bounded recipe");
      const auto &value = node.at("value");
      if (alternative == 0) {
        detail::DomainRecordPreflight<onebot11::bot::ForwardNode>::validate(
            value);
        preflight.node(value);
      } else {
        detail::DomainRecordPreflight<
            onebot11::bot::AuthorlessForwardNode>::validate(value);
        preflight.authorless_node(value);
      }
    }
  }
};
template <>
struct OperationTraits<onebot11::bot::SendOneBotGroupForwardMessageRequest>
    : OperationContract<onebot11::bot::SendOneBotGroupForwardMessageRequest,
                        onebot11::bot::OneBotGroupForwardMessageResult, true> {
  static constexpr bool requires_outcome_client = true;
  static auto normalize_result(const request_type &, result_type)
      -> result_type;
  static auto decode_result(const request_type &, const Json &) -> result_type;
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.target.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    detail::validate_single_message_outcome(r.target, v.outcome);
    v.validate();
    const auto &details = std::visit(
        [](const auto &o) -> const std::vector<SendOutputDetails> & {
          return o.output_details;
        },
        v.outcome);
    std::set<std::string_view> confirmed_ids;
    for (const auto &output : details)
      confirmed_ids.insert(output.message.id.serialized());
    std::set<std::string_view> seen;
    for (const auto &entry : v.output_details) {
      if (entry.message.conversation != r.target ||
          !seen.insert(entry.message.id.serialized()).second ||
          !confirmed_ids.contains(entry.message.id.serialized()))
        throw std::invalid_argument(
            "QQ forward auxiliary output is not uniquely confirmed");
      if (entry.node_indices)
        for (auto index : *entry.node_indices)
          if (index >= r.nodes.size())
            throw std::invalid_argument("QQ forward node index mismatch");
    }
    for (const auto &output : details) {
      if (output.message.conversation != r.target || output.content)
        throw std::invalid_argument(
            "QQ forward output scope/provenance mismatch");
      for (const auto &media : output.reusable_media)
        if (media.block_index)
          throw std::invalid_argument(
              "QQ forward has no common body associations");
    }
    if (v.forward &&
        (v.forward->source.conversation != r.target ||
         !confirmed_ids.contains(v.forward->source.id.serialized())))
      throw std::invalid_argument("QQ forward resource scope mismatch");
  }
};
template <>
struct OperationTraits<onebot11::bot::GetOneBotForwardMessageRequest>
    : OperationContract<onebot11::bot::GetOneBotForwardMessageRequest,
                        onebot11::bot::OneBotForwardMessage, false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.forward.source.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.forward != r.forward)
      throw std::invalid_argument("QQ forward result scope mismatch");
  }
};
} // namespace obcx::bot

#include "onebot11/bot/forward_outcome.hpp"
