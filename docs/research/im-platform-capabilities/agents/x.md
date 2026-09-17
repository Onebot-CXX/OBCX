# X (formerly Twitter) SNS + DM capability audit

## 1. Lane metadata

- **Platform:** X (formerly Twitter)
- **Scope:** SNS posts and conversations plus direct/private messaging. Covered: posts, replies, quote posts, reposts, likes, media, users, follows, blocks, mutes, lists, bookmarks, timelines, search, streams, webhooks/account activity/X Activity, DMs/XChat, Spaces/live, OAuth, access tiers, permissions, limits, policy, and lifecycle.
- **Research date / access date:** 2026-08-17 UTC.
- **Evidence rule:** Official X Help and X Developer documentation only. Help pages were discoverable but returned HTTP 403 to direct retrieval; product statements from those pages are therefore quoted only where indexed by the search service and are called out below.
- **Freshness caveat:** `docs.x.com` is a live, rapidly changing documentation set. Several current pages contradict each other (notably Account Activity tiering, full-archive eligibility, DM deletion, and v2 media upload). Endpoint reference and Developer Console entitlement should be verified immediately before implementation. Exact prices are intentionally not made part of the proposed contract.
- **Status vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, `UNKNOWN` as defined in the lane brief.

## 2. Executive findings

1. X API v2 exposes broad native SNS primitives—post lookup/create/delete/edit, replies, reposts, likes, timelines, search, social graph, Lists, Bookmarks, and media—but nearly every write is user-context OAuth and self-serve post creation has important policy/product restrictions. [S1][S4][S7][S8]
2. A developer app is not a separate chat bot identity. It acts as each authorizing X account; bot disclosure is a policy/product attribute, so OBCX must bind credentials per X user rather than model one app-global sender. [S4][S21]
3. Current access is credit-based pay-per-use, with reads billed per returned resource and writes/actions per request, alongside endpoint rate limits and account/package caps. Enterprise adds exclusive/high-volume surfaces and negotiated limits. Do not encode prices as stable constants. [S1][S2][S3]
4. Self-serve API replies are allowed only when the original author explicitly “summoned” the replying account by mentioning it or quoting one of its posts; self-serve API-created posts also allow at most one cashtag. This is much narrower than ordinary product reply behavior. [S8]
5. Quote-post creation is documented in the create schema but the current create reference/integration material marks it Enterprise-only; quote lookup remains available. Treat outbound quote as entitlement-gated, not baseline. [S7][S8]
6. Current Direct Messages v2 can create 1:1 and group conversations, send text or one media attachment, retrieve at most 30 days of DM events, and delete a sent event. It requires user context and `dm.read`/`dm.write` plus `tweet.read` and `users.read`; app-only access is unavailable. [S9][S10][S11]
7. Product messaging is migrating to encrypted **XChat**, but the documented compose/lookup DM API is explicitly the legacy unencrypted DM stack. X Activity exposes XChat event notifications (`chat.received`, `chat.sent`, `chat.conversation_join`), not a documented API for reading or composing encrypted plaintext. [S13][S19]
8. Three real-time families must not be conflated: Filtered Stream for matching public posts; X Activity for typed per-user events over HTTP stream or webhook; and the older Account Activity webhook bundle. X Activity has finer event subscriptions and self-serve limits up to 1,500 subscriptions. [S6][S12][S13]
9. The Account Activity introduction says pay-per-use gets 3 user subscriptions/1 webhook, while the API overview labels Account Activity Enterprise-only. This unresolved official conflict requires runtime entitlement probing or Console confirmation. [S1][S12]
10. Spaces are discovery/metadata APIs only: lookup/search live or scheduled Spaces, creator/host/speaker data, shared posts and ticket buyers. There is no official API to create, join, speak, moderate, record, or stream Space audio; ended Spaces become unavailable from these endpoints. [S18]
11. Media upload is a first-class v2 surface with simple and chunked async flows and post/DM media categories, but a stale post integration sentence still says v2 cannot “fully upload” media. Prefer the dedicated current Media reference and test the entitlement. [S16][S17][S8]
12. Product-only capabilities—including rich XChat encryption, disappearing messages, reactions, voice notes, audio/video calls, consumer post editing UI, and full Spaces participation—must never be advertised as available through the official automation API. [S19][S20]
13. Webhook consumers need hourly CRC validation, HMAC signature checking, public HTTPS without an explicit port, prompt 200 responses, durable deduplication, and replay/reconciliation paths. [S14][S12]
14. The API has no documented general idempotency key, delivery exactly-once guarantee, generic presence/status, typing/read-receipt outbound operation, arbitrary file API, or generic product notification inbox API. Those must be capability-discovered as unsupported/unknown rather than represented by no-op methods. [S7][S9][S13]

## 3. Research action log

1. Read the lane brief and enumerated every required surface/status dimension.
2. Searched official `docs.x.com` for: API catalog; posts/manage/search/stream/timelines; user graph/privacy; Lists/Bookmarks; DMs and OAuth scopes; X Activity/Account Activity/webhooks; Media/Spaces; pricing/rate limits; migrations; developer policy.
3. Searched official `help.x.com` for end-user posting, replies/reposts/bookmarks/Lists, edit post, DMs/groups, XChat encryption, calls, and Spaces.
4. Opened and read the most relevant live developer pages: overview, pricing, rate-limit table, authentication map, post/DM integration guides, search/stream, Account Activity, X Activity, webhook quickstart, media, timelines, and Spaces.
5. Direct fetches of X Help pages returned **HTTP 403**. Their existence and indexed official summaries were retained only to establish the product/API boundary; no unsupported product detail is elevated to an API claim.
6. Cross-checked conflicts among current pages. Recorded rather than silently resolving: Account Activity tier, full-archive access, v2 DM delete, v2 media upload, and edit-window wording.
7. Dropped SEO blogs, unofficial SDKs, reverse-engineered/private GraphQL clients, and browser automation because none establish supported official capability.

