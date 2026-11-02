# Message-producing extension binding handoff

Process-only implementation; not platform registration or runtime/provider acceptance.
Public gateway/envelope/request/result wire layouts are unchanged. No new SDK exports or production source registrations are required. Parent must propagate the new private header `include/core/bot/message_operation_binding.hpp` with the changed registry/handler headers and registry/dispatcher sources.

## Explicit registration

`OperationDescription` now includes `bool message_producing`. It is supplied by the actual typed `OperationDefinition` policy, not inferred from action strings, platform names, RTTI, or native JSON.

Ordinary queries retain `OperationDefinition<Request>{dependencies}` and their existing result validation/encoding. A message-producing extension uses:

```cpp
OperationDefinition<Request>{dependencies, MessageResultPolicy<Request>{...}}
```

Its `bot::OperationTraits<Request>` must declare `requires_outcome_client` (normally `static constexpr bool ... = true`). Marked operations and direct SendOutcome result types cannot use an ordinary definition without a policy. The explicit policy constructor requires the marker. The generic `bind_operation_handler` rejects marked operations/direct SendOutcome results; use an outcome-aware handler that owns its confirmed prefix across every transport step and cancellation. This marker also excludes ordinary public `bot::invoke`; the platform owner supplies its outcome-safe client/decode path.

Common SendMessageRequest retains source-compatible default construction, but its default policy enters exactly the same completion engine. Its specialization supplies the real request for content association validation, not a dispatcher exception.

## Policy API

`include/core/bot/message_operation_binding.hpp`, namespace `obcx::core`:

```cpp
struct MessageOutcomeContext {
  const bot::ConversationRef &target;
  const bot::SendMessageRequest *common_request;
};

template <bot::TypedOperation Request> struct MessageResultPolicy {
  using Result = typename bot::OperationTraits<Request>::result_type;
  std::function<MessageOutcomeContext(const Request &)> context;
  bot::SendOutcome &(*outcome)(Result &) noexcept;
  std::function<bot::SendOutcome(const Request &, bot::SendOutcome)> normalize_outcome;
  std::function<Result(const Request &, const Result *original,
                       bot::SendOutcome)> normalize_result;
  std::function<Result(const Request &, bot::SendFailure)> make_failure;
};
```

All fields are required. The context callback runs before the handler; it borrows the validated owned request, returns its complete target, and must not return references to temporary storage. Core validates installation equality. Extension contexts use `{request.target, nullptr}`. Only a real common SendMessageRequest can supply `common_request`; do not fabricate a body or input block indices.

`outcome` is a nonthrowing, nonallocating reference accessor: direct results return themselves; nested QQ results return `.outcome`. Before invoking potentially throwing normalization/result callbacks, core uses SDK `detail::SendEvidenceRecovery` with the actual common request or exact target to traverse **borrowed** primary evidence under shared work/output budgets. No raw outcome or auxiliary vector is copied to create this checkpoint. Budget exhaustion retains a bounded target-scoped prefix and unknown remainder.

Then core MOVES the owned primary outcome into `normalize_outcome`. The callback uses the request-aware common normalizer or the shared SDK single-output normalizer. A throwing/broken normalizer cannot erase the checkpoint. A candidate must remain exact-target, obey common/no-body provenance rules and preserve the recovered reference sequence. Core also checks that expansion of the checkpoint can still be represented as an unknown failure under the SAME SDK wire budget; otherwise it retains the already budgeted unknown checkpoint.

`normalize_result` receives the original result's auxiliary fields by const pointer; **the original primary outcome has been moved and must not be reread**. Null original means handler failure or a bare pre-submission error. The supplied primary outcome is already bounded/normalized. Construct the complete typed result while selectively repairing invalid optional fields. Independently valid auxiliary facts must survive. Ordinary malformed optional metadata is data to repair, not a reason to throw or clear the entire wrapper. Repair may downgrade success, but must preserve all common output details/references and may not upgrade failure/uncertainty or grant retry permission. Core enforces these constraints before ordinary trait validation and encoding.

If outer value/error conflict or post-handler normalization/validation/encoding fails, core retains the checkpoint and uses an unknown, nonretryable failure. It retries `normalize_result` with that failure and the same original auxiliary evidence. This permits valid auxiliary metadata to survive a malformed outer envelope or first encoding failure.

`make_failure` is ONLY the last resort for a callback violating its repair contract. It must construct a valid, encodable minimal Result retaining **every supplied effect**. It may mark auxiliary evidence unavailable, but cannot clip known primary effects, invent native IDs or make another provider call. The provider must budget its complete fallback wrapper before evidence recovery/inspection using actual wrapper overhead within the existing shared SDK budget, not a new arbitrary limit.

