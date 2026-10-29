## ADDED Requirements

### Requirement: Context capacity is explicit and accepts decimal k notation
Chat LLM SHALL require context_window_tokens and all applicable output-reserve, compaction, summary, and token-accounting settings explicitly. Token counts SHALL accept positive TOML integers or positive integer strings suffixed with k or K, interpreted with multiplier 1000. Missing settings, zero, negatives, fractions, unknown units, overflow, and unsupported types MUST be rejected rather than defaulted.

#### Scenario: Operator configures 128k
- **WHEN** context_window_tokens is set to "128k"
- **THEN** the validated capacity is exactly 128000 tokens and is neither a byte limit nor a message-count limit

#### Scenario: Operator supplies an integer
- **WHEN** context_window_tokens is set to integer 128000
- **THEN** it is equivalent to "128k"

#### Scenario: Invalid or missing capacity
- **WHEN** the value is omitted, fractional, non-positive, overflowing, or uses an unsupported suffix
- **THEN** candidate validation fails with no implicit capacity selected

### Requirement: Budget relationships are validated before activation
Configuration SHALL require `compaction_target_tokens < compaction_trigger_tokens <= context_window_tokens - response_reserve_tokens`, a positive recent_turns_to_keep, and explicit summary output/time limits. The fixed prefix and summary policy MUST have usable input capacity. No component SHALL silently lower retained-turn requirements or infer model capacity from a model-name substring.

#### Scenario: Reserve consumes the window
- **WHEN** the reserve or thresholds leave no valid prompt budget
- **THEN** the candidate is rejected before model requests or mutation of active sessions

#### Scenario: Required accounting profile is unknown
- **WHEN** the explicit token_accounting_profile is unsupported for the configured provider/model policy
- **THEN** validation reports an unsupported profile instead of using a hidden tokenizer or image-cost fallback

### Requirement: Every model request accounts for the complete multimodal input
Before each normal, tool-followup, retry, or summary request, Chat LLM SHALL account for system instructions, tools, message framing, retained text/image markers, current policy, replayed reasoning/continuation units, any actually attached transient images, tool results, and output reserve. The versioned profile SHALL account for plaintext reasoning and opaque protocol overhead according to the actual endpoint contract, without treating encrypted bytes as ordinary text tokens or assuming unknown overhead is zero. An unresolved marker SHALL be counted as text rather than as downloaded pixels. The configured versioned accounting profile SHALL distinguish exact text tokenization from conservative image/protocol estimates and SHALL define an explicit safe image bound when dimensions are unavailable without fetching. Known over-budget requests MUST NOT be sent. Only provider-supported output-control fields SHALL be emitted.

#### Scenario: Tool result grows the prompt
- **WHEN** a lookup result makes the next tool round exceed capacity
- **THEN** the actor re-evaluates the full budget and refuses an invalid request rather than assuming the initial turn's count still applies

#### Scenario: Viewed images consume context
- **WHEN** view_image resolves images for the next provider request
- **THEN** their conservative image costs and the total serialized request-size bound participate in admission before that visual request is sent

#### Scenario: Unopened image stays a marker
- **WHEN** history contains an image marker but the model has not requested its pixels for this turn
- **THEN** only its textual/reference cost is charged and no platform lookup or image download is performed for token counting

#### Scenario: Provider lacks an output-limit parameter
- **WHEN** the selected endpoint does not support a particular output-limit field
- **THEN** the adapter does not send that field and separately reports that local reserve is not a server-enforced limit

### Requirement: Compaction happens at token thresholds and preserves complete recent turns
Below the trigger, sessions SHALL append without trimming old entries. At the trigger, compaction SHALL summarize an older prefix while retaining the configured number of recent complete turns, current input, and unresolved actions. A tool call and its results MUST NOT be split across retained and discarded halves. A new summary MUST NOT be regenerated on every ordinary turn.

#### Scenario: Large message count remains within budget
- **WHEN** a session has many short messages but its complete input is below the trigger
- **THEN** it remains in the same epoch without count-based trimming or summarization

#### Scenario: Trigger is reached
- **WHEN** the predicted input reaches compaction_trigger_tokens
- **THEN** the actor selects whole older turns and verifies that summary plus retained tail and pending input can fit compaction_target_tokens

#### Scenario: Recent turns cannot fit
- **WHEN** a current input or required retained turn is too large even without the older prefix
- **THEN** the actor reports a capacity failure rather than truncating that input or silently reducing recent_turns_to_keep

