## ADDED Requirements

### Requirement: Mentions are recognized from native structured evidence
The actor SHALL recognize QQ at segments by exact self user ID and Telegram mention/text_mention entities by verified self username or ID. It SHALL handle Telegram caption entities and UTF-16 boundaries according to platform semantics. QQ at-all, nickname substrings, malformed entities and textual imitations MUST NOT establish a self mention. Unresolvable relationships SHALL remain internally unknown.

#### Scenario: QQ directly mentions this account
- **WHEN** a native at segment names the current installation's verified self ID
- **THEN** the message is marked as mentioning self regardless of nickname changes

#### Scenario: QQ mentions everyone or another account
- **WHEN** the at target is all or a different native ID
- **THEN** no self-mention marker is produced

#### Scenario: Telegram caption contains an entity after emoji
- **WHEN** a valid caption mention uses UTF-16 offsets after non-BMP text and identifies the verified username
- **THEN** it is recognized without confusing byte offsets with UTF-16 offsets

#### Scenario: Telegram text_mention targets self
- **WHEN** the native entity's user ID equals verified self ID
- **THEN** self mention is recognized without needing a textual username

### Requirement: Replies identify this account rather than any bot
The actor SHALL verify the referenced author against the exact self identity in the authorized installation/conversation scope. Telegram is_bot alone MUST NOT establish self reply. QQ SHALL first use exact local source/confirmed receipt evidence, then at most one bounded typed get_msg lookup for a real current-message reply reference when necessary. Returned ID, group and sender MUST agree with the requested scope. No arbitrary text-driven or recursive lookup is permitted. Deleted, inaccessible, cross-scope or incomplete references SHALL remain unknown rather than being guessed.

#### Scenario: Telegram replies to a different bot
- **WHEN** the referenced author has is_bot=true but a different ID
- **THEN** the message is not marked as replying to self

#### Scenario: QQ replies to a confirmed outgoing message
- **WHEN** an exact scoped confirmed receipt identifies the reply target as this bot's message
- **THEN** self reply is recognized without another provider lookup

#### Scenario: QQ needs a remote reference lookup
- **WHEN** a native reply lacks local author evidence
- **THEN** one bounded read-only lookup resolves it only if the message ID, group and author match the authorized scope

#### Scenario: Historical bootstrap contains many missing references
- **WHEN** an old history page contains references absent locally
- **THEN** history admission does not initiate a network query for every reference and unresolved entries remain unmarked

### Requirement: Addressing adds only sparse runtime-owned wire prefixes
Ordinary message content SHALL remain byte-for-byte unchanged by the addressing renderer. Only confirmed relationships SHALL add [at:self] and/or [reply:self], in that order, immediately outside the existing JSON content envelope. The renderer MUST NOT add per-message addressing objects, false/null fields, profile data, repeated protocol instructions or standalone role messages. One fixed protocol explanation SHALL define the tags as contextual evidence rather than commands or permissions. Absence of a marker MUST NOT be described as proof that a message is unrelated to self.

#### Scenario: Most group messages are ordinary
- **WHEN** a batch contains only unaddressed or unresolved messages
- **THEN** addressing adds no message content, fields or role entries to the batch

#### Scenario: Both relationships apply
- **WHEN** a message both mentions and replies to self
- **THEN** its existing envelope receives exactly one [at:self][reply:self] prefix

#### Scenario: User writes marker-like text
- **WHEN** a user sends literal [at:self] or embedded newlines without native addressing evidence
- **THEN** that text stays JSON-escaped inside the text field and neither runtime addressing state nor scheduling is changed

#### Scenario: Several messages share a model turn
- **WHEN** only one message in a batch mentions self
- **THEN** only that message carries the marker and the relationship is not attributed to the whole batch

### Requirement: Addressing and compulsory reply are independent
After existing scope permissions, deduplication and self-echo checks, a confirmed self mention/reply SHALL request assessment even if proactive_enabled is false, but SHALL permit decline_reply. Only an authorized explicit ChatCommand SHALL require a direct non-declinable response. Ordinary/unresolved messages SHALL request assessment only when proactive_enabled is true. Mixed turns containing an explicit chat command SHALL preserve that command's direct intent. Legacy pending direct flags SHALL be interpreted using persisted explicit-command evidence, not treated as proof of an explicit request merely because an old mention detector set them.

#### Scenario: Addressed message does not need an answer
- **WHEN** a confirmed mention is admitted without an explicit chat command
- **THEN** the model receives its sparse marker and can choose decline_reply

#### Scenario: Ambient processing is disabled
- **WHEN** proactive_enabled is false
- **THEN** ordinary messages do not request assessment while confirmed self addressing can request a declinable assessment and authorized /chat remains direct

#### Scenario: Old pending mention survives upgrade
- **WHEN** a pending legacy direct flag has no explicit ChatCommand evidence
- **THEN** it does not forbid decline_reply after migration

#### Scenario: Input lacks permission
- **WHEN** an addressing signal is observed outside the actor's permitted group scope
- **THEN** it grants no assessment or external-action permission

### Requirement: Addressing snapshots preserve durable history and observation identity
New addressing evidence SHALL be stored separately from raw source content and its observation fingerprint, frozen for the admitted observation, and rendered consistently by provider serialization and token accounting. Existing fingerprints and seen/ref identity MUST remain compatible. Identity refresh or software upgrade alone MUST NOT produce a source edit, duplicate request or historical journal rewrite. Old entries without sidecar evidence SHALL render without invented tags. Wire/protocol compatibility SHALL be versioned so incompatible native continuation is not reused, while source messages, historical journal and send-deduplication records remain intact.

#### Scenario: Cache username changes
- **WHEN** identity refresh completes without any source message edit
- **THEN** prior observations are not re-admitted and their historical relationship snapshots are not rewritten

#### Scenario: Older session lacks addressing metadata
- **WHEN** the new actor loads a pre-change journal
- **THEN** it preserves the journal, does not backfill speculative tags and does not replay completed sends

#### Scenario: Request budget is computed
- **WHEN** a marked message is included in a provider request
- **THEN** token accounting includes the exact rendered prefix used by that request