## 4. Source register

All sources accessed 2026-08-17.

| ID | Authority | Title / relevant heading | URL | Evidence (precise paraphrase/excerpt) |
|---|---|---|---|---|
| S1 | OFFICIAL | **X API Overview** — endpoint catalog/tiering | https://docs.x.com/x-api/overview | Catalogs Posts, Users, DMs, Spaces, Lists, Media, streaming and webhooks; says pay-per-use unless Enterprise-only; separately labels Account Activity and Stream Webhooks Enterprise-only. |
| S2 | OFFICIAL | **X API pay-per-usage pricing and credits** — credit consumption | https://docs.x.com/x-api/getting-started/pricing | “pay-per-usage”; reads per resource, writes/actions per request; 24-hour UTC resource dedupe, credit balance/spend controls, activity-event billing. |
| S3 | OFFICIAL | **X API Rate Limits** | https://docs.x.com/x-api/fundamentals/rate-limits | Endpoint/app/user windows, `x-rate-limit-*` headers and 429 behavior; examples used below for posts, DMs, Lists, graph and media. |
| S4 | OFFICIAL | **X API v2 authentication mapping** | https://docs.x.com/fundamentals/authentication/guides/v2-authentication-mapping | Maps endpoints to OAuth 1.0a, app-only, PKCE, and scopes; also contains stale Academic-only full-archive wording. |
| S5 | OFFICIAL | **Search Posts** — recent/full archive | https://docs.x.com/x-api/posts/search/introduction | Recent search is last 7 days/up to 100; full archive since March 2006/up to 500 and currently described as pay-per-use or Enterprise. |
| S6 | OFFICIAL | **Filtered Stream** — introduction | https://docs.x.com/x-api/posts/filtered-stream/introduction | Persistent near-real-time rule-matching post stream; rules/connections/operators vary by tier. |
| S7 | OFFICIAL | **Create Posts** — request schema | https://docs.x.com/x-api/posts/create-post | `POST /2/tweets` schema includes text, reply, quote ID, media, poll, reply settings, Community ID, edit options and other X-specific flags. |
| S8 | OFFICIAL | **Manage Posts integration guide** — self-serve restrictions/rates | https://docs.x.com/x-api/posts/manage-tweets/integrate | User-context auth; summoned-only replies and one-cashtag self-serve limits; 200 POST/15m, 50 DELETE/15m and combined 300/3h; source label. Contains stale media sentence. |
| S9 | OFFICIAL | **Manage Direct Messages integration guide** | https://docs.x.com/x-api/direct-messages/manage/integrate | 1:1/group/send/delete endpoints; user auth/scopes; text or attachment; one photo/video/GIF; shared post by URL. Contains contradictory v1.1 deletion sentence. |
| S10 | OFFICIAL | **Direct Messages Lookup** — introduction | https://docs.x.com/x-api/direct-messages/lookup/introduction | All/conversation/participant event lookup; MessageCreate/ParticipantsJoin/ParticipantsLeave; up to 30 days; cursor pagination. |
| S11 | OFFICIAL | **Manage Direct Messages** — introduction | https://docs.x.com/x-api/direct-messages/manage/introduction | Current v2 create conversation/send/delete operation catalog. |
| S12 | OFFICIAL | **Account Activity API v2** — activity types/tier table | https://docs.x.com/x-api/account-activity/introduction | Bundled webhook events for posts/deletes/mentions/replies/reposts/quotes/likes/follows/blocks/mutes/legacy DMs/typing/read/revoke; says pay-per-use 3 subscriptions/1 webhook, Enterprise 5,000+/5+. |
| S13 | OFFICIAL | **X Activity API** — events/privacy/subscriptions | https://docs.x.com/x-api/activity/introduction | HTTP stream or webhook; typed post/social/profile/Space/privacy/DM/XChat events; public vs OAuth-private rules; self-serve 1,500, Enterprise 75k, Partner 150k subscriptions. |
| S14 | OFFICIAL | **Webhooks quickstart** — CRC/security/management | https://docs.x.com/x-api/webhooks/quickstart | Public HTTPS, no explicit port, initial/hourly CRC, HMAC-SHA256 POST signature; create/list/delete/validate webhook with app-only bearer. |
| S15 | OFFICIAL | **OAuth 2.0 Authorization Code with PKCE** | https://docs.x.com/fundamentals/authentication/oauth-2-0/authorization-code | Fine-grained delegated user scopes; `offline.access` enables refresh tokens. |
| S16 | OFFICIAL | **Media introduction / best practices** | https://docs.x.com/x-api/media/introduction | Simple/chunked upload, categories and async processing; media format/size guidance. |
| S17 | OFFICIAL | **Initialize media upload** | https://docs.x.com/x-api/media/initialize-media-upload | `POST /2/media/upload/initialize`, total bytes/type/category and chunked flow; categories include post, DM and subtitles. |
| S18 | OFFICIAL | **Spaces endpoints on X API v2** — availability/lifecycle/roles | https://docs.x.com/x-api/spaces/introduction | Lookup/search only; live/scheduled lifecycle, ended-space removal, creator/hosts/speakers/listener aggregates; no participation operations listed. |
| S19 | OFFICIAL | **About Chat** — product encryption/features | https://help.x.com/en/using-x/about-chat | Product XChat: encrypted messages/media/files/reactions, groups, unsend/disappearing messages/voice notes; metadata not encrypted and no forward secrecy. Direct fetch 403; indexed official content only. |
| S20 | OFFICIAL | **Audio and Video Calls** | https://help.x.com/en/using-x/direct-messages/audio-video-calls | Product 1:1 and group calls; platform-dependent group availability; group calls not E2E encrypted. Direct fetch 403; indexed official content only. |
| S21 | OFFICIAL | **X Developer Policy / Automation rules** | https://docs.x.com/developer-terms/policy | Consent/opt-out and anti-spam obligations; no aggressive/bulk automation or substantially duplicate cross-account content; bot disclosure requirements. |
| S22 | OFFICIAL | **Edit Posts** — controls/history | https://docs.x.com/x-api/fundamentals/edit-posts | Edit chain/IDs, eligibility controls, limited edit window/count, and non-editable post classes. |
| S23 | OFFICIAL | **Follows / Blocks / Mutes docs** | https://docs.x.com/x-api/users/follows/introduction | Graph lookup and follow/unfollow; cross-referenced current Blocks/Mutes endpoint family and user-context restrictions. |
| S24 | OFFICIAL | **Bookmarks** — introduction | https://docs.x.com/x-api/posts/bookmarks/introduction | Private authenticated-user bookmark lookup/add/remove operations. |
| S25 | OFFICIAL | **Lists** — current endpoint family | https://docs.x.com/x-api/lists/manage-lists/introduction | Create/update/delete Lists and separate membership/follow/pin/timeline operations. |

