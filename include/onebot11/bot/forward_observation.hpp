#pragma once

#include "onebot11/bot/special_content.hpp"

namespace obcx::onebot11::bot {

// An observation within one get_forward_msg response, not a standalone node
// MessageRef, author, reply relation or new media capability.
struct ForwardFaceObservation {
  std::size_t node_index;
  std::size_t normalized_block_index;
  FaceView face;
};

// Separate result type preserves the existing ReceivedForwardNode and
// OneBotForwardMessage layouts/codecs. Common media references come from this
// same lookup, never a second query paired with unrelated presentation data.
struct ObservedForwardMessage {
  OneBotForwardMessage message;
  std::vector<ForwardFaceObservation> faces;

  void validate() const {
    message.validate();
    if (faces.size() > obcx::bot::detail::domain_maximum_items)
      throw std::invalid_argument("too many forward face observations");
    std::optional<std::pair<std::size_t, std::size_t>> previous;
    for (const auto &entry : faces) {
      entry.face.validate();
      if (!std::ranges::all_of(entry.face.id,
                               [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("invalid forward face ID");
      const auto position =
          std::pair{entry.node_index, entry.normalized_block_index};
      if ((previous && position <= *previous) ||
          entry.node_index >= message.nodes.size())
        throw std::invalid_argument("invalid forward face position");
      const auto &body = message.nodes[entry.node_index].body;
      if (entry.normalized_block_index >= body.size())
        throw std::invalid_argument("forward face block is unavailable");
      const auto *block = std::get_if<obcx::bot::UnsupportedContent>(
          &body[entry.normalized_block_index]);
      if (!block || block->category != "onebot11.face")
        throw std::invalid_argument("forward face projection mismatch");
      previous = position;
    }
  }
};

struct ObserveOneBotForwardMessageRequest {
  inline static const obcx::bot::ActionId action{"onebot11.forward.observe"};
  QqForwardRef forward;
  void validate() const { forward.validate(); }
};
} // namespace obcx::onebot11::bot

namespace obcx::bot {
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ForwardFaceObservation> {
  static void validate(const Json &d) {
    (void)detail::bounded_unsigned(d.at("node_index"), 98);
    (void)detail::bounded_unsigned(d.at("normalized_block_index"),
                                   detail::domain_maximum_items - 1);
  }
};
template <>
struct detail::DomainRecordPreflight<onebot11::bot::ObservedForwardMessage> {
  static void validate(const Json &d) {
    onebot11::bot::detail::preflight_array(d, "faces",
                                           detail::domain_maximum_items, true);
  }
};
OBCX_DOMAIN_FIELDS(onebot11::bot::ForwardFaceObservation,
                   OBCX_DOMAIN_FIELD(node_index),
                   OBCX_DOMAIN_FIELD(normalized_block_index),
                   OBCX_DOMAIN_FIELD(face));
OBCX_DOMAIN_FIELDS(onebot11::bot::ObservedForwardMessage,
                   OBCX_DOMAIN_FIELD(message), OBCX_DOMAIN_FIELD(faces));
OBCX_DOMAIN_FIELDS(onebot11::bot::ObserveOneBotForwardMessageRequest,
                   OBCX_DOMAIN_FIELD(forward));
template <>
struct OperationTraits<onebot11::bot::ObserveOneBotForwardMessageRequest>
    : OperationContract<onebot11::bot::ObserveOneBotForwardMessageRequest,
                        onebot11::bot::ObservedForwardMessage, false> {
  static bool supports_surface(const SurfaceId &s) {
    return s == onebot11::bot::surface;
  }
  static auto installation(const request_type &r)
      -> const BotInstallationRef & {
    return r.forward.source.conversation.installation;
  }
  static void validate_result(const request_type &r, const result_type &v) {
    v.validate();
    if (v.message.forward != r.forward)
      throw std::invalid_argument("forward observation scope mismatch");
  }
};
} // namespace obcx::bot
