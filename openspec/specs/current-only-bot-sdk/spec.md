# current-only-bot-sdk Specification

## Purpose
Define the current-only SDK/consumer boundary, typed persistence and explicit
offline transition without runtime historical import or renewed authority.

## Requirements

### Requirement: Current-only SDK contracts
The maintained SDK SHALL expose current typed bot contracts, not obsolete
group/private/topic messaging DTOs, MessagingClient, generic legacy reference
conversion, archive restoration, compatibility projections or historical media
imports. Current platform operations and private protocol implementation SHALL
remain separate from retired public DTO support.

#### Scenario: Consumer uses an obsolete contract
- **WHEN** source includes a retired SDK header or names a retired DTO/service
- **THEN** the contract is unavailable rather than adapted through an alias

### Requirement: Current consumers use current evidence
Message/reply/media/command/display/update processing SHALL consume current
typed source/reference evidence without historical decoding under a new name.
Ordinary behavior SHALL remain supported through current contracts. Explicit
native QQ profile/member selectors SHALL be installation-scoped query intent,
not reference-import authority; validated provider results supply user facts.
QQ/TG display views SHALL consume current scoped UserRefs only.

#### Scenario: Current message is processed
- **WHEN** a current message reaches a maintained consumer
- **THEN** processing uses its typed snapshot/context/observations rather than reconstructing an archive

#### Scenario: Native user input is queried
- **WHEN** an approved QQ query supplies a scoped native user selector
- **THEN** invalid/cross-installation input is rejected and only a matching validated provider response supplies the typed result

### Requirement: Store persists only current typed messages
Store schema 5 SHALL persist unified typed MessageReceived and lookup columns,
not another raw/native event archive or user-event snapshot. MessageStored wire
schema 2 SHALL carry source identity and typed message. Readers SHALL reject
old/partial schemas without online import, table alteration or deletion. Named
specialized observations MUST NOT hide a whole event. Structural persistence
SHALL NOT renew live media/read/reply authority.

#### Scenario: A current message is stored
- **WHEN** a normalized message is admitted to Store
- **THEN** its typed body/reference/context is stored without payload/raw event columns
- **AND** Bridge and Chat consume that same snapshot

### Requirement: Historical owner support is retired
Bridge SHALL expose no old single-output mapping, historical retry/migration or
quarantine binding APIs. Retired configuration SHALL be rejected. Current
version rejection and native protocol handling SHALL NOT act as compatibility
imports. No runtime constructor SHALL automatically convert, clear or replay
unsupported persisted data.

#### Scenario: Operator supplies old configuration or data
- **WHEN** an unsupported representation is encountered
- **THEN** it is rejected without silent import, deletion, migration or replay

### Requirement: Bridge current ledger keeps typed evidence and actual topic facts
Bridge schema 5 SHALL retain scoped members, outputs, receipts, attempt/retry
and command fences without legacy scalar source keys, old address records or
native message-ID export. Policy/EditIntent codecs SHALL require generation 2.
Actual group/topic facts SHALL preserve topic 1 versus absent topic even when
canonical scope is equal. Publication schema 2 SHALL retain typed source/target/
outputs and optional quote anchors; unknown effects SHALL remain unknown.

#### Scenario: Retry route is selected
- **WHEN** a persisted zero-effect eligible attempt is considered under current configuration
- **THEN** exact source/target/pair and actual topic facts are checked without old-key decoding or remapping the target

#### Scenario: Old ledger is presented
- **WHEN** a schema-4 ledger reaches schema-5 runtime startup
- **THEN** startup rejects it rather than converting it

### Requirement: Offline conversion stays outside runtime authority
The standalone operator utility SHALL back up and convert copies before any
replacement, accept already-current namespaces without reconversion, and retain
row counts, receipts, retry payloads and command fences. Unsupported historical
rows MUST NOT become new admissions. It SHALL perform no provider operations,
media fetch, process start or restart. Writing/building the utility SHALL NOT be
reported as successful data migration or runtime acceptance.

#### Scenario: Explicit offline conversion is prepared
- **WHEN** the operator reviews a stopped-writer conversion plan
- **THEN** all paths/namespaces are explicit and original files remain unchanged until every candidate conversion and integrity/count/binding check succeeds

### Requirement: Isolated matching products
Core SHALL be built separately from the running process. Actor builds SHALL
use scripts/actor_build.py and a frozen matching installed SDK. SDK 2.7.0 source
floors and exact binary identities SHALL remain distinct from provider/runtime
acceptance; deployment and process restart require separate authorization.

#### Scenario: Actor is compiled after the SDK change
- **WHEN** maintained actor products are built
- **THEN** the helper verifies the frozen SDK and cannot transitively rebuild or replace the live core