## 5. Product vs official API boundary

| Surface | End-user product | Current documented official API | Boundary/status |
|---|---|---|---|
| Posts | Create/delete, replies, quote/repost, polls/media, visibility/reply controls; Premium editing | Lookup/create/delete/edit, reply, repost, like, hide reply, media/poll fields | `API_LIMITED`: delegated user, plan/rate/policy restrictions; quote creation entitlement-gated. [S4][S7][S8][S22] |
| Feeds/discovery | Home/feed, profiles, mentions, search, Lists, notifications | User/mention/home/List timelines; recent/full search; filtered stream; no generic notification inbox endpoint found | `API_LIMITED`. [S1][S4][S5][S6] |
| Social/privacy | Follow, block, mute, like, bookmarks, Lists | Broad lookup/manage endpoints; block writes documented Enterprise-only while mute/follow/list/bookmark writes are user-context | `API_LIMITED`. [S3][S4][S23][S24][S25] |
| Legacy DMs | 1:1/group text/media; group participation; consumer controls | Send/create group, lookup 30-day events, delete sent event; one media attachment | `API_LIMITED`. [S9][S10][S11] |
| XChat | Encrypted chats/groups, reactions/files, disappearing/unsend, voice notes | X Activity notification events only; no documented encrypted plaintext lookup/send API | Product `NATIVE`; API `UNSUPPORTED` for content operations, `API_LIMITED` for event metadata. [S13][S19] |
| Calls | Product audio/video calls | No call create/join/media API found | Product `NATIVE`; API `UNSUPPORTED`. [S20] |
| Spaces | Host/co-host/speaker/listener live audio and reactions | Lookup/search metadata, shared posts/buyers, start/end activity event; no create/join/speak/moderate/audio | Product `NATIVE`; API `API_LIMITED` read/events only. [S13][S18] |
| Communities | Product community rooms/posts/moderation | Overview advertises lookup/search and create schema has `community_id`; no general room/member administration audited | `EXTENSION`/`API_LIMITED`; do not model as generic DM group. [S1][S7] |
| Webhooks | Not a consumer feature | Registered callback delivery for X Activity/AAA/Enterprise Stream Webhooks | API `API_LIMITED`; app/tier/CRC constraints. [S12][S13][S14] |

## 6. Capability evidence table

