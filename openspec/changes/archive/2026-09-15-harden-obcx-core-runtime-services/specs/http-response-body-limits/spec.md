## ADDED Requirements

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
