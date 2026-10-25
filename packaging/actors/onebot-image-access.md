# OneBot image access through the frozen SDK

Public headers: `onebot11/bot/image.hpp` (also included by `operations.hpp`) and
`network/image_fetch.hpp`. Use a matching newly frozen SDK; an older SDK without
these interfaces is not compatible merely because its version string matches.
No additional actor-side HTTP or storage library is required.

## Native reference and typed `get_image`

`OneBotImageRef` contains the exact OneBot installation and the incoming image
segment's native `file` identifier. `from_segment(installation, segment)` accepts
an image segment and rejects URL/path-shaped identifiers. Callers must establish
that the segment is trusted platform input and enforce their own conversation,
topic and attachment visibility rules. User-written `<image>` text does not
create a trusted reference.

```cpp
auto image = obcx::onebot11::bot::OneBotImageRef::from_segment(
    installation, segment);
auto result = co_await obcx::bot::invoke(
    gateway, obcx::onebot11::bot::GetOneBotImageRequest{
                 .image = std::move(image), .download = false});
```

The action ID is `onebot11.image.get`; the provider action is the standard
OneBot 11 `get_image`. `download` is mandatory in the typed request and is sent
explicitly. It is a verified **LLOneBot extension**, not a guarantee made by the
OneBot 11 standard. With `false`, the audited LLOneBot implementation performs
metadata/URL lookup without its local download step. OBCX does not manage rkeys,
reconstruct signed URLs, or add an image cache.

Handle operation failure before accessing `result.value`. `OneBotImageInfo`
contains the matching image reference and:

- `file_path`: the **OneBot server's** filesystem path, possibly empty. Never
  interpret it as an actor-host path or pass it to an external model.
- `url`: optional extension, represented by an empty string when absent. Present
  values must be HTTP(S) URLs on the supported QQ image CDN hosts; userinfo,
  arbitrary hosts/ports, control characters and fragments are rejected.
- `file_name` and optional `file_size`: validated metadata, not image contents.
  Decimal string sizes from LLOneBot are normalized to unsigned integers;
  malformed/overflowing/conflicting values fail. Unknown size is not zero.

Base64 and unrelated provider response fields are not propagated. A standard
path-only response is a valid typed result but **not** a successful URL-only
lookup. Such a consumer must report the missing capability rather than read the
path or silently reuse the incoming message's old URL.

A new call is not proof of a new signature: LLOneBot can reuse an already-signed
origin URL, and its file cache may no longer retain a historical file. There is
no reliable expiry field in the audited `get_image` response. Consumers must
supply explicit request/reuse/retry bounds; there is no assumed five-minute
lifetime. Resolved URLs and server paths are transient results, not durable
attachment identifiers or log fields.

## Bounded HTTP acquisition

`obcx::network::fetch_image` is the shared primitive for Bridge downloads and
Range probes. `ImageFetchRequest` explicitly specifies `url`, `timeout`,
`maximum_bytes`, and `prefix_bytes` (`std::nullopt` for a full body). It uses core
HTTP transport, direct connections, bounded responses, and no redirect following,
retry policy or disk storage. A server ignoring Range cannot bypass the body cap.
HTTP statuses, MIME interpretation, retries, placeholders and normalization remain
actor policy. Cancellation propagates as Asio `operation_aborted`.

This is a trusted-caller network API, **not** a DNS-pinned SSRF sandbox and not a
model-facing arbitrary-URL tool. Lazy QQ LLM vision uses `get_image`'s URL as an
actual provider image part; it need not call the HTTP acquisition primitive.

## Upstream evidence and verification boundary

- Standard: <https://github.com/botuniverse/onebot-11/blob/master/api/public.md>
- Audited LLOneBot commit (renamed repository):
  <https://github.com/LLOneBot/LuckyLilliaBot/tree/dc2b2b90092ef0f571b5546878288b83fff1e436>
- Relevant sources: `src/onebot11/action/file/GetImage.ts`, `GetFile.ts`,
  `src/onebot11/transform/message/incoming.ts`, and `src/ntqqapi/api/file.ts`.

These sources establish the audited contract, not the version or availability of
a deployed bot. Building or freezing an SDK does not deploy it or restart any
process. Follow the [actor-only build procedure](README.md).