A provider unable to guarantee this is an integration blocker and must not be registered. If a programming error nevertheless makes the final callback/encoding fail, core returns a diagnostic outer OutcomeUnknown/PossiblySubmitted/nonretryable failure **with its retained internal completion sidecar**, so physical observations are not erased. That path is deliberately not an accepted typed-provider result or a substitute for satisfying the fallback contract; the caller cannot recover an arbitrary broken wrapper generically.

## Completion and observations

```cpp
struct OperationCompletion {
  bot::OperationReply reply;
  std::optional<bot::SendOutcome> message_outcome;
};
// OperationRegistry
boost::asio::awaitable<OperationCompletion> invoke_completed(bot::OperationEnvelope);
// Existing gateway invoke() remains and returns only completion.reply.
```

Binding and registry retain the same prepared/admitted handler ownership. The process dispatcher calls `invoke_completed`, not the reply-only compatibility entry point. A registered message operation returns its COMPLETE encoded Result in `reply`, plus the verified common outcome projection for observation. The dispatcher never decodes a provider wrapper or rewrites the caller's result.

Only complete SendReceipt AND successful reply permit logical success. Every retained confirmed output can generate the existing physical confirmation, including partial/unknown/contract-violation outcomes. Supplied correlation is captured before invocation and retained exactly; a process-unique ID is generated only for message completions with absent correlation. Common and extension sends share the same observer/executor admission-close/drain path. Callback, queue, and observation-preparation failures cannot modify the already completed reply or resubmit.

## Telegram cached sticker consumption

The owner already supplies the outcome client marker and request-aware normalize/decode trait hooks. Register only after importing this process seam:

- Context: `{request.target, nullptr}`.
- Outcome accessor: identity on SendOutcome.
- Primary normalization: `Traits::normalize_result(request, std::move(outcome))`, updated by its owner to the reviewed single-output helper as appropriate.
- Result reconstruction: return the supplied normalized outcome; no auxiliary fields exist.
- Last resort: wrap the supplied SendFailure as SendOutcome.
- Bind the direct prefix-aware executor; add its descriptor/action only after integrated compile/review.
- Public client retains its request-aware `decode_result` and handles outer malformed values/correlation; this process policy does not replace client decoding.

There are no common ContentSlice or media block indices for sticker send. Do not invent a common RichText block to validate it.

## QQ forward consumption and remaining gate

QQ owner must add `requires_outcome_client` and replace its ordinary definition with the explicit policy. Its Result remains OneBotGroupForwardMessageResult with unchanged `outcome`, `forward`, and `output_details` fields.

- Context: `{request.target, nullptr}`.
- Outcome accessor: `.outcome` by mutable reference, noexcept.
- Primary normalization: reviewed `normalize_single_message_outcome(request.target, std::move(outcome))`; no fake common body.
- Result reconstruction: take supplied primary outcome, inspect original optional QqForwardRef and typed ForwardOutputDetails using the SAME SDK budget BEFORE copies, retain each independently valid resource/node association, and reject/mark only corrupt or ambiguous fields unavailable. Associations must name confirmed exact-target outputs; node indices must be valid for the request. A corrupt optional field downgrades conservatively without discarding unrelated verified fields.
- The parent-approved validator clarification permits **sparse optional node provenance**: omitted auxiliary detail means unavailable; every physical reference already exists in `.outcome`. No JSON field/layout change is required. The minimal fallback therefore need not duplicate every confirmed reference solely to attach null provenance. Old QQ validation currently requiring one auxiliary entry per outcome entry must be updated by its owner.
- `make_failure` can use an empty auxiliary details vector/null forward resource only after that sparse validation update, and only for repair-callback failure. Ordinary metadata corruption must retain valid siblings.
- QQ must provide full-wrapper budget accounting BEFORE recovery/inspection, including the wrapper fields and any retained auxiliary metadata. Sparse provenance removes redundant reference expansion but does not waive wrapper overhead. This full-wrapper-budget consumption and its validation are an explicit **registration gate**, not claimed complete in core.
- Public QQ client/outcome decoding must use request-aware bounded wrapper recovery and explicit correlation, preserving the complete typed result. Parent/platform owner handles this separately.

The completed QQ/TG candidate trees were read only. Neither extension was registered by this runtime lane. Native execution, native-origin/account fencing follow-up, concrete source/media policy, actor migration, CMake/export integration, runtime behavior and provider acceptance remain their respective owners' gates.
