## ADDED Requirements

### Requirement: Message-store remains an unchanged read-only source
Chat LLM SHALL consume the existing message-store schema and MessageStored event without changing that service's code, schema, tables, indexes, interfaces, event format, or behavior. Chat LLM MUST NOT perform source DDL/DML, add source pins, require a source migration, or constrain source deletion with a mandatory foreign key.

#### Scenario: LLM storage is initialized
- **WHEN** Chat LLM initializes its own state schema against an existing message-store deployment
- **THEN** only its own namespace is changed and message-store data, schema, and writer behavior remain unchanged

#### Scenario: Source schema is unsupported
- **WHEN** the configured source does not expose the supported existing message-store schema
- **THEN** the reader reports an actionable incompatibility without upgrading, repairing, or recreating source tables

### Requirement: LLM-owned databases use core storage interfaces
Chat LLM SHALL access its state DB and source DB through core DbManager and IDbConnection operations, with synchronous work dispatched through BlockingExecutor. State schema changes SHALL use the core migration mechanism only for the actor-owned namespace. Chat LLM MUST NOT open a private sqlite3 connection or issue sqlite3 C API calls.

#### Scenario: Session state is written
- **WHEN** a turn commits journal entries, a cursor, and stable attachment references
- **THEN** the actor uses one core-managed transaction in its configured state DB, without a transaction or write in the source DB

#### Scenario: LLM uses a separate database file
- **WHEN** explicit configuration assigns different core DB instances to state and message source
- **THEN** both repositories operate through their respective core interfaces without requiring a shared file or a new core API

### Requirement: Message references identify the exact source record
A MessageRef SHALL identify the configured source DB binding and namespace and the complete unique key of platform, bot installation, conversation, and native message ID. A platform-table primary-key id SHALL be treated only as a scoped lookup accelerator and MUST be checked against the complete identity. Topic eligibility SHALL be checked using existing source payload semantics; null topic MUST mean topic-less, not all topics. Query values MUST be bound parameters and identifiers MUST be validated.

#### Scenario: Native IDs collide
- **WHEN** two bots, groups, or platforms contain the same native message ID or table-local numeric id
- **THEN** a reference resolves only the exact configured source and conversation, never another row with a matching partial key

#### Scenario: Telegram topic differs
- **WHEN** a candidate source row belongs to a different topic from the target LLM session
- **THEN** it is excluded even when the group and bot match

#### Scenario: Raw is represented by payload
- **WHEN** an existing v4 source row stores an empty SQL raw string
- **THEN** the reader reconstructs raw from payload while preserving the distinction from JSON null, empty object, and empty JSON string

### Requirement: Logical references support same-database joins and separate-database merging
Chat LLM SHALL support same-instance key-based joins and separate-instance reads merged by MessageRef with equivalent results. Real SQL foreign keys, SQLite ATTACH, cross-database transactions, and source-side schema changes MUST NOT be prerequisites. Unresolved references MUST be explicit, not silently matched to a different message.

#### Scenario: State and source share an instance
- **WHEN** local references and source records are queried in the same core DB instance
- **THEN** the result can be joined by the exact source identity without changing source tables

#### Scenario: State and source are separate
- **WHEN** the equivalent references are resolved using a separate source DB instance
- **THEN** bounded core reads and in-memory merging produce the same scoped records without relying on an enforced foreign key

### Requirement: Source consumption is bounded and incremental rather than a rolling context window
The reader SHALL bootstrap a new session from a fixed source high-water mark within the configured token budget, then read new rows using existing insertion ids and bounded pages. The actor SHALL advance its local cursor only with a successful local journal commit. An I/O page size MUST NOT become a recent-message context limit. Existing upserts MUST NOT be misrepresented as a durable source revision feed.

#### Scenario: More than one page arrives
- **WHEN** eligible messages exceed one configured fetch page
- **THEN** the actor continues bounded pagination without dropping the beginning of its committed model context

#### Scenario: Timestamps repeat or arrive late
- **WHEN** new source rows have equal or older provider timestamps than previously consumed rows
- **THEN** they are consumed by source insertion identity and appended locally without reordering committed input

#### Scenario: Local commit fails
- **WHEN** source rows have been read but the local journal transaction fails
- **THEN** the local cursor is not advanced and a retry deduplicates by source identity and observed content fingerprint

