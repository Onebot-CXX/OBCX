# http-response-body-limits Specification

## Purpose
Define finite, configurable HTTP response-body limits that apply consistently across OBCX clients, transports, and operations.

## Requirements
### Requirement: HTTP responses have a finite default body limit
OBCX `HttpClient` SHALL apply an 8 MiB response-body limit when a caller does not configure another limit. The limit MUST apply without disabling normal timeout, TLS, proxy, header, or decompression behavior.

#### Scenario: Default client receives a response within the limit
- **WHEN** an HTTP response body is no larger than 8 MiB and the response is otherwise valid
- **THEN** the client completes the response read normally

#### Scenario: Default client receives a response above the limit
- **WHEN** an HTTP response declares or streams a body larger than 8 MiB
- **THEN** the client aborts the response read with `HttpClientError` instead of buffering the complete body

### Requirement: Callers can select a positive per-client body limit
OBCX `HttpClient` SHALL allow a caller to set a finite positive response-body limit before issuing a request. The selected limit MUST remain scoped to that client instance and MUST NOT change the default or another client instance.

#### Scenario: Media client opts into a larger finite limit
- **WHEN** a caller configures a 10 MiB limit and receives a valid 9 MiB response
- **THEN** that client accepts the response while an unconfigured client retains the 8 MiB default

#### Scenario: Response exceeds the selected limit
- **WHEN** a client configured for a finite limit receives a response one or more bytes above that limit
- **THEN** the client aborts the read with `HttpClientError`

#### Scenario: Caller selects an unlimited-equivalent value
- **WHEN** a caller attempts to configure a zero-byte response limit
- **THEN** the client rejects the setting and does not interpret it as unlimited

### Requirement: Response limits are consistent across transports and operations
The configured response-body limit SHALL govern every response-bearing asynchronous and synchronous direct or proxy HTTP operation. A transport or operation MUST NOT silently fall back to Beast's implicit limit or an unlimited parser.

#### Scenario: Direct and proxy GET responses use the selected limit
- **WHEN** equivalent direct and proxied GET responses exceed their clients' configured limit
- **THEN** both reads fail under the same response-limit policy

#### Scenario: POST response uses the selected limit
- **WHEN** a POST response exceeds the configured response-body limit
- **THEN** the response read fails even if the request body was accepted by the remote endpoint

#### Scenario: Chunked response crosses the limit
- **WHEN** a response has no known `Content-Length` and decoded body octets cross the selected parser limit
- **THEN** the client stops reading and reports `HttpClientError`

### Requirement: Decoded responses respect the selected finite body limit
The selected OBCX per-request or per-client response-body limit SHALL apply to decoded response bytes as well as wire body bytes for direct and proxy operations. Decoding MUST abort before appending bytes beyond that limit, regardless of Content-Length or compressed frame size declarations. An over-limit or malformed encoded response MUST produce an HTTP client error rather than returning a truncated successful body or silently returning compressed bytes. This requirement introduces no new configuration default and does not change the existing selection precedence.

#### Scenario: Small compressed body expands beyond the selected bound
- **WHEN** a compressed response fits within the wire-body limit but its decoded body would exceed the selected limit
- **THEN** the client aborts decoding without buffering the complete expansion and reports an HTTP client error

#### Scenario: Decoded response exactly meets the limit
- **WHEN** a valid compressed response satisfies both wire and decoded limits and its decoded size equals the selected bound
- **THEN** the client returns the complete decoded body normally

#### Scenario: Streaming response has no declared size
- **WHEN** a chunked or unknown-length encoded response expands beyond the selected decoded bound
- **THEN** the streaming collector rejects it at the bound rather than relying on response size metadata

#### Scenario: Encoded response is malformed
- **WHEN** a supported compressed response fails decoding
- **THEN** the client reports an error instead of silently treating the encoded bytes as successfully decoded content
