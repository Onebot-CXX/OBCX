# QQ media sources

## Current evidence boundary

SDK2.5.18 core/app and all six maintained actors have been built against its
frozen installed SDK. Evidence: `build/domain-sdk-2.5.18-current/acceptance-scope.md`
in the core workspace. Limited C ABI metadata/schema3 discovery and offline
configuration checks are recorded separately in `runtime-acceptance-scope.md`.
Neither check executes QQ queries/downloads or establishes media, cancellation,
provider, complete ABI or recovery acceptance. Historical candidate sections below
retain their implementation lineage; their old unbuilt status is superseded only
by this matching build evidence. No real QQ probe or management API was executed.

## Scoped forward query media (SDK2.5.18; runtime acceptance pending)

Scoped Get/Observe forward queries own their request and dependencies before
suspension. The named native GetForwardMsg operation retains the original
account origin and one configured deadline; response size, WS association and
completed Direct disposition are checked before projecting node content.
Plural `messages` retains its existing precedence over singular `message`;
invalid selected content never triggers a second query or a fallback shape.

Actual returned image/video/record/file URLs can be retained in the existing
private FIFO store under distinct `queried-forward-*` receipt kinds. Metadata
and selector/array positions are bounded along with the URL. The selector is
only the query wrapper, not a node MessageRef or proof of the original file
group. No archive or synthetic normalization event can mint these receipts.
They grant neither reply authority nor legacy image-sidecar associations, and
standard detached query results do not acquire this capability.

Common media.read uses the retained original origin, ingress fence and bounded
credential-isolated HTTP reader. Declared sizes must match downloaded bytes;
unknown filename/MIME/stable identity remains unknown. Retirement or eviction
cannot renew a receipt, expose its URL, or enable a filename/file-ID fallback.
Images without observed URLs retain the native-image recipe. Other attachments
without readable observed URLs remain explicitly unavailable; the wrapper group
is never used for a guessed get_group_file_url. Forward-node market faces are
not added by this change. No new configuration or SDK file cache is introduced.

This source wiring is included in the actual SDK2.5.18 core and Bridge builds;
download, cancellation and provider acceptance remain unexecuted. Both frozen
SDK2.5.17 and SDK2.5.18 remain unchanged.

## Forward-node face observations (SDK2.5.14 candidate)

`ObserveOneBotForwardMessageRequest{QqForwardRef}` calls the existing native
get_forward_msg once and returns `ObservedForwardMessage`: the same normalized
forward plus ordered `ForwardFaceObservation` entries (node index, normalized
block index and original FaceView). It does not synthesize a node MessageRef,
user, timestamp or reply relation. Face observations themselves grant no media
access; the later origin-bound query-media path is separate. Face positions are obtained
from the owning normalizer in that same response, not matched to another query
or a reconstructed archive. Malformed face metadata remains UnsupportedContent;
no face ID is decoded from an opaque reference or invented.

The ordinary GetOneBotForwardMessageRequest remains available and projects its
existing result from the same implementation. Its result, ReceivedForwardNode,
and old wire layouts are unchanged; the new operation/result use distinct names.
Scoped source matching, sorted unique node/block positions, face category and
numeric IDs are validated. The operation is a read-only typed view over the
existing standard query, not a new native provider action or arbitrary JSON
escape hatch. The SDK does not generate Bridge display text or cache policy.

Only the new SDK generation may install this header/operation. Prior installed
SDKs stay sealed. Compilation is not provider/ABI/loader or full node-rendering
acceptance; other unsupported node content and original CQ-string presentation
still require actor-specific compatibility review.

## Scoped file-ID acquisition (SDK2.5.11 candidate)

`ReadOneBotFileRequest{conversation, file_id, maximum_bytes}` is a named QQ-only
read. It requires a full same-installation group/private ConversationRef, a
nonempty bounded native file ID and an explicit positive byte limit. The ID
remains an opaque provider value, including provider IDs containing slash; it is
never interpreted as an OS path or URL by the SDK. There is no arbitrary file
management, cache registration, upload or native-method passthrough.

