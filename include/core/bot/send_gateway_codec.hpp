#pragma once
#include "core/bot/bot_messaging.hpp"
#include "core/bot/operation_gateway.hpp"
#include <map>
#include <memory>

namespace obcx::bot {
namespace detail {
inline auto unknown_send_error() -> BotOperationError {
  return {.code = BotOperationErrorCode::OutcomeUnknown,
          .message = "send did not produce a complete valid outcome",
          .provider_code = std::nullopt,
          .retry_after = std::nullopt,
          .retryable = false,
          .submission_safety = SubmissionSafety::PossiblySubmitted};
}
inline auto encode_send_outcome_value(const SendOutcome &value) -> Json {
  std::visit([](const auto &v) { v.validate(); }, value);
  Json encoded = {
      {"schema", 1},
      {"alternative", value.index()},
      {"value",
       std::visit([](const auto &v) { return bot_encode(v); }, value)}};
  validate_bot_wire(encoded);
  return encoded;
}
// Account in bounded fragments, before copying an entire typed outcome. The
// unknown form uses the same borrowed evidence, not a copied sidecar.
inline void inspect_send_outcome(BotWireBudget &budget,
                                 const SendOutcome &outcome, unsigned depth,
                                 bool standalone, bool as_unknown) {
  std::visit(
      [&](const auto &value) {
        constexpr bool receipt =
            std::same_as<std::decay_t<decltype(value)>, SendReceipt>;
        const bool failure = as_unknown || !receipt;
        Json record = {
            {"schema", 1},
            {failure ? "confirmed_messages" : "messages", Json::array()},
            {"output_details", Json::array()}};
        if (failure) {
          if constexpr (receipt) {
            record["error"] = bot_encode(unknown_send_error());
            record["may_have_unconfirmed_messages"] = true;
          } else {
            value.error.validate();
            record["error"] =
                bot_encode(as_unknown ? unknown_send_error() : value.error);
            record["may_have_unconfirmed_messages"] =
                as_unknown || value.may_have_unconfirmed_messages;
          }
        }
        Json header = {{"alternative", failure ? 1 : 0},
                       {"value", std::move(record)}};
        if (standalone)
          header["schema"] = 1;
        budget.inspect(header, depth);
        const auto &messages = [&]() -> const std::vector<MessageRef> & {
          if constexpr (receipt)
            return value.messages;
          else
            return value.confirmed_messages;
        }();
        for (const auto &message : messages) {
          message.validate();
          budget.inspect(Json(message), depth + 3);
        }
        for (const auto &output : value.output_details) {
          output.message.validate();
          budget.inspect(
              Json{{"schema", 1},
                   {"message", Json(output.message)},
                   {"content", output.content ? Json::array() : Json(nullptr)},
                   {"reusable_media", Json::array()}},
              depth + 3);
          if (output.content)
            for (const auto &slice : *output.content)
              budget.inspect(bot_encode(slice), depth + 5);
          for (const auto &media : output.reusable_media) {
            media.validate();
            budget.inspect(bot_encode(media), depth + 5);
          }
        }
      },
      outcome);
}
class SendEvidenceRecovery;
} // namespace detail

enum class SendOutcomeWireFormat { Standalone, NestedVariant };
class SendOutcomeEnvelopeBudget;

// Construct from the actual required result skeleton, replacing only its
// outcome with null. The factory derives costs/depth; callers supply no
// credits.
class SendOutcomeEnvelope {
public:
  static auto standalone() -> SendOutcomeEnvelope {
    return SendOutcomeEnvelope(nullptr);
  }
  static auto from_skeleton(const Json &required_wrapper,
                            const Json &null_outcome_slot,
                            SendOutcomeWireFormat format)
      -> SendOutcomeEnvelope;
  static auto for_reply(const SendOutcomeEnvelope &result_envelope)
      -> SendOutcomeEnvelope;
  // Keep native optional-slot paths relative to Result, not the reply shell.
  auto skeleton() const -> const Json & {
    static const Json root = nullptr;
    return structure_ ? *structure_->result_slot : root;
  }
  auto is_reply() const -> bool {
    return structure_ && structure_->result_depth != 0;
  }
  auto wire_format() const -> SendOutcomeWireFormat {
    return structure_ ? structure_->format : SendOutcomeWireFormat::Standalone;
  }
  auto reserve_primary(const SendOutcome &outcome) const
      -> SendOutcomeEnvelopeBudget;
  void validate_outcome_wire(const Json &wire) const {
    auto budget = base_budget();
    budget.inspect(wire, outcome_depth());
    // Reserve the unknown form before the strict decoder copies any evidence.
    // Only the constant header is constructed; arrays remain borrowed.
    auto fallback = base_budget();
    Json empty = {{"alternative", 1},
                  {"value", detail::bot_encode(SendFailure{
                                detail::unknown_send_error(), {}, {}, true})}};
    if (wire_format() == SendOutcomeWireFormat::Standalone)
      empty["schema"] = 1;
    fallback.inspect(empty, outcome_depth());
    const auto alternative =
        detail::bounded_unsigned(wire.at("alternative"), 1);
    const auto &value = wire.at("value");
    const auto &messages =
        value.at(alternative == 0 ? "messages" : "confirmed_messages");
    const auto &details = value.at("output_details");
    if (!messages.is_array() || !details.is_array())
      throw std::invalid_argument("expected send evidence arrays");
    for (const auto &message : messages)
      fallback.inspect(message, outcome_depth() + 3);
    for (const auto &output : details)
      fallback.inspect(output, outcome_depth() + 3);
  }
  void validate_result_wire(const Json &wire) const {
    // Inspect the borrowed Result under the precharged real reply shell. No
    // copy of Result is made merely to manufacture an outer root.
    auto budget =
        structure_ ? structure_->result_base : detail::BotWireBudget{};
    budget.inspect(wire, structure_ ? structure_->result_depth : 0);
    auto check = [&](auto &&self, const Json &base,
                     const Json &actual) -> void {
      if (&base == outcome_slot()) {
        if (wire_format() == SendOutcomeWireFormat::Standalone) {
          detail::require_only_keys(actual, "SendOutcome",
                                    {"schema", "alternative", "value"});
          if (detail::bounded_unsigned(actual.at("schema"), 1) != 1)
            throw std::invalid_argument("send outcome schema mismatch");
        } else
          detail::require_only_keys(actual, "nested SendOutcome",
                                    {"alternative", "value"});
        (void)detail::bounded_unsigned(actual.at("alternative"), 1);
        detail::require_object(actual.at("value"), "send outcome value");
      } else if (base.is_null()) {
        // A reserved optional null may be populated; the whole wire is bounded.
      } else if (base.is_object()) {
        if (!actual.is_object() || actual.size() != base.size())
          throw std::invalid_argument("result differs from reserved wrapper");
        for (auto it = base.begin(); it != base.end(); ++it)
          self(self, it.value(), actual.at(it.key()));
      } else if (base.is_array()) {
        if (!actual.is_array() || actual.size() < base.size())
          throw std::invalid_argument("result removed required array entries");
        for (std::size_t i = 0; i < base.size(); ++i)
          self(self, base[i], actual[i]);
      } else if (base != actual)
        throw std::invalid_argument("result changed required wrapper value");
    };
    check(check, skeleton(), wire);
  }

private:
  struct Structure {
    Json skeleton;
    const Json *outcome_slot;
    const Json *result_slot;
    unsigned depth;
    unsigned result_depth;
    SendOutcomeWireFormat format;
    detail::BotWireBudget base;
    detail::BotWireBudget result_base;
    std::map<const Json *, unsigned> optional_slots;
    Structure(const Json &wire, SendOutcomeWireFormat representation)
        : skeleton(wire), outcome_slot(nullptr), result_slot(nullptr), depth(0),
          result_depth(0), format(representation) {}
  };
  static auto make_skeleton(const Json &required_wrapper,
                            const Json &null_outcome_slot,
                            const Json &result_slot,
                            SendOutcomeWireFormat format)
      -> SendOutcomeEnvelope;
  explicit SendOutcomeEnvelope(std::shared_ptr<const Structure> structure)
      : structure_(std::move(structure)) {}
  auto outcome_slot() const -> const Json * {
    return structure_ ? structure_->outcome_slot : &skeleton();
  }
  auto outcome_depth() const -> unsigned {
    return structure_ ? structure_->depth : 0;
  }
  auto base_budget() const -> detail::BotWireBudget {
    return structure_ ? structure_->base : detail::BotWireBudget{};
  }
  auto inspect_primary(const SendOutcome &outcome, bool unknown) const
      -> detail::BotWireBudget {
    auto budget = base_budget();
    detail::inspect_send_outcome(
        budget, outcome, outcome_depth(),
        wire_format() == SendOutcomeWireFormat::Standalone, unknown);
    return budget;
  }
  std::shared_ptr<const Structure> structure_;
  friend class SendOutcomeEnvelopeBudget;
  friend class detail::SendEvidenceRecovery;
};

// One instance per assembled result. Work charges survive rejected metadata;
// output charges commit only when the fragment fits. Never reset per entry.
class SendOutcomeEnvelopeBudget {
public:
  void inspect_optional(const Json &skeleton_slot, const Json &candidate) {
    const auto depth = slot_depth(skeleton_slot);
    work_.inspect(candidate, depth + (skeleton_slot.is_array() ? 1 : 0));
  }
  void append(const Json &skeleton_array, const Json &encoded_entry) {
    const auto depth = slot_depth(skeleton_array);
    if (!skeleton_array.is_array())
      throw std::invalid_argument("envelope append requires a reserved array");
    work_.inspect(encoded_entry, depth + 1);
    auto next = output_;
    next.inspect(encoded_entry, depth + 1);
    output_ = next;
  }
  void replace_null(const Json &skeleton_null, const Json &encoded_value) {
    const auto depth = slot_depth(skeleton_null);
    if (!skeleton_null.is_null() || replaced_.contains(&skeleton_null))
      throw std::invalid_argument(
          "envelope replacement requires an unused null");
    work_.inspect(encoded_value, depth);
    auto next = output_;
    // This exact null was charged by the factory and can be replaced once.
    --next.nodes_;
    next.inspect(encoded_value, depth);
    replaced_.insert(&skeleton_null);
    output_ = next;
  }

private:
  explicit SendOutcomeEnvelopeBudget(const SendOutcomeEnvelope &envelope,
                                     const SendOutcome &outcome)
      : envelope_(envelope), work_(envelope.inspect_primary(outcome, false)),
        output_(envelope.inspect_primary(outcome, true)) {
    // Reserve the actual primary AND its unknown fallback, without copying it.
    output_.nodes_ = std::max(output_.nodes_, work_.nodes_);
    output_.bytes_ = std::max(output_.bytes_, work_.bytes_);
    work_ = output_;
  }
  auto slot_depth(const Json &slot) const -> unsigned {
    if (envelope_.structure_) {
      const auto found = envelope_.structure_->optional_slots.find(&slot);
      if (found != envelope_.structure_->optional_slots.end())
        return found->second;
    }
    throw std::invalid_argument("optional slot is not owned by this envelope");
  }
  SendOutcomeEnvelope envelope_;
  detail::BotWireBudget work_;
  detail::BotWireBudget output_;
  std::set<const Json *> replaced_;
  friend class SendOutcomeEnvelope;
};
inline auto SendOutcomeEnvelope::reserve_primary(
    const SendOutcome &outcome) const -> SendOutcomeEnvelopeBudget {
  return SendOutcomeEnvelopeBudget(*this, outcome);
}
inline auto SendOutcomeEnvelope::from_skeleton(const Json &required_wrapper,
                                               const Json &null_outcome_slot,
                                               SendOutcomeWireFormat format)
    -> SendOutcomeEnvelope {
  return make_skeleton(required_wrapper, null_outcome_slot, required_wrapper,
                       format);
}
inline auto SendOutcomeEnvelope::make_skeleton(const Json &required_wrapper,
                                               const Json &null_outcome_slot,
                                               const Json &result_slot,
                                               SendOutcomeWireFormat format)
    -> SendOutcomeEnvelope {
  if (!null_outcome_slot.is_null() ||
      (format != SendOutcomeWireFormat::Standalone &&
       format != SendOutcomeWireFormat::NestedVariant))
    throw std::invalid_argument("invalid outcome envelope slot or format");
  detail::validate_bot_wire(required_wrapper);
  std::size_t ordinal = 0;
  std::size_t selected = detail::bot_maximum_items;
  std::size_t selected_result = detail::bot_maximum_items;
  auto locate = [&](auto &&self, const Json &node) -> void {
    if (&node == &null_outcome_slot)
      selected = ordinal;
    if (&node == &result_slot)
      selected_result = ordinal;
    ++ordinal;
    if (node.is_structured())
      for (const auto &child : node)
        self(self, child);
  };
  locate(locate, required_wrapper);
  if (selected == detail::bot_maximum_items ||
      selected_result == detail::bot_maximum_items)
    throw std::invalid_argument(
        "outcome/result slot is not in required wrapper");
  auto structure = std::make_shared<Structure>(required_wrapper, format);
  ordinal = 0;
  auto reserve = [&](auto &&self, const Json &node, unsigned depth,
                     bool inside_result) -> void {
    const auto position = ordinal++;
    if (position == selected_result) {
      structure->result_slot = &node;
      structure->result_depth = depth;
      inside_result = true;
    }
    if (position == selected) {
      structure->outcome_slot = &node;
      structure->depth = depth;
      return;
    }
    if (node.is_null() || node.is_array())
      structure->optional_slots.emplace(&node, depth);
    structure->base.nodes();
    if (!inside_result)
      structure->result_base.nodes();
    const auto bytes = [&](std::size_t count) {
      structure->base.bytes(count);
      if (!inside_result)
        structure->result_base.bytes(count);
    };
    if (node.is_binary())
      bytes(node.get_binary().size());
    else if (node.is_string())
      bytes(node.get_ref<const std::string &>().size());
    else if (node.is_structured())
      for (auto it = node.begin(); it != node.end(); ++it) {
        if (node.is_object())
          bytes(it.key().size());
        self(self, it.value(), depth + 1, inside_result);
      }
  };
  reserve(reserve, structure->skeleton, 0, false);
  SendOutcomeEnvelope envelope(std::move(structure));
  (void)envelope.reserve_primary(
      SendFailure{detail::unknown_send_error(), {}, {}, true});
  return envelope;
}
inline auto SendOutcomeEnvelope::for_reply(const SendOutcomeEnvelope &result)
    -> SendOutcomeEnvelope {
  if (result.is_reply())
    throw std::invalid_argument("outcome envelope already includes a reply");
  // Derive the real public reply shape from its existing codec, not credits.
  Json reply = OperationReply::success(Json(nullptr));
  const auto shell =
      from_skeleton(reply, reply.at("value"), result.wire_format());
  auto allowance = shell.base_budget();
  allowance.inspect(result.skeleton(), shell.outcome_depth());
  // Only a previously validated required skeleton is copied, never effects.
  reply.at("value") = result.skeleton();
  const Json *copied_outcome = nullptr;
  auto locate = [&](auto &&self, const Json &before,
                    const Json &after) -> void {
    if (&before == result.outcome_slot())
      copied_outcome = &after;
    if (before.is_structured()) {
      auto next = after.begin();
      for (const auto &child : before)
        self(self, child, *next++);
    }
  };
  locate(locate, result.skeleton(), reply.at("value"));
  if (!copied_outcome)
    throw std::invalid_argument("outcome slot missing from reply result");
  return make_skeleton(reply, *copied_outcome, reply.at("value"),
                       result.wire_format());
}

namespace detail {
// Work is charged before decoding/copying, including invalid and duplicate
// entries. Output has a separate budget so the retained failure remains
// encodable.
class SendEvidenceRecovery {
public:
  SendEvidenceRecovery(const SendMessageRequest &request,
                       const SendOutcomeEnvelope &envelope)
      : SendEvidenceRecovery(request.target, &request, envelope) {}
  // A provider extension has a target but no common input-body associations.
  SendEvidenceRecovery(const ConversationRef &target,
                       const SendOutcomeEnvelope &envelope)
      : SendEvidenceRecovery(target, nullptr, envelope) {}
  template <class F> void attempt(F &&operation) {
    try {
      operation();
    } catch (const BotBudgetExceeded &) {
      throw;
    } catch (...) { /* An invalid field cannot erase independent evidence. */
    }
  }
  void reference(const MessageRef &message) {
    work_.nodes();
    message.validate();
    work_.inspect(Json(message), depth_ + 3);
    (void)retain_reference(message);
  }
  void reference(const Json &wire) {
    work_.inspect(wire, depth_ + 3);
    (void)retain_reference(wire.get<MessageRef>());
  }
  void details(const SendOutputDetails &value) {
    work_.nodes();
    value.message.validate();
    work_.inspect(Json(value.message), depth_ + 4);
    const auto index = retain_reference(value.message);
    if (request_ && !retained_[index].content && value.content)
      attempt([&] {
        std::vector<ContentSlice> content;
        for (const auto &slice : *value.content) {
          work_.inspect(bot_encode(slice), depth_ + 5);
          validate_slice(slice);
          content.push_back(slice);
        }
        retain_content(index, std::move(content));
      });
    for (const auto &media : value.reusable_media)
      attempt([&] {
        work_.nodes();
        media.media.validate();
        work_.inspect(bot_encode(media.media), depth_ + 6);
        PublishedMedia recovered{media.media, std::nullopt, std::nullopt};
        if (media.identity)
          attempt([&] {
            media.identity->validate();
            work_.inspect(bot_encode(*media.identity), depth_ + 6);
            validate_identity(*media.identity, recovered.media);
            recovered.identity = media.identity;
          });
        if (request_)
          attempt([&] {
            work_.nodes();
            validate_block(media.block_index);
            recovered.block_index = media.block_index;
          });
        retain_media(index, std::move(recovered));
      });
  }
  void details(const Json &wire) {
    work_.nodes();
    if (!wire.is_object())
      return;
    const auto &message = wire.at("message");
    work_.inspect(message, depth_ + 4);
    const auto index = retain_reference(message.get<MessageRef>());
    // Metadata needs a recognized record, but malformed metadata never removes
    // the independently established physical reference.
    work_.inspect(wire.at("schema"), depth_ + 4);
    if (bounded_unsigned(wire.at("schema"), 1) != 1)
      return;
    if (request_ && !retained_[index].content && wire.contains("content"))
      attempt([&] {
        const auto &field = wire.at("content");
        work_.inspect(field, depth_ + 4);
        if (field.is_null())
          return;
        auto content = bot_decode<std::vector<ContentSlice>>(field);
        for (const auto &slice : content)
          validate_slice(slice);
        retain_content(index, std::move(content));
      });
    if (!wire.contains("reusable_media"))
      return;
    const auto &media = wire.at("reusable_media");
    work_.nodes();
    if (!media.is_array())
      return;
    for (const auto &entry : media)
      attempt([&] {
        work_.nodes();
        if (!entry.is_object())
          return;
        work_.inspect(entry.at("schema"), depth_ + 6);
        if (bounded_unsigned(entry.at("schema"), 1) != 1)
          return;
        const auto &source = entry.at("media");
        work_.inspect(source, depth_ + 6);
        PublishedMedia recovered{bot_decode<MediaRef>(source), std::nullopt,
                                 std::nullopt};
        if (entry.contains("identity"))
          attempt([&] {
            const auto &identity = entry.at("identity");
            work_.inspect(identity, depth_ + 6);
            auto decoded = bot_decode<std::optional<MediaIdentity>>(identity);
            if (decoded)
              validate_identity(*decoded, recovered.media);
            recovered.identity = std::move(decoded);
          });
        if (request_ && entry.contains("block_index"))
          attempt([&] {
            const auto &block = entry.at("block_index");
            work_.inspect(block, depth_ + 6);
            auto decoded = bot_decode<std::optional<std::size_t>>(block);
            validate_block(decoded);
            recovered.block_index = decoded;
          });
        retain_media(index, std::move(recovered));
      });
  }
  void outcome(const SendOutcome &outcome) {
    std::visit(
        [&](const auto &value) {
          const auto &messages = [&]() -> const std::vector<MessageRef> & {
            if constexpr (std::same_as<std::decay_t<decltype(value)>,
                                       SendReceipt>)
              return value.messages;
            else
              return value.confirmed_messages;
          }();
          for (const auto &message : messages)
            attempt([&] { reference(message); });
          for (const auto &item : value.output_details)
            attempt([&] { details(item); });
        },
        outcome);
  }
  void wire(const Json &payload) {
    work_.nodes();
    if (!payload.is_object() || !payload.contains("value"))
      return;
    const auto &value = payload.at("value");
    work_.nodes();
    if (!value.is_object())
      return;
    for (const auto *key : {"messages", "confirmed_messages"}) {
      work_.nodes();
      if (!value.contains(key) || !value.at(key).is_array())
        continue;
      for (const auto &item : value.at(key))
        attempt([&] { reference(item); });
    }
    work_.nodes();
    if (value.contains("output_details") &&
        value.at("output_details").is_array())
      for (const auto &item : value.at("output_details"))
        attempt([&] { details(item); });
  }
  auto finish() -> SendOutcome {
    return send_failure(unknown_send_error(), std::move(retained_), true);
  }

private:
  SendEvidenceRecovery(const ConversationRef &target,
                       const SendMessageRequest *request,
                       const SendOutcomeEnvelope &envelope)
      : target_(target), request_(request), depth_(envelope.outcome_depth()),
        work_(envelope.base_budget()), output_(envelope.base_budget()) {
    Json empty = {
        {"alternative", 1},
        {"value", bot_encode(SendFailure{unknown_send_error(), {}, {}, true})}};
    if (envelope.wire_format() == SendOutcomeWireFormat::Standalone)
      empty["schema"] = 1;
    output_.inspect(empty, depth_);
  }
  auto retain_reference(const MessageRef &message) -> std::size_t {
    message.validate();
    if (message.conversation != target_)
      throw std::invalid_argument("confirmed send scope mismatch");
    const auto found = indices_.find(message.id);
    if (found != indices_.end())
      return found->second;
    SendOutputDetails detail{message, std::nullopt, {}};
    output_.inspect(Json(message), depth_ + 3);
    output_.inspect(bot_encode(detail), depth_ + 3);
    const auto index = retained_.size();
    retained_.push_back(std::move(detail));
    indices_.emplace(message.id, index);
    return index;
  }
  void validate_slice(const ContentSlice &slice) {
    work_.nodes();
    if (!request_)
      throw std::invalid_argument(
          "extension output has no common content association");
    if (const auto text = content_slice_text(slice, *request_)) {
      work_.bytes(text->size());
      validate_utf8(*text);
    }
  }
  void retain_content(std::size_t index, std::vector<ContentSlice> content) {
    if (content.empty())
      return;
    output_.inspect(bot_encode(content), depth_ + 4);
    retained_[index].content = std::move(content);
  }
  void validate_identity(const MediaIdentity &identity,
                         const MediaRef &media) const {
    if (identity.installation != media.installation)
      throw std::invalid_argument("media identity scope mismatch");
  }
  void validate_block(const std::optional<std::size_t> &block) const {
    if (block && (!request_ || *block >= request_->body.size() ||
                  std::holds_alternative<RichText>(request_->body[*block])))
      throw std::invalid_argument("invalid media input association");
  }
  void retain_media(std::size_t index, PublishedMedia media) {
    media.validate();
    if (media.media.installation != target_.installation)
      throw std::invalid_argument("published media scope mismatch");
    output_.inspect(bot_encode(media), depth_ + 5);
    retained_[index].reusable_media.push_back(std::move(media));
  }
  const ConversationRef &target_;
  const SendMessageRequest *request_; // null explicitly means no common body
  unsigned depth_;
  BotWireBudget work_;
  BotWireBudget output_;
  std::map<MessageId, std::size_t> indices_;
  std::vector<SendOutputDetails> retained_;
};
inline void validate_single_message_outcome(const ConversationRef &target,
                                            const SendOutcome &outcome) {
  target.validate();
  std::visit(
      [&](const auto &value) {
        value.validate();
        if constexpr (std::same_as<std::decay_t<decltype(value)>,
                                   SendReceipt>) {
          if (value.messages.size() != 1)
            throw std::invalid_argument(
                "extension receipt requires exactly one message");
        }
        for (const auto &output : value.output_details) {
          if (output.message.conversation != target || output.content)
            throw std::invalid_argument(
                "extension output scope or content association mismatch");
          for (const auto &media : output.reusable_media)
            if (media.block_index)
              throw std::invalid_argument(
                  "extension media has no common block association");
        }
      },
      outcome);
}
} // namespace detail

template <> struct GatewayCodec<SendOutcome> {
  static auto encode(const SendOutcome &value) -> Json {
    return detail::encode_send_outcome_value(value);
  }
  static auto decode(const Json &payload) -> SendOutcome {
    detail::validate_bot_wire(payload);
    detail::require_only_keys(payload, "SendOutcome",
                              {"schema", "alternative", "value"});
    if (detail::bounded_unsigned(payload.at("schema"), 1) != 1)
      throw std::invalid_argument("send outcome schema mismatch");
    auto value = detail::decode_variant<SendOutcome>(
        payload.at("value"),
        detail::bounded_unsigned(payload.at("alternative"), 1));
    std::visit([](const auto &v) { v.validate(); }, value);
    return value;
  }
};
inline void to_json(Json &document, const SendOutcome &value) {
  document = GatewayCodec<SendOutcome>::encode(value);
}
inline void from_json(const Json &document, SendOutcome &value) {
  value = GatewayCodec<SendOutcome>::decode(document);
}
// All admission/recovery reserves an explicit enclosing envelope, including
// the unknown-outcome fallback. Structural standalone serialization remains
// separate from request-bound admission.
inline auto normalize_send_outcome(const SendMessageRequest &request,
                                   SendOutcome outcome,
                                   const SendOutcomeEnvelope &envelope)
    -> SendOutcome {
  try {
    (void)envelope.reserve_primary(outcome);
    validate_send_outcome(request, outcome);
    return outcome;
  } catch (...) {
  }
  detail::SendEvidenceRecovery recovery(request, envelope);
  try {
    recovery.outcome(outcome);
  } catch (const detail::BotBudgetExceeded &) {
  }
  return recovery.finish();
}
inline auto normalize_single_message_outcome(
    const ConversationRef &target, SendOutcome outcome,
    const SendOutcomeEnvelope &envelope) -> SendOutcome {
  try {
    (void)envelope.reserve_primary(outcome);
    detail::validate_single_message_outcome(target, outcome);
    return outcome;
  } catch (...) {
  }
  detail::SendEvidenceRecovery recovery(target, envelope);
  try {
    recovery.outcome(outcome);
  } catch (const detail::BotBudgetExceeded &) {
  }
  return recovery.finish();
}
namespace detail {
inline auto decode_enveloped_send_outcome(const Json &payload,
                                          const SendOutcomeEnvelope &envelope)
    -> SendOutcome {
  envelope.validate_outcome_wire(payload);
  if (envelope.wire_format() == SendOutcomeWireFormat::Standalone) {
    require_only_keys(payload, "SendOutcome",
                      {"schema", "alternative", "value"});
    if (bounded_unsigned(payload.at("schema"), 1) != 1)
      throw std::invalid_argument("send outcome schema mismatch");
  } else
    require_only_keys(payload, "nested SendOutcome", {"alternative", "value"});
  auto value = decode_variant<SendOutcome>(
      payload.at("value"), bounded_unsigned(payload.at("alternative"), 1));
  std::visit([](const auto &v) { v.validate(); }, value);
  return value;
}
} // namespace detail
inline auto decode_send_outcome(const Json &payload,
                                const SendMessageRequest &request,
                                const SendOutcomeEnvelope &envelope)
    -> SendOutcome {
  try {
    return normalize_send_outcome(
        request, detail::decode_enveloped_send_outcome(payload, envelope),
        envelope);
  } catch (...) {
  }
  detail::SendEvidenceRecovery recovery(request, envelope);
  try {
    recovery.wire(payload);
  } catch (const detail::BotBudgetExceeded &) {
  }
  return recovery.finish();
}
inline auto decode_single_message_outcome(const Json &payload,
                                          const ConversationRef &target,
                                          const SendOutcomeEnvelope &envelope)
    -> SendOutcome {
  try {
    return normalize_single_message_outcome(
        target, detail::decode_enveloped_send_outcome(payload, envelope),
        envelope);
  } catch (...) {
  }
  detail::SendEvidenceRecovery recovery(target, envelope);
  try {
    recovery.wire(payload);
  } catch (const detail::BotBudgetExceeded &) {
  }
  return recovery.finish();
}

} // namespace obcx::bot
