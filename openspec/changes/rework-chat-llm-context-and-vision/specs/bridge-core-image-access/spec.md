## ADDED Requirements

### Requirement: Bridge delegates image acquisition to core
Bridge SHALL replace duplicated image URL parsing, direct client construction, bounded HTTP downloads and GIF Range-probe transport with the shared core image acquisition API. Supported native QQ image references SHALL resolve through the exact-installation typed core image resolver. Existing Telegram typed bounded fetching SHALL remain in use. Unused legacy connection-manager download paths SHALL be removed instead of retained as bypasses.

#### Scenario: A native QQ image is forwarded
- **WHEN** Bridge needs current image data for a supported QQ image resource
- **THEN** it obtains temporary access through the typed core resolver and uses core acquisition rather than implementing provider refresh or HTTP transport in the actor

#### Scenario: Configured placeholder needs acquisition
- **WHEN** Bridge chooses its configured placeholder under its existing fallback policy
- **THEN** it uses core bounded acquisition without treating the configured placeholder as a native QQ attachment

### Requirement: Bridge retains formatting and forwarding policy
Bridge SHALL retain actor-level retry/backoff configuration, order-preserving batch outcomes, MIME/GIF interpretation, byte and dimension policies, normalization, placeholder selection, failure presentation and forwarding targets. Migration SHALL NOT introduce configuration defaults, LLM persistence, message-store changes, or an unbounded download fallback.

#### Scenario: Image retrieval fails
- **WHEN** core returns an image acquisition failure
- **THEN** Bridge applies its explicit existing retry/placeholder/failure policy while preserving media order and unrelated message forwarding

#### Scenario: Core API is missing from the selected SDK
- **WHEN** Bridge is built or activated without its required image capability
- **THEN** the missing dependency is explicit rather than silently returning to a private transport implementation
