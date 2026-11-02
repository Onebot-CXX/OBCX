# Full-result allowance for message-producing outcomes

This is an SDK/process budget seam, not native policy registration or runtime/provider acceptance. Existing shared limits remain 65,536 nodes, 64 MiB of string/key/binary bytes and depth 32. No smaller provider cap or numeric budget credit is introduced.

## Exact SDK API

Header: `core/bot/send_gateway_codec.hpp`, namespace `obcx::bot`.

```cpp
enum class SendOutcomeWireFormat { Standalone, NestedVariant };

SendOutcomeEnvelope::standalone() -> SendOutcomeEnvelope;
SendOutcomeEnvelope::for_reply(const SendOutcomeEnvelope &result_envelope)
    -> SendOutcomeEnvelope;
SendOutcomeEnvelope::from_skeleton(
    const Json &required_wrapper, const Json &null_outcome_slot,
    SendOutcomeWireFormat) -> SendOutcomeEnvelope;

envelope.skeleton() -> const Json &;
envelope.wire_format() -> SendOutcomeWireFormat;
envelope.is_reply() -> bool;
envelope.reserve_primary(const SendOutcome &) -> SendOutcomeEnvelopeBudget;
envelope.validate_outcome_wire(const Json &) -> void;
envelope.validate_result_wire(const Json &) -> void;

budget.inspect_optional(const Json &skeleton_slot, const Json &candidate) -> void;
budget.append(const Json &skeleton_array, const Json &encoded_entry) -> void;
budget.replace_null(const Json &skeleton_null, const Json &encoded_value) -> void;

normalize_send_outcome(const SendMessageRequest &, SendOutcome,
                       const SendOutcomeEnvelope &) -> SendOutcome;
normalize_single_message_outcome(const ConversationRef &, SendOutcome,
                                 const SendOutcomeEnvelope &) -> SendOutcome;
decode_send_outcome(const Json &, const SendMessageRequest &,
                    const SendOutcomeEnvelope &) -> SendOutcome;
decode_single_message_outcome(const Json &, const ConversationRef &,
                              const SendOutcomeEnvelope &) -> SendOutcome;
```

All old two-argument normalizer/decoder signatures retain their standalone semantics. The new envelope-aware overloads additionally reserve the unknown fallback before admitting a complete primary. There are no new default arguments.

`Standalone` is the existing `{schema:1, alternative, value}` representation. `NestedVariant` is the existing generic nested `{alternative, value}` representation; the record inside `value` still has its own schema. Representation is explicit, not inferred from malformed input. The contextual decoder reads the supplied inner value directly; it never copies an unbounded inner value to manufacture a standalone root. Malformed variants still use the same indexed evidence recovery.

## Required skeleton and slot identity

Derive the skeleton from the **actual valid minimum fallback Result encoding**, with exactly its outcome slot replaced by null. Required keys, primitive values, mandatory containers, strings and their depths must be real, not estimated discounts. Optional fields remain in their actual absent/empty representation. Supply the null slot by reference from that same Json tree.

The factory validates the bounded tree before making its owned copy, locates the designated slot, precharges all other nodes/bytes, records its actual depth, and proves that an empty unknown outcome fits. A missing/non-null slot or invalid format is rejected. An over-budget or too-deep skeleton is rejected before handler invocation when the policy constructs its context.

The returned envelope owns an immutable skeleton. Optional accounting accepts **only slot references from `envelope.skeleton()`**, not equal-looking values from the original Json or a Json copy. It verifies slot identity/depth and records each null replacement once. No public numeric refund exists. Array entries are appended; existing required array entries remain present.

### Explicit approved outer boundary

