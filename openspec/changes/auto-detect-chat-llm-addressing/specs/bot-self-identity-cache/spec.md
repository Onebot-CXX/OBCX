## ADDED Requirements

### Requirement: Self identity is obtained from the exact platform installation
Both platforms SHALL expose the same bot.self.get / GetSelfRequest / SelfInfo contract. This operation SHALL only read the runtime cache and MUST NOT perform or wait for provider I/O, including on cache miss. The runtime SHALL obtain self identity in the background through QQ get_login_info or Telegram getMe, scoped to the full BotInstallationRef and installation/account generation. It SHALL cache native user ID and available username/display name independently of LLM session history. Nicknames MUST NOT establish account identity or override the user-authored persona.

#### Scenario: Multiple bots share a display name
- **WHEN** two installations return the same display name
- **THEN** their cached identities remain separate and a message targeting one is not classified as targeting the other

#### Scenario: Profile data reaches the runtime
- **WHEN** a self query succeeds
- **THEN** validated native identity becomes available without exposing credentials or repeating the profile in every model message

### Requirement: Cache refresh follows lifecycle and coalesces concurrent work
The runtime SHALL load self identity when an installation becomes available and invalidate/refresh it after reconnect, account replacement, or an explicit successful runtime reload refresh. It SHALL refresh periodically using connection.self_identity_refresh_interval_ms, with the explicitly user-approved default of 300000 milliseconds and positive integer overrides. Refresh completions SHALL schedule the next interval. Concurrent refresh requests SHALL coalesce to one in-flight provider lookup per installation; cached queries SHALL not request refresh. Validation-only or rejected reload candidates MUST NOT mutate the live cache. Late results from an invalidated generation MUST NOT be published.

#### Scenario: Many sessions receive messages concurrently
- **WHEN** several groups request self identity while its lookup is in flight
- **THEN** each immediately receives a cached snapshot or an unavailable result and none starts an additional platform call

#### Scenario: Account changes while a request is outstanding
- **WHEN** a lookup started before account replacement completes afterward
- **THEN** its result is rejected and cannot supply the new generation's ID or username

#### Scenario: Candidate preparation fails
- **WHEN** a reload candidate is rejected before activation
- **THEN** the active identity cache is unchanged

### Requirement: Identity failures remain explicit and bounded
Cache state SHALL distinguish ready, refreshing and unavailable. A failed background refresh SHALL retain the last successful snapshot only within the same account/connection generation; an empty or invalidated cache SHALL return an explicit unavailable error. Provider reads SHALL use existing operation budgets and cancellation, retrying only on the configured refresh schedule or lifecycle refresh. Authoritative ingress self IDs can support ID-based recognition, but missing or conflicting username evidence MUST NOT be guessed or replaced with a nickname/config fallback. No configuration default other than the user-approved 300000ms refresh interval SHALL be introduced.

#### Scenario: Cache has not been populated
- **WHEN** GetSelfRequest is invoked before the first successful refresh
- **THEN** it immediately returns unavailable without making a provider request

#### Scenario: Refresh interval is overridden
- **WHEN** connection.self_identity_refresh_interval_ms is explicitly set to a positive integer
- **THEN** the background schedule uses that interval instead of 300000ms, while invalid types or non-positive values fail configuration validation

#### Scenario: Telegram getMe is unavailable
- **WHEN** the API fails and a textual username mention has no verified self username
- **THEN** its relationship is unknown while explicit commands and otherwise eligible ambient assessment remain usable

#### Scenario: Ingress identity conflicts with the cached account
- **WHEN** authoritative self ID evidence disagrees with cached identity
- **THEN** the cache is invalidated and the stale username is not used to classify mentions

### Requirement: Manual identity fields are retired without silent fallback
The actor SHALL no longer require bot_username, bot_names or bot_nickname. Present legacy fields SHALL be type-checked and generate a fixed retirement diagnostic, but their values MUST NOT affect identity recognition. New example and deployment configurations SHALL omit these fields.

#### Scenario: Legacy username disagrees with the API
- **WHEN** a configuration contains a valid string bot_username different from the verified account username
- **THEN** API identity governs recognition and the legacy value has no effect

#### Scenario: Legacy field has the wrong type
- **WHEN** a retired username/nickname is not a string or bot_names is not an array of strings
- **THEN** configuration validation reports the invalid legacy type