| Capability | Product support | Official API support/status | Restrictions / semantics | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | Apps and labeled automated accounts | `API_LIMITED` | App acts through an authorized user; no separate DM bot principal. Bot must disclose automation/operator. | S4,S21 | HIGH |
| Authentication | User authorizes apps | `NATIVE` OAuth 2 PKCE; `API_LIMITED` OAuth 1.0a and app-only | App-only is public/server read and webhook management; user writes/private reads need user context and endpoint scopes; refresh requires `offline.access`. | S4,S15 | HIGH |
| Multi-account | Multiple X accounts | `API_LIMITED` | One OAuth grant/token set per account; policy forbids abusive/duplicate cross-account automation. | S4,S21 | HIGH |
| Account/profile/status | Profiles, protected/verified state | `NATIVE` read; `API_LIMITED` profile events | User lookup and `/users/me`; X Activity profile updates. No generic presence/online status. | S1,S4,S13 | HIGH |
| User lookup | ID/handle/profile | `NATIVE` | Public/protected-field visibility and expansions apply. | S4 | HIGH |
| Follow graph | Follow/unfollow, followers/following | `NATIVE` read/write, user-context writes | Typical limits: GET 300/15m app/user; writes 50/15m user. | S3,S4,S23 | HIGH |
| Blocks | Block/unblock/list blocked | `API_LIMITED` | Read is user context; current docs mark block/unblock writes Enterprise-only. | S3,S4 | MEDIUM |
| Mutes | Mute/unmute/list muted | `NATIVE` but delegated | User-context scopes; GET typically 15/15m, writes 50/15m. | S3,S4 | HIGH |
| Lists | Curated public/private account lists | `NATIVE` | Create/update/delete, lookup, members, follow/unfollow, pin/unpin, List timeline; owner/delegated scopes and limits. | S4,S25 | HIGH |
| Bookmarks | Private saves/folders in product | `NATIVE` for list/add/remove; folder API `UNKNOWN` | Authenticated owner only; `bookmark.read`/`bookmark.write`; no folder endpoint established in audited docs. | S4,S24 | HIGH |
| Likes/reactions | Post likes; XChat reactions | Post likes `NATIVE`; DM reaction API `UNSUPPORTED` | Like/unlike and liking-user/liked-post lookup. XChat reactions are product encrypted but no compose/lookup API. | S4,S19 | HIGH |
| DM 1:1 | Yes | `NATIVE` for legacy DM | User OAuth only; recipient messaging settings/blocks can reject. | S9,S11 | HIGH |
| DM group/conversation | Up to product-defined membership | `API_LIMITED` | Create a group with initial participants/message; send to existing conversation. Membership events are readable, but no complete member-admin API was found. | S9,S10 | HIGH |
| Guild/server/room/channel | Communities and DM groups exist | `EXTENSION`; no generic guild/server API | A List, Community, Space and DM conversation have distinct semantics and must not be collapsed. | S1,S7,S18 | HIGH |
| Thread/topic | Post reply chains/conversation IDs | `NATIVE` lookup/composition | `conversation_id`, referenced-post relationships and reply payload; no mutable generic topic object. | S7,S12 | HIGH |
| Post/message create | Product posts and DMs | `NATIVE` with material restrictions | Post: text/media/poll/reply etc.; DM: text or attachment. Delegated identity and credits/rates. | S7,S8,S9 | HIGH |
| Get/history | Product histories | Posts `NATIVE`; DM `API_LIMITED` | Post lookup/timelines/search; DM event lookup only up to 30 days and max 100/page. | S4,S5,S10 | HIGH |
| Edit | Product post edit and some chat UI behavior | Post edit `API_LIMITED`; DM edit `UNSUPPORTED` | Post edit chain with eligibility window/count and excluded post types; no documented DM text-edit endpoint. | S7,S22 | HIGH |
| Delete/unsend | Delete posts; DM/XChat deletion/unsend differs | Post and legacy sent-DM delete `API_LIMITED`; XChat unsend API `UNSUPPORTED` | Delete own post/event only. DM v2 integration text conflicts internally; test. Product recipient semantics are not portable. | S7,S9,S11,S19 | MEDIUM |
| Reply | Product replies | `API_LIMITED` | Self-serve can reply only if summoned; direct reply events available. | S7,S8,S13 | HIGH |
| Quote | Product quote posts | `API_LIMITED` | Lookup/reference supported; create is currently Enterprise-only per create docs. Preserve referenced-post semantics. | S7,S8 | MEDIUM |
| Forward/share | Product share post to DM | `EMULATED` | No native forward object; put Post URL in DM text, yielding `referenced_tweets`. | S9 | HIGH |
| Repost | Product repost/undo | `NATIVE` | User-context write; lookup reposters; post-create events distinguish via references. | S4,S13 | HIGH |
| Rich text/entities/mentions | Hashtags, cashtags, URLs, mentions | `API_LIMITED` | Plain text plus parsed entities; reply mentions/exclusions. No arbitrary rich-text formatting contract. Self-serve one-cashtag rule. | S7,S8 | HIGH |
| Polls | Product polls | `NATIVE` create/read fields | Create payload; mutually exclusive with some attachment/quote/card combinations; no generic interactive form. | S7 | HIGH |
| Cards/buttons/forms/commands | Cards/deep links exist in product | `EXTENSION`/`UNSUPPORTED` as generic interactions | `card_uri`/DM deep link fields are X-specific; no bot command/button/form callback framework. | S7 | HIGH |
| Stickers | Some consumer media affordances | `UNSUPPORTED` | No official sticker operation/object found. | S1,S9 | MEDIUM |
| Image/GIF/video | Yes | `NATIVE` upload/attach/download-by-returned-URL | Post: up to 4 images or one GIF/video; DM: one media item. Async processing may be required. | S9,S16,S17 | HIGH |
| Audio/file/media groups | XChat files/voice notes; Space audio | Legacy APIs `UNSUPPORTED` for arbitrary files/audio; media group `API_LIMITED` | No XChat plaintext/file API; no Space audio stream. Treat post gallery as post media array, not generic album. | S16,S18,S19 | HIGH |
| Media upload | Yes | `NATIVE`, sometimes async | Simple/chunked initialize/append/finalize/status; ownership/category/expiry constraints; current reference supersedes stale guide. | S16,S17 | HIGH |
| Media download | Client renders media | `API_LIMITED` | Request media expansions/fields and fetch returned URLs; no universal authenticated file-download abstraction documented. | S10,S16 | MEDIUM |
| Members/roles/permissions | DM group, Community, Space roles | `EXTENSION`/`API_LIMITED` | Space creator/host/speaker/listener metadata is read-only; DM participant events; no common role-management API. | S10,S18 | HIGH |
| Moderation | Reply controls/hide, block/mute/report | `API_LIMITED` | Hide/unhide replies and user privacy actions exist; no general report/moderation audit API established. | S4,S23 | HIGH |
| Audit log | Product internal | `UNSUPPORTED` | Activity feeds are events, not an administrative immutable audit log. | S12,S13 | HIGH |
| Webhooks/event subscriptions | Developer feature | `NATIVE` but tiered | X Activity typed subscriptions; AAA bundled subscriptions; CRC/signature and webhook quotas. | S12,S13,S14 | HIGH |
| Persistent streams | N/A | `NATIVE` | Filtered public-post stream and X Activity stream; reconnect/checkpoint handling is adapter-owned. | S6,S13 | HIGH |
| Generic notifications | Product notifications tab | `UNSUPPORTED` as inbox; `EMULATED` from events | Activity APIs deliver selected events, not fetch/manage/read-state for the consumer notification inbox. | S12,S13 | HIGH |
| Presence | Some live indicators | `UNSUPPORTED` | No generic online/offline presence endpoint/event found. | S1,S13 | HIGH |
| Typing/read receipt | Legacy DM product | Inbound `API_LIMITED`; outbound `UNSUPPORTED` | AAA/X Activity receive legacy DM typing/read events; no official operation found to send typing or mark read. | S12,S13 | HIGH |
| Feed/timeline | Home/profile/mentions/List | `NATIVE` but window/visibility limited | User, mentions, reverse chronological home, List timeline; cursor pagination and auth vary. | S4 | HIGH |
| Search | Product search | `NATIVE` recent/full archive | Recent 7 days; full archive current pay-per-use/Enterprise; query/operator and resource caps. | S5 | HIGH |
| Live/Spaces | Full product participation | Read/events `API_LIMITED`; control/audio `UNSUPPORTED` | Search/lookup live or scheduled, roles/shared posts/buyers; start/end events; ended records unavailable. | S13,S18 | HIGH |
| Voice/video calls | Product XChat calls | `UNSUPPORTED` | No official call signaling/media API. | S20 | HIGH |
| Encryption | XChat encrypted content | API content operations `UNSUPPORTED`; event exposure `API_LIMITED` | XChat product encryption excludes metadata and lacks forward secrecy; legacy DM API is explicitly unencrypted stack. | S13,S19 | HIGH |
| Federation | No federated product model documented | `UNSUPPORTED` | X IDs/resources are platform-local. | S1 | HIGH |
| Tenant/admin/compliance | Developer Project/App; compliance products | `EXTENSION` | Project/App is credential/billing boundary, not a user guild tenant. Compliance jobs/streams concern X content obligations, not tenant admin. | S1 | MEDIUM |
| Idempotency | N/A | `UNKNOWN` / assume absent | No idempotency-key contract found for post/DM writes. Client retries can duplicate content; reconcile returned IDs/events. | S7,S9 | MEDIUM |
| Pagination | Product infinite scroll | `NATIVE` | `next_token`/`pagination_token`; endpoint-specific page sizes and windows. | S5,S10 | HIGH |
| Rate/usage limits | Consumer daily limits also exist | `API_LIMITED` | Endpoint app/user windows + 429 headers; credits, spend limit, Post-read package cap and subscription quotas all apply independently. | S2,S3,S12,S13 | HIGH |
| Passive-reply window | No generic window | `EXTENSION` | X has summoned-only self-serve reply eligibility, not a WhatsApp-style elapsed-time customer-care window. | S8 | HIGH |
| Payload/message limits | Product/account tier varies | `API_LIMITED` / partly `UNKNOWN` | Post schema does not state a stable max text/body size; media/poll counts are specified elsewhere; DM lookups 30 days and one attachment/send. Discover/validate dynamically. | S7,S9,S10,S16 | MEDIUM |