The synchronous endpoint captures the original self authority, transport owner
and one configured deadline before returning owned work. A process-private
FileQueryKind selects only get_group_file_url/get_private_file_url; group_id and
user_id remain strings as in the existing vendor serializers. HTTP and WS reuse
the existing owning/admission/cancel-and-join transport machinery. Standard
NativeAction inventory and standard operation semantics are unchanged. WS uses
the shared obcx.standard echo tracker for correlation, not to claim that these
vendor methods belong to OneBot11's standard API.

Native replies are bounded and associated before accepting a completed direct
read. Locators remain private. Credential-isolated HTTP acquisition uses the
original authority/deadline and existing read limits; it requires completed200,
nonempty bytes within the caller budget and matching explicit file_size/size
metadata when supplied. Conflicting declarations fail. Empty files remain
unrepresentable by the current nonempty BytesSource contract and fail explicitly.
The MediaBlob has File kind, measured size, no invented MediaIdentity and no
provider URL/path/credentials, filename or MIME guesses. Caller/Bridge supplies
its observed filename for sending and owns caches, final files, conversions,
leases and cleanup. No automatic retry or account replacement occurs.

Ordinary received file segments with an explicit nonempty file_id now use the
internal file-id reference kind; common read_media delegates to this same owned
file reader with its ORIGINAL deadline. Legacy file references remain decodable
but may contain only a display filename, so they do not authorize a guessed
file-ID query. Legacy forward-file-id references remain non-readable: the
outer forward's group/private scope is not proof of the file's original scope.

When an incoming file has a valid direct URL, the existing process-private live
receive-authority store captures a received-file reference instead, retaining
the original origin and explicit size. This preserves URL-first acquisition
without exposing the URL or minting authority from an archive. It retains only
bounded read credentials/metadata, NOT file contents, filesystem caches or a
new cache-management API. Empty/absent file_id is not replaced by a filename or
hash for native queries. The observed safe file display basename is preserved
from the file field. Bridge's existing typed attachment reader retains that
filename and rejects declared file-size/budget mismatches before sending bytes.

This still does not complete all Bridge file fallback/forward-node scenarios or
retire existing ResolveOneBot*File and private legacy routes. Core compilation
is separate from installed consumer, HTTP/WS provider, cancellation and
actual-file acceptance.

## Exact known-content forward preflight (SDK 2.5.2)

The implementation class has a versioned symbol name to avoid interposing a
new inline-object layout on older installed actors. The sealed 2.5.1 candidate
was quarantined after ELF inspection found this weak-symbol hazard; it is not
a deployment candidate. This fix does not claim full loader/ABI acceptance.

The typed and borrowed-JSON validators account for actual validated group/user
ID widths inside the platform module, actual node/segment separators and JSON's
short control escapes. Actors neither decode opaque IDs nor construct native
payloads. The envelope reserves the maximum-width uint64 echo, matching the
existing caller policy; final serialization is still checked before transport.
For prepared LocalFile/BytesSource and literal text, accounting follows the
native recipe exactly. Unknown remote media lengths still require preparation
and a final envelope check; this is not a claim that arbitrary remote content
has an exact size before I/O.

`validate_independent_forward_plan(target, nodes, maximum_envelope_bytes,
per_message_limits)` is a pure public preparation boundary for the existing
ExHentai direct workflow. Each node has an independent one-message budget while
the entire plan retains the caller's native-envelope and 99-node bounds. It
performs no send, file access, media acquisition, identity minting or admission.
The caller must still own its batch/disk budget and separately submit each node
with SingleMessageOnly. Ordinary group-forward submission continues to enforce
one cumulative media budget; the independent-plan validator cannot authorize
or relax a group-forward send.

## Explicit authorless forward extension (SDK 2.5)

`AuthorlessForwardNode{body}` preserves the existing provider-specific forward
workflow that omits `uin` and `name`. It is a distinct alternative, not an absent
field/default on `ForwardNode`. No cached self, command caller or invented name
is substituted. Standard authored nodes and the standard composition API retain
their required author fields. Provider acceptance is not implied; unsupported
providers return the ordinary typed failure without a fallback or extra send.

