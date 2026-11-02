# Current process domain API integration

SDK/core 2.7.0 is the current-only source generation. Use the matching installed
SDK; schema and version labels alone are not a C++ ABI or runtime acceptance
claim. No deployment, restart or online migration is authorized here.

## Process/platform boundary

One registered PlatformDomainBinding per surface owns current native codecs,
normalization, observed self and explicit configured conversation resolution.
There is no archive decoder, generic reference conversion service, historical
ID import or legacy media association. Native protocol carriers remain private
implementation details, not ordinary actor SDK inputs.

Capture receive time and account/connection generation at native admission.
Named typed context and observations retain sender/profile, group/topic, edit,
reply and specialized content facts. Card metadata is content, never an archive
of an entire event. Current scoped references serialize deterministically;
restoration does not admit replies, media reads or provider history lookup.

## Services and consumers

The generation owns BotClient and ConversationResolver. Current configuration
supplies explicit kind, native ID and optional numeric topic; it does not parse
old chat:/group: keys. Platform display views consume current scoped UserRefs.
The existing QQ profile/member queries accept scoped native selectors as query
intent; only validated matching provider responses produce user observations.

Store schema 5 persists only typed MessageReceived snapshots and lookup columns.
MessageStored schema 2 carries source identity and typed message, not a native
archive. Chat schema 2 reads those snapshots. Bridge schema 5 persists scoped
members, actual route/topic facts, current send plans, receipts and command
fences. Its policy and edit-intent codecs require generation 2. Old schemas are
rejected, never upgraded in a runtime constructor.

## Evidence and ownership

ReplyEvidenceStore retains validated relations/authors only within the admitted
account generation and clears them on invalidation/close. Its total-retention
policy remains a separate unresolved design issue; there are no unapproved
capacity/TTL configuration options or replacement defaults. Preserve local-first,
evidence-bound reply behavior. Restored references cannot start a provider lookup.

OperationRegistry attaches authoritative cache invalidation before callbacks
activate. Both present and absent self observations use the generation fence.
Transport owners fence the original connection/I/O epoch before publishing;
reading the current epoch after an old callback resumes is insufficient.

Message-producing operations use outcome-aware handlers and explicit result
policies. Keep every confirmed output and unknown effect across cancellation or
errors. Only complete receipts authorize logical success. Physical confirmations
are separate observations, not MessageStored source admissions. Correlation is
not provider idempotency or retry permission. Existing process-owned notification
admission/close and shutdown drain preserve ownership through completion.

## Products and offline transition

Actor input contract schema 3, scheduler ABI V2 and manifest schema 2 remain
independent of Store/Chat/Bridge database versions. Loader rejection precedes
actor preparation, not dlopen or ELF initialization. Build core in isolation;
freeze its installed SDK and build maintained actors with scripts/actor_build.py.

The operator-requested scripts/migrate_typed_databases.py is a standalone offline
utility, not imported by SDK/runtime. It backs up, converts copies and validates
counts/integrity before replacement. Its Bridge conversion preserves typed
receipts, retry payloads and command fences, including topic 1 versus absent
topic. Unsupported historical rows are not promoted into current admissions.
The utility has not been executed by the coding assistant.