## 7. Inbound event inventory

### 7.1 X Activity API (preferred granular family)

Delivery is a **persistent HTTP stream** (`GET /2/activity/stream`) and/or a registered webhook. Subscriptions are individually created/updated/deleted by event type and `user_id` filter; private event subscriptions require that user’s OAuth authorization and relevant scopes. [S13]

- **Posts:** `post.create`, `post.delete`, `post.mention.create`, `post.reply.create` (direct replies only), `post.quote.create` (direct quotes only), `post.repost.create`.
- **Likes/social/privacy:** `like.create` (directional), `follow.follow`, `follow.unfollow`, `mute.mute`, `mute.unmute`, `block.block`, `block.unblock`.
- **Profiles:** bio, profile picture, banner, display name/screenname, handle, location/geo, URL, verified badge, affiliate badge updates.
- **Encrypted XChat notifications:** `chat.received`, `chat.sent`, `chat.conversation_join`. Do not infer plaintext availability.
- **Legacy unencrypted DM:** `dm.received`, `dm.sent`, `dm.read`, `dm.indicate_typing`.
- **Live:** `spaces.start`, `spaces.end`; `broadcast.chat` for owner’s broadcast chat.
- **Lifecycle:** `oauth.revoke`.
- **Other out-of-scope but present:** `news.new` (tier-restricted).
- **Limits:** self-serve 1,500, Enterprise 75,000, Partner 150,000 subscriptions as currently documented. [S13]

### 7.2 Account Activity API v2 (older bundled account webhook)

Webhook-only bundled subscription emits `tweet_create_events` (including standalone/reply/repost/quote/mention forms), `tweet_delete_events`, `favorite_events`, follow/unfollow, block/unblock, mute/unmute, legacy `direct_message_events`, typing, read, and authorization revoke. It identifies the subscribed account with `for_user_id`. It also offers subscription list/count/delete and replay-job endpoints. [S12]

**Access conflict:** the AAA page says pay-per-use 3 unique subscriptions/1 webhook; the overview says Enterprise-only. Mark capability `UNKNOWN` until Console/endpoint entitlement confirms. [S1][S12]

### 7.3 Public post streams/webhooks

- **Filtered Stream:** rule-matching public Posts over a persistent connection. Self-serve and Enterprise limits/operators differ. [S6]
- **Stream Webhooks:** filtered-post delivery to webhook is explicitly Enterprise-only in the overview; do not confuse it with attaching a webhook to X Activity. [S1]
- **Compliance:** deletion/compliance signals exist, but a complete compliance-stream audit was outside this lane; adapters must still process `post.delete`/AAA delete notices.

### 7.4 Delivery requirements

Registered webhook URL must be public HTTPS without explicit port, answer initial/hourly CRC using HMAC-SHA256 over `crc_token`, verify `x-twitter-webhooks-signature` against the raw body, and remain valid or delivery stops. Webhook management uses app-only bearer auth. The adapter should acknowledge quickly, enqueue durably, deduplicate, and treat ordering/exactly-once as unguaranteed. [S14]

## 8. Outbound operation inventory