The supervisor explicitly required the real successful **OperationReply** envelope, not merely `GatewayCodec<Result>`. Its existing serializer validates `{ok:true,value:<Result>}` and otherwise could still fail after a Result-only checkpoint. `for_reply(result_envelope)` derives that actual shell through `OperationReply::success(Json(nullptr))` and its existing serializer, then checks the already-validated required Result skeleton under that shell before copying it. It records both the Result and outcome locations/depths. No actual primary is copied to manufacture an outer root, and no numeric margin is supplied. Calling `for_reply` on an already reply-aware envelope is rejected.

For a reply envelope, `skeleton()` still exposes the owned **Result subtree**, so native optional-slot paths do not change. All primary/work/output reservations nevertheless include the real outer reply. Final `validate_result_wire` inspects the borrowed Result using a precharged actual reply-shell budget and depth, without constructing/copying a full reply for measurement. It also checks required Result keys/primitive values and array prefixes; reserved nulls may be populated and arrays may grow. This is accounting/shape validation, not native semantic authorization.

This intentionally tightens **process** boundary admission only by actual reply structure/depth. It does not change any two-argument standalone SDK normalizer/decoder's exact boundary behavior. `operation_result.hpp` and its wire layout remain untouched.

## Native consumption example

For the documented schema-1 forward wrapper, its sparse minimum has this shape:

```cpp
Json required = {{"schema", 1},
                 {"outcome", nullptr},
                 {"forward", nullptr},
                 {"output_details", Json::array()}};
auto envelope = SendOutcomeEnvelope::for_reply(
    SendOutcomeEnvelope::from_skeleton(
        required, required.at("outcome"), SendOutcomeWireFormat::NestedVariant));
```

The native owner must verify/derive this from its real minimal fallback codec rather than treating the example as new QQ policy. Sparse node provenance avoids duplicating physical refs; it does not remove these wrapper keys/containers from accounting.

Use the **same `for_reply` factory and required structure** for process context, primary normalizer/decoder, optional assembly and minimal fallback. A fixed wrapper may share one immutable envelope across callbacks. If required fields depend on the request, derive their actual values deterministically from that request before provider I/O; do not reserve a different structure in a later callback.

```cpp
// Existing native context callback:
return obcx::core::MessageOutcomeContext{request.target, nullptr, envelope};

// Existing primary normalization callback (owned primary is moved):
auto primary = normalize_single_message_outcome(
    request.target, std::move(outcome), envelope);

// Client/wire path: borrow the nested variant, not the enclosing wrapper.
auto decoded = decode_single_message_outcome(
    wrapper.at("outcome"), request.target, envelope);

// Existing reconstruction callback, after primary admission:
auto budget = envelope.reserve_primary(primary);
const auto &nodes_slot = envelope.skeleton().at("output_details");
const auto &forward_slot = envelope.skeleton().at("forward");
```

One `budget` instance covers the whole reconstruction, including all optional siblings:

1. Call `budget.inspect_optional(nodes_slot, raw_entry)` before parsing/copying a borrowed raw entry. This charges inspected work even when native validation later rejects it. Do not scan/copy the entire auxiliary array first.
2. Apply existing bounded native shape/range/scope/reference-membership checks. Do not invent node provenance, common body content or media block indices. If the source is already typed, apply its existing bounded field checks before encoding a single candidate; never encode an unchecked auxiliary container merely to measure it.
3. For the resulting canonical wire fragment, call `budget.append(nodes_slot, encoded_entry)` **before retaining/inserting** that entry in the result. The method charges work and checks a tentative output budget; rejected output does not consume the output reservation. There is no re-encoding of the growing result/prefix.
4. For an independently valid forward resource, use `inspect_optional(forward_slot, raw_forward)` before extraction where applicable, then `replace_null(forward_slot, encoded_forward)` before retaining it. This releases only that factory-identified placeholder node, once; key bytes and enclosing structure remain charged.
5. Ordinary malformed metadata may be skipped selectively without dropping valid siblings. On shared work/output exhaustion, stop the optional work as appropriate and keep all admitted primary effects. A budget exception is not evidence that a provider send never occurred. Native result semantics remain the owner's responsibility.

