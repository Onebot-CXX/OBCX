## ADDED Requirements

### Requirement: Persistent history represents images with stable markers
Chat LLM SHALL render image-bearing messages as ordered text and stable markers such as `<image>img_opaque_id</image>`. An actor-owned attachment record SHALL associate each ID with exact platform, bot installation, conversation, topic, source message identity, attachment position, and available native file locator. It MUST NOT persist a platform-resolved temporary image URL or image bytes in the LLM DB, ordinary logs, or a local media cache. Existing message-store records SHALL remain untouched even if their original payload contains an old URL. Provider-owned opaque reasoning is governed by the durable-context protocol-state exception: it may be retained unchanged, but its internals cannot be verified as free of encoded image data and MUST NOT serve as an attachment cache.

#### Scenario: Incoming image has an expiring URL
- **WHEN** an eligible QQ image segment contains a native file locator and a URL
- **THEN** the durable model projection contains a stable image marker and trusted locator metadata, not that URL

#### Scenario: Message contains text and several images
- **WHEN** an eligible message contains text interleaved with multiple images
- **THEN** the projection preserves deterministic text/marker order and does not fetch image content merely to add history

#### Scenario: Image-only message is addressed to the bot
- **WHEN** an image-only message satisfies an existing direct trigger and permission gate
- **THEN** it can enter the model as an image marker without being rejected for empty text

### Requirement: The model explicitly requests image access through a stable tool
The normal tool set SHALL declare view_image with a stable image_id argument for direct and proactive turns. Executing the tool SHALL validate the ID against trusted attachment metadata authorized for the exact current session. A marker appearing in untrusted text SHALL NOT create an attachment or authorize arbitrary file/URL access. Tool invocation SHALL count toward existing bounded tool execution policy.

#### Scenario: Model requests an available attachment
- **WHEN** the model calls view_image for an authorized stable image ID
- **THEN** the actor resolves that attachment through the corresponding platform API for this turn

#### Scenario: Model does not need the image
- **WHEN** a reply can be produced without a view_image call
- **THEN** no image-resolution API request or image download is made for the markers in its history

#### Scenario: User fabricates a marker
- **WHEN** user text contains an invented marker or an ID belonging to another installation, group, or topic
- **THEN** the executor rejects view_image for that ID without provider I/O

### Requirement: QQ image URLs are resolved on demand rather than stored or copied from history
After an authorized view_image call, the actor SHALL use an exact-installation typed OneBot image-resolution operation to request a current provider-accessible image URL from the stable native locator. It MUST NOT rely on an expired URL saved in the original message or bypass the BotOperationGateway with arbitrary provider calls. Missing resolution capability, unsupported OneBot response shapes, and unavailable historical files SHALL be explicit failures.

#### Scenario: Source message URL has expired
- **WHEN** the model requests an old image whose source payload URL is no longer usable
- **THEN** the actor uses the native locator to request a current URL rather than replaying or persisting the old URL

#### Scenario: Platform only returns a local file path
- **WHEN** the OneBot implementation cannot return a supported public image URL
- **THEN** the actor reports the capability mismatch and does not send an arbitrary local path to the LLM or silently introduce a local downloader/cache

#### Scenario: Typed image-resolution operation is unavailable
- **WHEN** the installed SDK or platform implementation lacks the required operation
- **THEN** the actor reports the missing capability without invoking a private transport or silently modifying core; the minimal platform extension requires explicit scope approval

### Requirement: Tool execution supplies actual visual input to both providers
A successful view_image SHALL supply actual multimodal input for the immediate model continuation, not merely return a textual URL. The adapter SHALL use provider-valid image blocks: OpenAI-compatible image_url and Codex input_image. If the endpoint does not support images inside tool outputs, a runtime-generated transient user multimodal message SHALL associate the image with the tool call. The durable tool result SHALL contain only the stable image ID, outcome, and current-turn-only availability semantics.

#### Scenario: OpenAI-compatible continues after the tool
- **WHEN** a QQ image is resolved for an OpenAI-compatible request
- **THEN** the continuation includes an image_url content part pointing at the temporary URL, with a matching sanitized tool result

#### Scenario: Codex continues after the tool
- **WHEN** the same attachment is requested through Codex
- **THEN** the adapter supplies input_image in a supported input shape rather than omitting image content or returning text alone

#### Scenario: Provider cannot fetch external images
- **WHEN** the configured endpoint rejects external image URLs or does not support visual input
- **THEN** the actor reports an explicit capability failure without claiming that the model saw the image or adding an unapproved local-image fallback