| Family | Official operations | Result / error / async semantics |
|---|---|---|
| Posts | Lookup one/many; create/edit/delete; reply; repost/undo; like/unlike; hide/unhide reply; retrieve quoted-by/reposted-by/liking users | Create returns Post data/ID or per-request errors. Edit yields a new version ID in an edit chain. Writes need user context; 400/401/403/429 are material. No documented idempotency token. [S4][S7][S8][S22] |
| Timelines/search | User posts, mentions, reverse chronological home, List posts; recent/full search; counts | Cursor/token pagination; visibility and time/archive windows; reads are billable resources and package-capped. [S2][S4][S5] |
| Filtered stream | Create/list/delete rules; open stream | Long-lived connection; adapter owns reconnect/backoff and duplicate-safe checkpointing. [S6] |
| Users/social | User lookup/me; follow/unfollow; followers/following; block/mute lists and writes; privacy actions | Public reads may be app-only; private lists/writes delegated. Block writes may require Enterprise. [S3][S4][S23] |
| Lists | Create/update/delete List; lookup; list posts/members/followers; add/remove member; follow/unfollow; pin/unpin | Owner/user authorization and endpoint-specific pagination/rates. [S4][S25] |
| Bookmarks | List authenticated user’s bookmarks; add/remove | Private owner-only delegated access; pagination; no audited folder operation. [S24] |
| DM (legacy) | Send 1:1; create group with first message; send to conversation; list all/by participant/by conversation events; delete sent event | Create returns `dm_conversation_id`/event ID. Lookup is only up to 30 days, max 100/page. One media attachment, uploaded by sender and time-limited. Typical send cap: 15/15m plus 1,440/day user/app; lookup 15/15m/user. [S3][S9][S10] |
| Media | Simple upload; initialize/append/finalize/status; metadata/subtitles | Chunked processing can be `pending`/`in_progress`/`succeeded`/`failed`; poll status respecting `check_after_secs`, then reference `media_id`. [S16][S17] |
| Spaces | Lookup one/many/by creators, keyword search; posts shared in Space; ticket buyers | Read-only metadata; ended Space lookup becomes unavailable. No join/control/audio result type. [S18] |
| Webhooks/activity | Create/list/validate/delete webhook; create/list/update/delete X Activity subscription; connect activity stream; AAA subscribe/list/count/delete/replay | CRC can fail registration/validation; invalid webhook stops events. OAuth split depends on private event vs app-level webhook management. [S12][S13][S14] |

## 9. Normalized common-capability candidates

These are safe cross-platform candidates when modeled narrowly and discovered at runtime:

1. **ActorRef / UserProfile** — platform, stable user ID, handle/display name, avatar, optional protected/verified fields. Never use handle as immutable key. [S4][S13]
2. **CredentialBinding** — app/project ID + authorizing user ID + granted scopes + token expiry/refresh capability. Supports many accounts without pretending the app is the sender. [S4][S15]
3. **ContentRef / Post** — ID, author, text, created time, entities, media refs, conversation ID, and typed references (`reply_to`, `quote_of`, `repost_of`). Preserve edit-chain IDs. [S7][S22]
4. **CreatePostRequest/Result** — text, media refs, optional reply target/poll/reply policy; result ID or typed platform error. Quote creation should be a discoverable optional capability. [S7][S8]
5. **ConversationRef / DirectMessageEvent** — platform conversation ID, event ID, sender, timestamp, text, attachments, participant join/leave. Add `history_horizon` because X is 30 days. [S9][S10]
6. **SocialGraphCapabilities** — independently discoverable follow, mute, block, like and bookmark read/write flags; access is not symmetric. [S4]
7. **CursorPage<T>** — items plus opaque next token and optional result count. Never normalize an opaque X token into an offset. [S5][S10]
8. **MediaUploadJob** — media category, bytes/type, append/finalize, processing state and retry-after/check-after; useful across async media platforms. [S16][S17]
9. **EventEnvelope** — platform event ID if present, subscription/user binding, event kind, occurred/received times, typed payload plus retained raw payload/version. Supports both stream and webhook ingress. [S12][S13]
10. **WebhookRegistration** — URL, validity, secret/key reference, delivery family and subscription IDs. Transport/CRC/signature stay process-owned. [S14]
11. **RateBudget/EntitlementSnapshot** — endpoint capability, auth mode, scopes, plan, remaining/reset headers and credit/package state. Avoid static tier assumptions. [S2][S3]
12. **LiveSessionSummary** — ID, state, schedule, creator/hosts/speakers/counts; control/audio capabilities separate and false for X. [S18]

## 10. Required namespaced extensions

- `x.post.reply_settings`, `x.post.summoned_reply_eligible`, `x.post.cashtag_count`, `x.post.source_label` — X-specific creation and policy rules. [S7][S8]
- `x.post.edit_controls` / `x.post.edit_history_ids` — edit creates a version chain rather than a generic in-place mutation. [S22]
- `x.post.community_id`, `x.post.for_super_followers_only`, `x.post.nullcast`, `x.post.made_with_ai`, `x.post.paid_partnership`, `x.post.card_uri`, `x.post.direct_message_deep_link` — non-portable schema semantics. [S7]
- `x.activity.event_type`, `x.activity.direction`, `x.activity.filter`, `x.activity.tag` — preserve exact X Activity subscription semantics and distinction from AAA. [S13]
- `x.aaa.*` raw event families (`tweet_create_events`, `favorite_events`, etc.) — legacy-shaped payloads differ from X Activity and should not leak into the common event core. [S12]
- `x.chat.*` — encrypted XChat notification events must remain distinct from `x.dm.*` legacy plaintext events. [S13]
- `x.space.creator_id`, `host_ids`, `speaker_ids`, ticket-buyer/shared-post relationships and ephemeral-ended state. [S18]
- `x.media.category` (`tweet_*`, `dm_*`, subtitles/amplify) and processing metadata. [S16][S17]
- `x.billing.resource_kind` and 24-hour UTC dedupe observation for cost controls, not business capability semantics. [S2]
- A typed/raw X payload escape hatch is justified for evolving event schemas, but secrets, unknown arbitrary outbound parameters, and private/undocumented GraphQL operations must be excluded.

