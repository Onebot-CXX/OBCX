# OneBot media access through the domain SDK

## Ordinary actor migration

Use `<core/bot/bot_client.hpp>` and an actually observed `MediaRef`:
`BotClient::read_media(ReadMediaRequest{media, maximum_bytes})` returns
`BotOperationResult<MediaBlob>`. The byte bound is explicit and positive; it is
not a new default. Check failure before accessing the value. The blob owns its
`BytesSource`, available metadata, optional kind and optional stable identity.
Unknown metadata is not zero, an inferred filename or proof of playability.

Retain the original installation/source association and enforce the actor's
conversation/topic/attachment visibility policy. A restored archive, opaque ID,
URL or user-written `<image>` text does not renew read authority. QQ's retained
receive/query receipts use their original origin, bounds and private retention;
eviction, expiry or account retirement is explicit unavailability, never a
reason to reacquire through an old raw-event URL.

The previous actor example using `OneBotImageRef::from_segment`,
`GetOneBotImageRequest`, `onebot11.image.get`, and direct URL consumption is
retired. `onebot11/bot/image.hpp` is now a **process-only** response recipe and is
not installed in SDK2.5.18. Do not export it, parse native segments in an ordinary
actor, or recover it by including core source headers. Chat LLM's migrated image
path uses bounded owned bytes; this document no longer instructs callers to send
signed URLs or server paths to an external model.

## Explicit standard media queries

The separately installed `<onebot11/bot/standard_media_queries.hpp>` models
`GetImageParameters`, `GetRecordParameters`, scoped selectors and explicit
`RecordFormat`. Use the named standard client operation with its required limits
and execution mode. This is query intent, not an observed attachment capability.

`ImageFileData` and `ConvertedRecordData` carry `ProviderLocalFile` with an
explicitly accessed `SensitiveText` path. That path belongs to the provider:
it does not authorize an OBCX-host open, mapping, download or disclosure to a
model/log. A record's requested output format is not verified byte metadata.
Async/rate-limited acknowledgment is not a completed query. The private
LLOneBot `download=false` behavior is not a standard API promise or a new public
ordinary-actor URL escape hatch.

## Acquisition and caller responsibilities

The owning adapter uses existing bounded, credential-isolated HTTP acquisition
where actual evidence permits it. SDK syntax validation is not a hostname/IP/CDN
allowlist or a DNS-pinned SSRF sandbox. Destination/business authorization remains
caller/deployment-owned. Do not add an SDK access policy or silently inherit bot
credentials for arbitrary media hosts.

For a caller-owned `HttpSource`, actual bytes must pass the selected adapter's
bounds, declared-size and format checks before sending. Complete-body acquisition
cannot silently accept a Range prefix. For `LocalFile`, callers supply the final
path: QQ's path-capable recipe serializes the provider-visible path without
assuming that it exists on the OBCX host; Telegram's upload recipe must actually
read its supplied path through the process reader. Neither maps paths for actors.

Bridge owns conversion, cache keys, temporary files, leases, placeholders and
cleanup through all outputs/cancellation/uncertain effects. It must not recover a
failed read from a native URL/path. See the
[QQ media implementation boundaries](../../src/onebot11/bot/domain-api.md) and
[Telegram media matrix](../../src/telegram/bot/domain-api.md) for supported kinds
and explicit unavailable combinations. A bounded readable blob is not decoder,
transcoding or provider-playback acceptance.

## Historical protocol evidence and verification boundary

The earlier private get_image review used:

- [OneBot11 public API](https://github.com/botuniverse/onebot-11/blob/master/api/public.md).
- [LLOneBot/LuckyLilliaBot commit dc2b2b90092ef0f571b5546878288b83fff1e436](https://github.com/LLOneBot/LuckyLilliaBot/tree/dc2b2b90092ef0f571b5546878288b83fff1e436),
  notably `src/onebot11/action/file/GetImage.ts`, `GetFile.ts`,
  `src/onebot11/transform/message/incoming.ts`, and `src/ntqqapi/api/file.ts`.

Those references are historical protocol evidence, not the deployed provider's
identity or a reason to expose a private recipe. The complete standard inventory
has its separately pinned reference in the change's standard coverage matrix.
Current SDK/core and real actor builds are recorded under
`build/domain-sdk-2.5.18-current/`; media/provider/cancellation and recovery
acceptance remain open. Follow the [actor-only build procedure](README.md) and
[coordinated cutover gates](domain-sdk-cutover.md); builds and metadata checks do
not authorize deployment, restart or probing.