`inspect_optional` is work-only. `append`/`replace_null` also inspect their canonical fragment, so raw inspection plus canonical accounting is deliberately charged as two inspections, not silently refunded. Do not reset/clone a budget per candidate, continue with a fresh budget after exhaustion, or treat output accounting as a substitute for native validation. An output charge precedes insertion; if later native construction fails, abandoning the attempt may leave a conservative reservation. It must never be reused to admit unaccounted additional data.

`reserve_primary` charges the actual primary and its unknown-failure form, taking the componentwise maximum required nodes/bytes. Each form is depth-checked. It uses bounded fragments and borrowed message/detail containers rather than copying a whole primary to construct the fallback. It is not a replacement for semantic/target validation; use the normalizer or process admission first.

The two explicit budgets are SDK-owned: inspected work and encoded output. Native code must not create a competing limit or calculate manual credits. Optional slot lookup is indexed, each candidate is inspected separately, and no repeated whole-prefix encoding or quadratic fit probing is needed.

## Process propagation and fallback guarantee

`MessageOutcomeContext` has a validated `SendOutcomeEnvelope envelope` and these constructions:

```cpp
MessageOutcomeContext(target, common_request);           // direct Result + real reply
MessageOutcomeContext(target, common_request, envelope); // explicit for_reply envelope
```

Only a real common request supplies body associations. No-body extensions pass null. The three-argument constructor rejects a Result-only envelope; callers must use `for_reply`. The two-argument constructor reserves the actual reply around a standalone SendOutcome. The existing MessageResultPolicy callback signatures are unchanged. The default common policy shares one reply envelope between its context and its envelope-aware primary normalizer.

The initial `SendEvidenceRecovery` starts **both** work/output states with the required complete-reply allowance and applies the actual outcome depth to every inspected/retained fragment. Only the bounded full-wrapper-admissible prefix becomes the process checkpoint; there is no larger notification-only prefix later clipped from the caller.

Before adopting a normalized checkpoint expansion, `validate_effects` preflights both actual and unknown forms under the same context, then performs existing scope/provenance/retention checks. Later result validation and fallback use this same context. Unknown conversion therefore cannot introduce an unreserved header after effects have been captured. The actual result is also checked against the reserved skeleton before publication.

The final `make_failure` contract remains strict: encode every supplied primary effect in the declared minimal wrapper, not a smaller clipped outcome. A callback/codec that changes required structure, erases effects or violates its fallback contract still triggers the existing explicit internal-contract diagnostic, never an accepted native path. That diagnostic is not an alternative registration strategy. The native owner must consume and review this seam before enabling the policy.

Common/direct policies retain their existing construction syntax and effect/retry semantics, but process boundary admission now includes the real reply overhead as explicitly approved. Standalone SDK admission remains unchanged. No registry, dispatcher, operation_result, cache/origin/resolver, value layout, actor or platform implementation was modified here.

## Remaining client/owner gates

QQ must use the same reply-aware envelope in its process policy and request-aware public `decode_result`/normalization hook, including nested-variant and optional-metadata accounting. TG direct policies/client hooks can use `for_reply(standalone())` with the contextual helpers when claiming the full reply allowance. These consumption changes and registration are their owners' gates; no active platform tree was edited.

The common public `BotClient::send_impl` in read-only `bot_client.hpp` still calls the legacy two-argument `decode_send_outcome` in both successful and malformed-outer-value paths. If parent wants that client to enforce reply-aware recovery of arbitrary external/malformed input, the precise separate change is to pass `SendOutcomeEnvelope::for_reply(SendOutcomeEnvelope::standalone())` at both calls. This increment does not silently change the legacy decoder or claim that client migration. Generic `Traits::decode_result` dispatch and outer OperationReply parsing remain read-only. The helpers preflight the represented subtree under a derived outer allowance; they do not independently validate an unseen external outer wire object. No installed-SDK, actor-transport, runtime replay or provider acceptance is claimed.