## 11. Limits, policy, review, regional and lifecycle risks

1. **Commercial/entitlement:** pay-per-use credit balance, spending limit, monthly/package resource caps, endpoint rate windows and webhook/subscription caps are independent failure dimensions. Enterprise has exclusive/high-volume endpoints. Exact rates/prices can change; query Console/docs and expose an entitlement snapshot. [S1][S2][S3][S13]
2. **Representative current request limits:** post create 200/user/15m and combined create/repost 300/user/3h; post delete 50/user/15m; DM send 15/user/15m and 1,440/user/app/24h; DM lookup 15/user/15m; follow/mute/List/bookmark endpoints have their own windows. Always honor response headers and 429 reset rather than hard-coding. [S3][S8]
3. **Self-serve post constraints:** summoned-only replies and one cashtag materially affect automation; quote creation appears Enterprise-only. [S7][S8]
4. **Automation policy:** obtain consent for automated replies/DMs where required, provide immediate opt-out, avoid bulk/aggressive actions and duplicate cross-account content, identify bot/operator, and use the official API. AI-generated unsolicited reply behavior may require additional approval under current guidelines. [S21]
5. **Private/protected content:** private X Activity events require user OAuth/scopes; public-event subscriptions exclude protected-account posts. DM access is always delegated. [S9][S13]
6. **DM lifecycle:** REST history is at most 30 days; legacy DM and XChat are separate stacks. Product migration could retire or narrow legacy DM operations/events. [S10][S13][S19]
7. **Space lifecycle:** scheduled up to 14 days in advance; cancelled/ended state changes and ended Spaces become unavailable, requiring deletion/reconciliation of cached data. [S18]
8. **Webhook lifecycle:** hourly CRC failures invalidate delivery; secret rotation, signature verification, quick acknowledgement, replay and reconciliation are operational requirements. [S14]
9. **Media lifecycle:** asynchronous processing, format/category restrictions, ownership and attachment lifetime can invalidate a post/DM send after upload. [S9][S16][S17]
10. **Regional/review:** no reliable general regional matrix was found. Account availability, product XChat/calls, protected content, age/safety and policy enforcement can differ. Mark region as `UNKNOWN`, do not promise global parity.
11. **Deprecation:** OAuth 1.0a is legacy-supported, not preferred. v1.1 AAA/DM references and old terminology remain in current pages; new integrations should prefer v2 and X Activity where entitlement/functionality matches, without treating legacy endpoints as current baseline. [S4][S9][S12][S13]

## 12. Conflicts and unknowns

| Issue | Contrary official evidence | Audit disposition |
|---|---|---|
| Account Activity access | Overview says Enterprise-only; AAA introduction gives pay-per-use 3 subscriptions/1 webhook. [S1][S12] | `UNKNOWN`; confirm in Developer Console/endpoint before enabling. Prefer X Activity for new typed subscriptions when sufficient. |
| Full-archive search | Authentication map says Academic Research only; current Search introduction says pay-per-use or Enterprise. [S4][S5] | Treat Search introduction as newer, but capability-probe and handle 403. |
| DM delete v2 | Current endpoint catalog lists `DELETE /2/dm_events/:event_id`; same integration guide’s “ID compatibility” paragraph says delete via v1.1 “not yet available in v2.” [S9][S11] | Likely stale paragraph. Implement v2 behind integration test; never require v1.1 baseline. |
| v2 media upload | Current Media references expose `/2/media/upload...`; Manage Posts guide says there is not yet a way to “fully upload media using v2.” [S8][S16][S17] | Prefer dedicated current Media reference; test actual entitlement/format. |
| Account Activity vs X Activity naming | Overview has both “X Activity” and “Account Activity”; pricing bills X Activity events while AAA remains separately documented. [S1][S2][S12][S13] | Model as distinct adapters/event families, not aliases. |
| Product Edit Post timing | Indexed Help material has historically stated a one-hour product window, while developer edit fundamentals state 30 minutes/5 edits. [S22] | API contract follows `edit_controls.editable_until`, never a hard-coded window. |
| Quote-post creation tier | Schema exposes `quote_tweet_id`, while current supporting docs label outbound quote Enterprise-only. [S7][S8] | `API_LIMITED`; entitlement discovery required. |
| XChat content API | X Activity documents encrypted Chat events but no plaintext lookup/send endpoints. [S13] | `UNSUPPORTED` for content until an official reference appears; do not reverse engineer clients. |
| Bookmark folders | Product may organize bookmarks, but audited API family establishes only list/add/remove. [S24] | `UNKNOWN`/unsupported in baseline. |
| DM edits/reactions, call control, Space audio/control, presence | Product has some of these; no official automation endpoint found. [S18][S19][S20] | `UNSUPPORTED`; capability false. |
| Idempotency and webhook ordering/redelivery contract | No authoritative idempotency or exactly-once/ordering guarantee found in checked pages. | `UNKNOWN`; design duplicate-safe and reconciliation-based. |
| Payload text maxima | Create schema does not state a stable text/request-body maximum; product tiers may vary. [S7] | Validate server-side and preserve structured error; do not freeze “280” into common DTO. |

## 13. OBCX design implications