### Requirement: Frozen model projections are session state rather than a duplicate archive
Chat LLM SHALL materialize only normalized text and stable image-reference markers needed by retained sessions, alongside source references and rendering fingerprints. It MUST NOT create a second full raw/payload archive or persist platform-resolved temporary image URLs or image bytes. A source update or deletion MUST NOT rewrite an already committed projection; a newly observed change SHALL be represented as a later observation if admitted to context. A stable attachment record SHALL retain the exact platform identity and native file locator needed for on-demand resolution, independently of any expired URL in the source payload.

#### Scenario: Source row changes after a turn
- **WHEN** a referenced source message is edited after its input has been committed
- **THEN** replay uses the frozen projection, and a later observed edit does not mutate the old prefix

#### Scenario: Source row is deleted
- **WHEN** a source reference no longer resolves
- **THEN** retained text/image-marker projections remain replayable while an unmaterialized reference is reported missing without source repair or fabrication; markers do not guarantee that the platform can still retrieve the image

### Requirement: Commands and outbound receipts do not require source backfill
Ordinary chat processing SHALL consume the existing MessageStored event and only read its source. Authorized consumed chat commands and confirmed bot receipts without source rows SHALL be stored as local LLM events with optional MessageRefs. Chat LLM MUST preserve command ACL and Consume/Continue semantics and MUST NOT manufacture ordinary inbound events or backfill the source to make these associations exist.

#### Scenario: Chat command is consumed
- **WHEN** an authorized ChatCommand has no message-store row because ordinary pipelines are bypassed
- **THEN** its user input remains in the local conversation for later turns, the command still selects Consume, and no source write occurs

#### Scenario: Bot reply later appears as an echo
- **WHEN** a confirmed outbound receipt is later observed through MessageStored with the same exact platform identity
- **THEN** the local record is associated without duplicating the assistant reply or triggering another response

### Requirement: Legacy history storage is retired without importing or deleting it at startup
Chat LLM SHALL reject obsolete history_db_path, history_limit, and history_ttl_days with migration guidance and SHALL neither read, import, nor create the old messages_v2 history DB. A new core-managed LLM state DB SHALL remain supported. Reload-candidate preparation and ValidationOnly MUST NOT create state tables; only explicit startup MAY initialize the actor-owned schema. No preparation or validation path SHALL download media, run cleanup, or read/delete legacy history files.

#### Scenario: Legacy history is present on disk
- **WHEN** a new explicitly configured actor starts with an unused old history.db present
- **THEN** it does not inspect or import that history, does not delete the file automatically, and uses its core-managed state DB

### Requirement: Validation-only performs configuration checks without actor database access
Chat LLM's ValidationOnly path SHALL validate explicit options, declared DB binding names, local profile/tokenizer/prompt resources, fixed-request budgets, and static provider contracts without obtaining DB services, constructing repositories, or reading schema, source messages, or session state. It SHALL NOT require a runtime BlockingExecutor or live operation gateway, create its own executor, or perform remote capability checks. Local Codex credential validation SHALL NOT refresh credentials. Passing ValidationOnly SHALL NOT assert database availability or schema compatibility. Actual source/state schema checks SHALL run only at startup/reload through the existing core BlockingExecutor. This actor guarantee SHALL NOT be represented as changing the core CLI's pre-existing opening of configured DB instances; no core change is required.

#### Scenario: Configured schema is absent or unsupported during validation
- **WHEN** ValidationOnly checks an otherwise valid configuration against declared DB instances whose source/state schema is absent or unsupported
- **THEN** Chat LLM performs no DB calls and does not fail on schema contents, initialize tables, or claim that schema compatibility was verified

#### Scenario: Static resources or binding names are invalid
- **WHEN** ValidationOnly encounters an undeclared source DB binding, invalid profile/vocabulary, or an impossible fixed-request budget
- **THEN** it rejects the configuration without querying either DB or contacting a provider

#### Scenario: Startup encounters an unsupported source schema
- **WHEN** startup follows successful configuration-only validation but the source schema is unsupported
- **THEN** the core-managed read-only probe rejects activation without repairing the source or initializing session tables

#### Scenario: State initialization is needed during reload preparation
- **WHEN** a reload candidate requires creating or changing persistent state rather than validating an existing compatible schema
- **THEN** it reports the required explicit startup or restart action without mutating either active state or message-store
