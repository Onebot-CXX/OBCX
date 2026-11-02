#pragma once

#include "onebot11/bot/group_forward.hpp"
#include <map>

namespace obcx::onebot11::bot::forward_outcome {
namespace d = obcx::bot;
using Request = SendOneBotGroupForwardMessageRequest;
using Result = OneBotGroupForwardMessageResult;

inline auto envelope() -> const d::SendOutcomeEnvelope & {
  static const auto value = [] {
    Result minimum{d::send_failure(d::detail::unknown_send_error(), {}, true),
                   std::nullopt,
                   {}};
    // Derive every required key/container/depth from the actual Result codec.
    auto wire = d::GatewayCodec<Result>::encode(minimum);
    auto &slot = wire.at("outcome");
    d::detail::require_only_keys(slot, "QQ nested outcome",
                                 {"alternative", "value"});
    slot = nullptr;
    return d::SendOutcomeEnvelope::for_reply(
        d::SendOutcomeEnvelope::from_skeleton(
            wire, slot, d::SendOutcomeWireFormat::NestedVariant));
  }();
  return value;
}
inline void make_unknown(d::SendOutcome &outcome) {
  auto error = d::detail::unknown_send_error();
  if (auto *failure = std::get_if<d::SendFailure>(&outcome)) {
    failure->error = std::move(error);
    failure->may_have_unconfirmed_messages = true;
  } else {
    auto &receipt = std::get<d::SendReceipt>(outcome);
    d::SendFailure unknown{std::move(error), std::move(receipt.messages),
                           std::move(receipt.output_details), true};
    outcome = std::move(unknown);
  }
}
inline auto minimal(d::SendOutcome outcome) -> Result {
  return {std::move(outcome), std::nullopt, {}};
}

// The primary is already admitted against the same real reply allowance.
// One cumulative optional budget covers ALL siblings, including rejected work.
class Assembly {
public:
  Assembly(const Request &request, Result &result,
           const d::SendOutcomeEnvelope &allowance)
      : request_(request), result_(result), allowance_(allowance),
        budget_(allowance.reserve_primary(result.outcome)) {
    const auto &outputs = std::visit(
        [](const auto &v) -> const std::vector<d::SendOutputDetails> & {
          return v.output_details;
        },
        result_.outcome);
    for (const auto &output : outputs)
      confirmed_.insert(output.message.id.serialized());
  }
  void typed(const Result *original) {
    if (!original)
      return;
    attempt([&] {
      if (original->forward)
        retain_forward(*original->forward);
    });
    for (const auto &entry : original->output_details) {
      if (exhausted_)
        break;
      attempt([&] {
        budget_.inspect_optional(nodes_slot(), d::Json(nullptr));
        retain_entry(entry);
      });
    }
  }
  void wire(const d::Json &original) {
    attempt([&] {
      const auto &forward = original.at("forward");
      budget_.inspect_optional(forward_slot(), forward);
      if (!forward.is_null())
        retain_forward(d::GatewayCodec<QqForwardRef>::decode(forward));
    });
    if (exhausted_)
      return;
    // Charge malformed container values too, without copying an entire array.
    const auto it = original.find("output_details");
    if (it == original.end()) {
      corrupt_ = true;
      return;
    }
    if (!it->is_array()) {
      attempt([&] {
        budget_.inspect_optional(nodes_slot(), *it);
        corrupt_ = true;
      });
      return;
    }
    for (const auto &entry : *it) {
      if (exhausted_)
        break;
      attempt([&] {
        budget_.inspect_optional(nodes_slot(), entry);
        d::detail::require_only_keys(entry, "QQ output provenance",
                                     {"schema", "message", "node_indices"});
        if (d::detail::bounded_unsigned(entry.at("schema"), 1) != 1)
          throw std::invalid_argument("QQ provenance schema mismatch");
        ForwardOutputDetails value{entry.at("message").get<d::MessageRef>(),
                                   std::nullopt};
        // Invalid association is not a reason to discard the confirmed key.
        try {
          d::detail::DomainRecordPreflight<ForwardOutputDetails>::validate(
              entry);
          value.node_indices =
              d::detail::domain_decode<std::optional<std::vector<std::size_t>>>(
                  entry.at("node_indices"));
        } catch (...) {
          corrupt_ = true;
        }
        retain_entry(value);
      });
    }
  }
  void resource(const QqForwardRef &value) {
    attempt([&] { retain_forward(value); });
  }
  void provenance(const ForwardOutputDetails &value) {
    attempt([&] {
      budget_.inspect_optional(nodes_slot(), d::Json(nullptr));
      retain_entry(value);
    });
  }
  bool corrupt() const { return corrupt_; }

private:
  template <class F> void attempt(F &&f) {
    if (exhausted_)
      return;
    try {
      f();
    } catch (const d::detail::DomainBudgetExceeded &) {
      corrupt_ = exhausted_ = true;
    } catch (...) {
      corrupt_ = true;
    }
  }
  auto nodes_slot() const -> const d::Json & {
    return allowance_.skeleton().at("output_details");
  }
  auto forward_slot() const -> const d::Json & {
    return allowance_.skeleton().at("forward");
  }
  bool confirmed(const d::MessageRef &message) const {
    message.validate();
    return message.conversation == request_.target &&
           confirmed_.contains(message.id.serialized());
  }
  void retain_forward(const QqForwardRef &forward) {
    forward.validate();
    if (!confirmed(forward.source))
      throw std::invalid_argument("unconfirmed forward resource");
    // Typed field bounds are checked before encoding/copying a single value.
    const auto encoded = d::detail::domain_encode(forward);
    budget_.replace_null(forward_slot(), encoded);
    result_.forward = forward;
  }
  void retain_entry(const ForwardOutputDetails &entry) {
    const auto &conversation = entry.message.conversation;
    const auto &installation = conversation.installation;
    // Check declared wire field lengths without decoding/semantic validation.
    // ADL MessageRef encoding validates first, so it cannot account for
    // rejected typed keys. Inspect their bounded raw spelling before any such
    // rejection.
    if (installation.installation_id.size() > 256 ||
        installation.surface.value().size() > 128 ||
        conversation.id.serialized().size() > 4096 ||
        entry.message.id.serialized().size() > 4096)
      throw std::invalid_argument("oversized forward provenance key");
    const d::Json key = {{"conversation",
                          {{"installation",
                            {{"installation_id", installation.installation_id},
                             {"surface", installation.surface.value()}}},
                           {"id", conversation.id.serialized()}}},
                         {"id", entry.message.id.serialized()}};
    budget_.inspect_optional(nodes_slot(), key);
    if (!confirmed(entry.message))
      throw std::invalid_argument("unconfirmed forward provenance");
    const auto found = seen_.find(entry.message.id.serialized());
    if (found != seen_.end()) {
      result_.output_details[found->second].node_indices.reset();
      corrupt_ = true;
      return;
    }
    bool association = true;
    if (entry.node_indices && entry.node_indices->size() <= 99)
      budget_.inspect_optional(nodes_slot(),
                               d::detail::domain_encode(*entry.node_indices));
    try {
      entry.validate();
      if (entry.node_indices)
        for (auto index : *entry.node_indices)
          if (index >= request_.nodes.size())
            throw std::invalid_argument("forward node index mismatch");
    } catch (...) {
      association = false;
      corrupt_ = true;
    }
    // Encode the validated candidate before retaining any optional vector.
    auto encoded = d::detail::domain_encode(
        ForwardOutputDetails{entry.message, std::nullopt});
    if (association && entry.node_indices)
      encoded["node_indices"] = d::detail::domain_encode(*entry.node_indices);
    budget_.append(nodes_slot(), encoded);
    ForwardOutputDetails value{entry.message, std::nullopt};
    if (association)
      value.node_indices = entry.node_indices;
    result_.output_details.push_back(std::move(value));
    seen_.emplace(result_.output_details.back().message.id.serialized(),
                  result_.output_details.size() - 1);
  }
  const Request &request_;
  Result &result_;
  const d::SendOutcomeEnvelope &allowance_;
  d::SendOutcomeEnvelopeBudget budget_;
  std::set<std::string_view> confirmed_;
  // Own keys so optional-vector relocation cannot invalidate the index.
  std::map<std::string, std::size_t> seen_;
  bool corrupt_ = false;
  bool exhausted_ = false;
};

inline auto assemble(const Request &request, const Result *original,
                     d::SendOutcome primary,
                     const d::SendOutcomeEnvelope &allowance) -> Result {
  auto result = minimal(std::move(primary));
  try {
    Assembly assembly(request, result, allowance);
    assembly.typed(original);
    if (assembly.corrupt())
      make_unknown(result.outcome);
  } catch (...) {
    make_unknown(result.outcome);
  }
  return result;
}
inline auto decode(const Request &request, const d::Json &wire,
                   const d::SendOutcomeEnvelope &allowance,
                   bool malformed_outer) -> Result {
  // Capture primary evidence before wrapper inspection or optional work.
  const auto it = wire.is_object() ? wire.find("outcome") : wire.end();
  auto primary =
      it != wire.end()
          ? d::decode_single_message_outcome(*it, request.target, allowance)
          : d::send_failure(d::detail::unknown_send_error(), {}, true);
  auto result = minimal(std::move(primary));
  bool corrupt = malformed_outer;
  try {
    allowance.validate_result_wire(wire);
  } catch (...) {
    corrupt = true;
  }
  try {
    Assembly assembly(request, result, allowance);
    assembly.wire(wire);
    corrupt = corrupt || assembly.corrupt();
  } catch (...) {
    corrupt = true;
  }
  if (corrupt)
    make_unknown(result.outcome);
  return result;
}
} // namespace obcx::onebot11::bot::forward_outcome

namespace obcx::bot {
inline auto
OperationTraits<onebot11::bot::SendOneBotGroupForwardMessageRequest>::
    normalize_result(const request_type &request, result_type value)
        -> result_type {
  const auto &allowance = onebot11::bot::forward_outcome::envelope();
  auto primary = normalize_single_message_outcome(
      request.target, std::move(value.outcome), allowance);
  return onebot11::bot::forward_outcome::assemble(
      request, &value, std::move(primary), allowance);
}
inline auto
OperationTraits<onebot11::bot::SendOneBotGroupForwardMessageRequest>::
    decode_result(const request_type &request, const Json &wire)
        -> result_type {
  return onebot11::bot::forward_outcome::decode(
      request, wire, onebot11::bot::forward_outcome::envelope(), false);
}
} // namespace obcx::bot