### Requirement: Summarization is bounded and cannot execute conversation instructions
Summarization SHALL use a separate bounded, read-only model request with no executable action tools. Source messages, images, and tool data MUST remain untrusted content; summary prose MUST NOT become system policy or authority for reply/user IDs. Summary input itself SHALL fit its budget, using bounded complete-turn chunking when necessary. It SHALL use durable text, prior observations, and stable image IDs without automatically resolving all historical images. Any carried image IDs MUST be validated against trusted pre-compaction attachment metadata rather than invented by summary prose.

#### Scenario: Old content requests a send
- **WHEN** the source of a summary contains instructions to call send_message
- **THEN** summarization cannot dispatch any bot operation or follow that instruction as policy

#### Scenario: Summary source exceeds one request
- **WHEN** the selected old prefix cannot fit a single summary request
- **THEN** the actor summarizes bounded complete-turn chunks rather than issuing an oversized request

### Requirement: Compaction preserves valid reasoning units and retires them explicitly
Compaction SHALL treat reasoning and its dependent assistant/tool messages as complete protocol units. Reasoning belonging to the retained recent tail SHALL remain intact and compatible; reasoning belonging to the summarized prefix SHALL retire from the new epoch's active requests with that prefix. The summary request SHALL use conversation text, final answers, and tool observations, not reinterpret raw reasoning as ordinary text or attempt to decrypt or summarize opaque continuation. A summary MUST NOT impersonate a provider-native reasoning block. If an endpoint dependency prevents a valid retained-tail request, compaction SHALL fail explicitly instead of splitting, rewriting, silently dropping required blocks, or reducing recent-turn retention. The reasoning-retention boundary SHALL commit atomically with the summary and epoch; failures SHALL leave old reasoning available and eventual cleanup SHALL affect only actor-owned state.

#### Scenario: Recent turns contain signed thinking
- **WHEN** older turns are summarized while recent turns retain signed thinking and tool results
- **THEN** the retained blocks and signatures remain unchanged in their required positions and the summarized prefix's reasoning is not replayed into the new epoch

#### Scenario: Reasoning dependency crosses the proposed boundary
- **WHEN** a proposed compaction would leave a required continuation block outside the retained protocol unit
- **THEN** the actor selects a valid complete boundary within the configured budget or rejects compaction without sending a malformed request

#### Scenario: Compaction transaction fails
- **WHEN** the candidate summary or its atomic commit fails
- **THEN** the prior epoch and its complete reasoning remain available without partial retirement or premature deletion

### Requirement: Compaction commits atomically and preserves state on failure
The new frozen summary, epoch, retained journal boundary, and stable attachment-reference changes SHALL commit in one state-DB transaction. Until success, the old epoch and its reference metadata SHALL remain available. Failure MUST NOT fall back to rolling deletion, silently drop images, or initiate unbounded retries. An uncompressed request SHALL proceed only if it still fits the hard input-plus-reserve budget.

#### Scenario: Summary succeeds
- **WHEN** a valid summary and retained tail meet the target budget
- **THEN** one atomic epoch transition occurs and subsequent requests append to the new frozen prefix

#### Scenario: Summary or transaction fails
- **WHEN** summarization times out, returns unusable content, remains too large, or fails to commit
- **THEN** the prior journal remains intact and no over-budget model request is sent

#### Scenario: Summary retains an image reference
- **WHEN** an old image-bearing turn is replaced by an accepted summary carrying a validated stable image ID
- **THEN** the new context distinguishes summarized observations from current visual input and the model can request view_image for that ID without reusing an old URL

### Requirement: Budget and cache telemetry distinguishes estimates from provider facts
The actor SHALL record separately estimated input tokens, provider-reported input/output tokens, and provider-reported cached input tokens, together with safe epoch/prefix and compaction diagnostics. Missing usage SHALL be unknown, not zero. The implementation MUST NOT claim cache hits solely from prefix stability or expose message content, image bytes, or credentials through routine metrics.

#### Scenario: Provider reports cached tokens
- **WHEN** a completed response includes a supported cache-usage field
- **THEN** the actor records its actual value and source independently from its input estimate

#### Scenario: Provider omits usage
- **WHEN** no supported usage fields are returned
- **THEN** telemetry reports them as unavailable without claiming a miss or a hit

#### Scenario: Only local request validation was run
- **WHEN** acceptance compares serialized request prefixes without calling a real provider
- **THEN** the report distinguishes verified durable-prefix stability from unverified provider cache effectiveness and separately accounts for temporary visual-suffix changes