### Requirement: Temporary visual inputs are confined to the current tool turn
Resolved URLs and any in-memory image bytes SHALL exist only in request-local turn state. The actor SHALL preserve the durable text/marker prefix, keep valid visual inputs ordered within the current wire tool loop, and discard them after completion or cancellation. Later turns MUST explicitly call view_image again to see pixels. Dropping transient image input between turns MUST NOT be implemented by replacing markers or rewriting old durable entries.

#### Scenario: Later user asks about the same image
- **WHEN** a later turn needs actual pixels from a retained image marker
- **THEN** the model calls view_image again and the actor requests fresh access rather than restoring a stored URL or file

#### Scenario: Process restarts
- **WHEN** a session is restored after a restart
- **THEN** text, image IDs, and sanitized tool outcomes are restored but no temporary URL or image-byte replay is attempted

#### Scenario: Current turn is cancelled
- **WHEN** cancellation interrupts image resolution or a visual continuation
- **THEN** request-local visual material is released and no temporary link is promoted into durable history

### Requirement: URL lifetime and retries are explicit and bounded
Image-resolution deadlines, image-count limits, request-size limits, URL reuse policy, and refresh-attempt bounds SHALL be explicitly configured. The implementation SHALL honor trustworthy returned expiry metadata and an explicitly configured conservative reuse bound when expiry is absent. It MUST NOT treat a typical five-minute lifetime as a hidden default or a guarantee. A refresh SHALL occur only within a bounded current-turn path and MUST NOT replay already executed side effects.

#### Scenario: URL expires before a later tool-loop request
- **WHEN** a current-turn URL is past its permitted reuse deadline
- **THEN** the actor resolves it again within the configured bound and replaces only transient visual material, leaving durable history unchanged

#### Scenario: Provider reports an expired image URL
- **WHEN** a visual request fails because image access expired
- **THEN** the actor performs only the permitted bounded refresh or reports failure, without looping indefinitely or re-executing send_message

#### Scenario: Image resource is unavailable
- **WHEN** the platform cannot retrieve a historical image
- **THEN** the tool returns a bounded unavailable result while preserving text and the stable marker, without fabricating image observations

### Requirement: Telegram follows the same lazy tool contract without leaking bot credentials
Telegram image markers SHALL also be resolved only after view_image. The actor SHALL use the existing exact-installation typed file-fetch operation with explicit byte bounds, retain returned bytes only in memory for the current turn, and supply a transient data URL or equivalent supported image block. Bot-token-bearing Telegram download URLs MUST NOT be sent to the LLM, persisted, or logged. The actor MUST NOT create a persistent image directory.

#### Scenario: Telegram image is opened
- **WHEN** view_image targets a permitted Telegram attachment
- **THEN** the typed operation fetches bounded bytes and the immediate model continuation receives a visual block without a bot-token URL

#### Scenario: Telegram payload exceeds the bound
- **WHEN** the file's declared or received bytes exceed the explicit bound
- **THEN** the tool fails within the existing typed transport limit without retaining an unbounded buffer or writing an image file

### Requirement: Temporary access links and visual bytes are excluded from durable diagnostics
The actor SHALL validate platform-returned image URLs for supported scheme and public destinations without fetching them locally, reject credential-bearing arbitrary locations, and register temporary links/bytes for request-trace redaction. Routine telemetry and inspectable durable assistant/tool serialization MUST exclude the resolved link and image payload, including echoed values. Editable fields SHALL be sanitized; an inspectable signed/integrity-protected reasoning block that echoes such values SHALL cause an explicit persistence/continuation conflict rather than be rewritten or persisted unchanged. Opaque reasoning MAY be retained under the durable-context contract without asserting internal sanitization; entire reasoning blocks SHALL be excluded from ordinary diagnostics, and opaque state MUST NOT restore image access. No actor-owned general HTTP image downloader, address-pinning transport, content-addressed image cache, or media GC SHALL be required by this capability.

#### Scenario: Request tracing is enabled
- **WHEN** a view_image request or continuation is traced
- **THEN** the trace records stable image ID, bounded status, and safe resource metadata without temporary URLs, image bytes, or bot credentials

#### Scenario: Model output echoes a temporary link
- **WHEN** a provider response includes a temporary image URL used by the current turn
- **THEN** the actor removes or replaces that value in editable fields before durable storage, or explicitly rejects persistence if the value is integrity-protected; ordinary diagnostics expose neither the link nor reasoning content