1. **Capability discovery, not giant `IBot`.** Publish small process-owned capabilities such as `PostReader`, `PostWriter`, `LegacyDmReader/Writer`, `SocialGraph`, `ListManager`, `MediaUploader`, `ActivitySubscriber`, and `SpaceReader`. Quote, block-write, AAA, XChat-content, and call control must be independent flags. This follows the tier/auth asymmetry and product/API split. [S1][S4][S7][S13]
2. **Credential actor per authorized account.** Serializable requests carry an internal credential-binding ID, never raw tokens. The adapter resolves that binding to App + X user + scopes and rejects sender mismatch. App-only and user-context clients are separate pools. [S4][S15]
3. **Process owns transport.** Long-lived filtered/activity streams, webhook HTTP/CRC/HMAC, OAuth callbacks, media bytes, retries and rate budgets remain outside business actors. Business actors receive typed serializable `XEventEnvelope` messages after verification/deduplication. [S6][S13][S14]
4. **Separate XChat and legacy DM DTOs.** `LegacyDmEvent` may contain API-readable text/attachments and a 30-day horizon; `XChatActivityEvent` must not imply readable encrypted content. Never downcast both to a misleading universal “message body.” [S9][S10][S13]
5. **Model post relationships explicitly.** `PostRefKind = Reply | Quote | Repost` and edit-chain metadata avoid losing X semantics. `CreateReply` performs a local summoned-eligibility precheck when possible but still treats 403 as authoritative. [S7][S8][S22]
6. **Typed results and partial errors.** Every egress command returns `Success(resource_id, raw)` or structured `AuthScopeMissing`, `EntitlementRequired`, `PolicyRejected`, `RateLimited(reset_at)`, `CreditExhausted`, `NotFoundOrNoLongerAvailable`, `MediaProcessingFailed`, etc.; no empty/no-op implementation. [S2][S3][S7][S9][S18]
7. **Cost/rate governor.** Maintain endpoint/user/app buckets from response headers plus configurable credit/spend/package budgets. Resource reads can be charged differently from request writes and deduped by X in a UTC window; this belongs in adapter operations, not domain semantics. [S2][S3]
8. **At-least-once-safe ingress.** Deduplicate by platform event ID where present, otherwise a bounded hash key; persist subscription/filter and cursor/reconnect state; reconcile REST state after stream gaps and use AAA replay only when entitled. Do not claim exactly once. [S10][S12][S13][S14]
9. **Async media workflow.** Use a serializable upload state machine (`Initialize → Append → Finalize → Poll → Ready/Failed`) and issue post/DM create only after readiness and within media validity. Do not send large bytes through business actors. [S9][S16][S17]
10. **Ephemeral and bounded history metadata.** Every reader advertises its known horizon. DMs advertise 30 days; recent search 7 days; full archive is a separate entitlement; ended Space objects must be tombstoned/removed. [S5][S10][S18]
11. **Namespaced raw escape hatch with schema version.** Preserve unknown X event fields and exact raw error bodies for forward compatibility, but outbound raw operation use must be allow-listed to official endpoints only. [S1][S13][S21]
12. **Startup and periodic entitlement probes.** Check `/users/me`/scopes, configured tier, webhook validity and harmless endpoint access; update advertised capabilities when 403/credit/tier changes occur. This is mandatory given official documentation conflicts. [S1][S2][S12][S14]

### Suggested serializable messages

```text
XGetPost { credentialBinding?, postId, fields, expansions }
XCreatePost { credentialBinding, text?, mediaIds[], replyTo?, quoteOf?, poll?, xOptions? }
XCreateLegacyDm { credentialBinding, participantOrConversation, text?, mediaId? }
XUploadMediaStart / XUploadMediaChunk / XUploadMediaFinalize / XPollMedia
XFollowUser / XMuteUser / XBlockUser / XBookmarkPost / XManageList
XSearchPosts { authBinding, query, horizon: Recent|FullArchive, cursor? }
XActivitySubscribe { authBinding?, eventType, userId?, webhookId?, tag? }
XVerifiedIngress { family, subscriptionId?, forUserId?, eventType, payload, raw, receivedAt }
XApiResult<T> | XApiFailure { category, httpStatus?, platformCode?, retryAt?, raw? }
```

The `xOptions` union holds only audited, platform-namespaced fields. A capability descriptor reports auth mode, scopes, plan qualification, history horizon, read/write support and last successful probe.

## 14. Claim-to-source checklist

| Claim / recommendation | Sources |
|---|---|
| Broad v2 SNS API, but delegated/tiered writes | S1,S4,S7,S8 |
| App is not sender; bind per authorized account | S4,S21 |
| Credit + request/package limits; prices not stable contract | S1,S2,S3 |
| Summoned-only self-serve replies and one-cashtag rule | S8 |
| Quote creation entitlement-gated | S7,S8 |
| Legacy DM operations, scopes, 30-day history, one media | S9,S10,S11 |
| XChat product/API split | S13,S19 |
| Filtered Stream vs X Activity vs AAA separation | S6,S12,S13 |
| AAA tier conflict requires probing | S1,S12 |
| Spaces read/events only; ended data unavailable | S13,S18 |
| Current media API and stale-guide conflict | S8,S16,S17 |
| Product-only encryption/calls/voice features not API features | S19,S20 |
| CRC/signature/durable webhook ingress | S12,S14 |
| No generic idempotency/presence/outbound receipt contract | S7,S9,S13 |
| Capability-sliced architecture and typed failures | S1,S2,S3,S4,S13 |
| Transport/process ownership | S6,S13,S14,S16 |
| Separate XChat from legacy DM DTOs | S9,S10,S13 |
| Explicit post relationship/edit-chain DTOs | S7,S8,S22 |
| Cost/rate governor and entitlement probes | S1,S2,S3,S12,S14 |
| Async media state machine | S9,S16,S17 |
| Bounded history and ephemeral Space caching | S5,S10,S18 |
| Official API only; anti-spam/consent policy | S21 |
