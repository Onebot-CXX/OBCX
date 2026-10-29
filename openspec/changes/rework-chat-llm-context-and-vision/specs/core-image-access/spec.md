## ADDED Requirements

### Requirement: Stable QQ image resources exclude temporary authorization
Core SHALL provide a validated OneBot image reference containing the exact installation and native file identifier accepted by get_image. It SHALL be persistable without a signed URL, rkey, token, image bytes or arbitrary local path. References SHALL be extracted from trusted platform image segments, not model-supplied URLs. Invalid or unsupported native identifiers SHALL fail explicitly.

#### Scenario: LLOneBot emits a native image filename
- **WHEN** a trusted image segment contains file and a temporary url
- **THEN** extraction retains the native file identifier and installation, not the temporary url

#### Scenario: File is a URL or local path
- **WHEN** a segment file field contains a URL or a filesystem path rather than a supported native identifier
- **THEN** extraction rejects it instead of persisting credentials or inventing a stable identity

#### Scenario: Arbitrary endpoint is not an image locator
- **WHEN** extraction receives an unrecognized host, credentials, a local file path or unsupported query fields
- **THEN** it rejects the locator without making a request or exposing the input in diagnostics

### Requirement: OneBot image retrieval uses the standard action and scoped typed operation
Core SHALL expose typed get_image through BotOperationGateway, bound to the exact installation and native image reference. The download policy SHALL be explicit. URL-only LLOneBot consumers SHALL request download:false and use the validated optional URL extension without downloading or reading the returned server path. Standard file-path-only results SHALL remain distinguishable from URL-capable results. Metadata parsing SHALL reject malformed sizes and unsupported URLs, omit base64 and unrelated provider fields, and validate result/request identity. Core SHALL NOT implement its own rkey management or signed-URL construction, invent expiry, or bypass the typed gateway.

#### Scenario: A historical resource is viewed again
- **WHEN** an authorized actor resolves its retained resource locator
- **THEN** core calls get_image on the configured installation and returns its validated result rather than reusing the source segment URL; this does not guarantee platform retention or that the platform actually refreshed an embedded expired signature

#### Scenario: Provider capability is absent or malformed
- **WHEN** get_image fails, returns malformed metadata, or a URL-only consumer receives only a server file path
- **THEN** the operation or consumer reports explicit failure without reading that path, substituting the old segment URL, or inventing a lifetime

#### Scenario: LLOneBot supports metadata-only retrieval
- **WHEN** an authorized consumer requests an image URL without local materialization
- **THEN** the request explicitly includes download:false and no OBCX image cache or rkey action is used

### Requirement: Core provides shared bounded HTTP image acquisition
Core SHALL expose the shared direct HTTP/HTTPS acquisition primitive used by Bridge image downloads and Range probes. Requests SHALL explicitly provide timeout, response-body byte limit and probe/range intent; the byte cap SHALL apply during receipt even when a server ignores Range. Parsing and transport failures SHALL be safely classified without credential-bearing diagnostics. Redirect behavior SHALL be explicit and SHALL NOT silently broaden the accepted destination policy. This primitive SHALL NOT impose download or persistence on lazy chat_llm QQ vision.

#### Scenario: Server ignores a one-byte probe
- **WHEN** a server sends a full image despite a Range probe
- **THEN** the configured response cap still bounds received data and an over-limit result is explicit

#### Scenario: Core does not own Bridge forwarding policy
- **WHEN** acquisition returns a status or transport failure
- **THEN** the actor decides retries, placeholders, image normalization and forwarding behavior

### Requirement: Image interface changes ship through an isolated SDK generation
New public image APIs and their dependencies SHALL be exported by the SDK. Core changes SHALL be built in an isolated non-live directory, installed into a new frozen SDK, and consumed by actor-only builds. Existing message-store and DbManager SHALL remain unchanged. Source implementation SHALL NOT imply deployment, restart or replacement of existing installed SDK generations.

#### Scenario: Actor consumes the image API
- **WHEN** Bridge or chat_llm is built against the new image interface
- **THEN** its core dependency is the imported frozen SDK and no actor build rebuilds or overwrites live core