Authorless nodes use the same typed body/media validation, aggregate byte limits,
borrowed preflight, explicit native-envelope budget, absolute deadline and
single-submission outcome accounting. Existing variant tags stay unchanged;
the new alternative is appended. All known/possible physical output and forward
metadata remain accounted for. Neither confirmation nor a successful result is
author proof for such a node. The caller still owns shared-file lifetimes.

## Observed market-face media (SDK 2.5)

A live `mface` carrying a supported HTTP(S) URL can produce a `received-mface`
image-family receipt and an optional bounded summary caption. `market_face_view`
recognizes that projection without I/O or new authority. Image-family means the
received source, not a verified static encoding: the Bridge prepares owned
bytes and explicitly chooses Animation for GIF, or normalized Image for static
formats. It keeps the summary with that item instead of moving it into an album
caption shared with unrelated images. Missing/unusable source evidence remains
UnsupportedContent and retains the existing explicit summary text presentation.

The receipt uses the existing installation-local store, original ingress and
account origin, retention bounds, cancellation and absolute read deadline.
Reference-kind matching prevents switching a received-image token into a
market-face token or vice versa. No new configuration, self cache, hostname
policy or download transport is introduced. Pure archive restoration still
returns no market-face media capability. The legacy image/sidecar association
path remains restricted to received-image; market faces do not create new old
image IDs or rewrite historical fingerprints. Forward-node market faces are not
newly authorized by this live-message path.

## Caller-owned media sources

`LocalFile{path, metadata}` supplies the caller's **final provider-visible
absolute POSIX path** for typed image/animation sends and constructed forward
nodes. The adapter serializes it as a percent-encoded `file://` URI; callers do
not write CQ JSON. Spaces, `#`, `%` and UTF-8 bytes are encoded without changing
path components. Relative paths, Windows drive paths and caller-authored file
URIs are unsupported native representations in this recipe.

There is no OBCX-host stat/read, canonicalization, path mapping, registration or
installed media authorization policy. The caller owns placement, accessibility,
mounts, business authorization and file lifetime, including recovery and cleanup
after possibly submitted outcomes. `obcx.path-mapping`, if used, is caller-side.
Declared sizes count against item/aggregate limits, including across forward
nodes. Absent provider-file sizes remain unknown, not zero or verified. The SDK
bounds its own encoded path/envelope memory, not unobserved provider file I/O.
Path metadata/format is not verified by reading the remote file.

`BytesSource` accepts static PNG/JPEG images and GIF animations. As with the
Telegram recipe, a PNG containing the APNG `acTL` marker is conservatively
rejected from `Image`; this is not a full PNG decoder or a conversion to static
content. APNG is not a supported QQ `Animation` encoding. `HttpSource` uses the
existing direct bounded HTTP(S) image transport, then the same byte format
validation and native base64 serialization. No installation/proxy
credentials, redirects or retries are introduced. HTTP work and native
submission share the existing absolute action deadline; cancellation remains
owned by the awaiting executor/transport. Actual downloaded bytes, not remote
size hints, consume preparation budgets. Since SDK 2.5.4, the non-Range fetch
requires HTTP 200 rather than any 2xx response; a 206 prefix cannot become a
complete source merely by matching an image signature. An explicit HttpSource
size declaration must equal the measured bytes before encoding or submission.
This does not turn unknown provider-only LocalFile sizes into measured sizes.
Callers retain destination/business authorization; this is not a DNS-pinning
sandbox.

Native image `MediaRef` reuse remains installation-scoped and retains its
existing provider-size lookup. `media.read` resolves native image metadata and
fetches validated HTTP(S) URLs with explicit byte bounds. Network destination
policy remains caller/deployment-owned, not a new SDK hostname/IP/CDN allowlist. A provider filesystem path is never opened on the OBCX host or reused as
a caller-supplied path. Cross-installation reuse remains rejected: explicitly
read owned bytes and then send them to the destination.

The obsolete `RegisterSharedFileRequest`, `onebot11.shared_file.register` action
and `MediaSourcePolicy` interface have been removed. Restoring an old synthetic
`shared` media reference does not recreate a native reference or authority; the
QQ codec rejects it. Capabilities describe supported image/animation source
recipes, subject to the syntax and native-reference restrictions above, not
filesystem reachability or provider execution guarantees.
