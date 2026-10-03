# Research: Telegram platform capabilities audit

## 1. Lane metadata

- **Platform:** Telegram
- **Primary automation surface:** ordinary Telegram **Bot API 10.2** (HTTP API), current through the official July 14, 2026 changelog.
- **Other surfaces, kept separate:** Telegram Mini Apps (client-side web UI attached to bots); Telegram API/MTProto and TDLib (full user/client surface); end-user Telegram product.
- **Research date / access date:** 2026-08-17 UTC.
- **Scope:** identity/authentication; ingress; private chats, basic groups, supergroups, channels, channel direct messages and forum topics; messaging/media; reactions, polls and stickers; inline mode; commands, buttons and callbacks; payments and business/secretary bots; membership/admin/moderation; files; limits; privacy mode; and the Bot API versus TDLib boundary.
- **Freshness caveat:** Telegram's current Bot API page reports 10.2 and includes features released in 2026 (rich messages, ephemeral messages, communities, guest mode, managed bots). Older FAQ text conflicts with parts of that current reference and is treated as stale where noted. Telegram says availability may vary by client, geography, technical or regulatory conditions [S12].

## 2. Executive findings

1. **Ordinary bots use a BotFather-issued bearer token over HTTPS; they are not user accounts.** A bot has no phone-number login, online/last-seen status, contacts or ordinary user social graph. A leaked token grants full bot control [S1][S2].
2. **Ingress is a mutually exclusive choice of `getUpdates` long polling or `setWebhook`.** Updates are queued for no more than 24 hours; consumers must deduplicate/order using `update_id`; failed webhook deliveries are retried, but no exactly-once guarantee is documented [S1].
3. **The Bot API is event-oriented, not a general archive API.** It receives new/edited messages known to the bot but exposes no ordinary `getChatHistory` endpoint. In contrast, TDLib is a full client and explicitly exposes paginated `getChatHistory` [S1][S8].
4. **Bot topology is rich but permission-sensitive:** private chats, basic groups, supergroups, channels, channel direct-message chats, communities, and forum topics are represented. Bots can post to channels or manage groups only when membership/admin rights permit it [S1][S10].
5. **Privacy Mode is a major ingress boundary.** It is enabled by default in groups and limits a bot to addressed commands, qualifying general commands, messages sent via it, replies, and service messages. Bot admins receive all group messages; disabling privacy requires re-adding the bot [S3].
6. **Messaging is broad:** text/rich text, reply/quote, most Telegram media, albums, polls, stickers, reactions, forward and copy are official. Editing/deleting/forwarding are constrained by authorship, rights, age, content type and protected-content rules [S1].
7. **Membership administration is capable but not directory-complete.** Bots can count members, list administrators, inspect one member (guaranteed for others only when admin), approve join requests, invite, promote, restrict, ban and set permissions. There is no Bot API operation to enumerate all members or read the supergroup/channel admin log [S1][S10][S11].
8. **Inline mode, native commands, reply/inline keyboards, callback queries and Mini Apps are first-class interaction surfaces.** Inline use must be enabled; answers are capped at 50 results. Direct-link and inline Mini Apps do not thereby gain chat read/write access; attachment-menu access is restricted to major advertisers outside the test environment [S3][S4][S5].
9. **Payments are native but policy-split.** Digital goods/services inside Telegram must use Telegram Stars; physical goods/services use third-party providers. Shipping and pre-checkout updates require bot answers, with pre-checkout due within 10 seconds [S6][S7][S12].
10. **Business/Secretary bots are still Bot API bots, not userbots.** With explicit account connection and granted rights, they receive business-message updates and may act on behalf of the connected account, generally in selected chats and subject to access/write settings and a recent-chat window [S1][S3][S12].
11. **Default hosted Bot API file limits are materially lower than the consumer product:** 20 MB download and generally 50 MB multipart upload (10 MB photo); a self-hosted official Bot API server permits unlimited download and uploads up to 2,000 MB [S1].
12. **Presence, read receipts, inbound typing, calls/live voice, contacts, full member enumeration, audit history, arbitrary historical retrieval and secret chats are not ordinary Bot API capabilities.** TDLib/MTProto can expose many of these as a logged-in client; that is a separate user-authorized surface and must never be marketed as a normal bot [S8][S9].
13. **Rate limits are operational, not a stable quota contract:** approximately one message/second per chat, 20/minute in a group and about 30/second for free bulk notifications. Paid broadcast can raise throughput, but eligibility, Stars cost and thresholds are commercial/policy details, not portable semantics [S2].

## 3. Research action log

1. Read the lane brief and enumerated the required capability cells and evidence policy.
2. Attempted four focused official-domain searches covering Bot API transport/messages, bot features/Mini Apps, TDLib, and product topology. The configured Exa provider returned HTTP 429; a Brave retry had no configured API key. No search result claims were used.
3. Used official entry points directly: Bot API reference/changelog, Bot Features, Bots overview/FAQ, Mini Apps, inline mode, physical and Stars payment guides, Bot Developer Terms, Telegram API overview, TDLib overview/getting-started/getChatHistory, and MTProto topology/forum/rights/admin-log/auth pages.
4. Checked current-reference anchors for `Update`, `getUpdates`, `setWebhook`, `sendMediaGroup`, `forwardMessage(s)`, `copyMessage(s)`, edit/delete, reactions, polls, stickers, topics, member methods and files.
5. Compared current Bot API 10.2 and Bot Features against older Bot FAQ. Conflicts found: the FAQ says bots cannot see other bots and gives older general wording, while Bot API 10.0/current Bot Features document opt-in bot-to-bot communication. Current API/changelog wins; the conflict is retained in §12.
6. No nonofficial or secondary source was needed. Some Doxygen deep links are generated/version-sensitive; stable TDLib overview/getting-started pages were preferred.

## 4. Source register

| ID | Authority | Title | URL | Relevant section / precise evidence (accessed 2026-08-17) |
|---|---|---|---|---|
| S1 | OFFICIAL | Telegram Bot API 10.2 | https://core.telegram.org/bots/api | “Authorizing your bot,” “Getting updates,” `Update`, messaging, chat administration, stickers and files. Token-authenticated HTTPS; long polling/webhook exclusivity; 24-hour queue; operations, rights and limits. |
| S2 | OFFICIAL | Bots: An introduction for developers | https://core.telegram.org/bots | “How do bots work?” / “How are bots different from users?” Bots are special accounts, cannot start user conversations, have no online status, and use a token. |
| S3 | OFFICIAL | Telegram Bot Features | https://core.telegram.org/bots/features | “Inputs,” “Inline Requests,” “Mini Apps,” “Secretary Bots,” “Privacy Mode.” Defines commands/buttons, inline enablement, business access, and group-message visibility. |
| S4 | OFFICIAL | Telegram Mini Apps | https://core.telegram.org/bots/webapps | “Implementing Mini Apps,” launch modes, validation and attachment menu. Direct/inline Mini Apps cannot read chat or send without active user flow; attachment menu is restricted; validate `initData`. |
| S5 | OFFICIAL | Inline Bots | https://core.telegram.org/bots/inline | Inline query/results and feedback. User invokes `@bot query` in any chat; feedback can be sampled. |
| S6 | OFFICIAL | Bot Payments API (physical goods/services) | https://core.telegram.org/bots/payments | Provider-token flow, invoices, shipping/pre-checkout, third-party processing, 10-second pre-checkout deadline. |
| S7 | OFFICIAL | Payments for Digital Goods and Services | https://core.telegram.org/bots/payments-stars | Digital purchases require `XTR` Telegram Stars; invoice, `pre_checkout_query`, `successful_payment`, refund/support flow. |
| S8 | SPEC | TDLib: Getting Started | https://core.telegram.org/tdlib/getting-started | TDLib is a fully functional asynchronous client; four chat types, user authorization, updates, files and explicit `getChatHistory` pagination. |
| S9 | OFFICIAL | Telegram APIs | https://core.telegram.org/api | Explicitly separates simplified Bot API from Telegram API/TDLib customized clients; lists user authorization, contacts, calls, read metrics, stories, secret chats and admin log as client API features. |
| S10 | SPEC | Channels, supergroups, gigagroups and basic groups | https://core.telegram.org/api/channel | Product/client topology: basic groups ≤200, supergroups ≤200,000, unlimited broadcast channels, migration, rights and admin-log links. |
| S11 | SPEC | Admin log | https://core.telegram.org/api/recent-actions | `channels.getAdminLog` is a Telegram API/MTProto operation for supergroups/channels—not an ordinary Bot API method. |
| S12 | OFFICIAL | Bot Platform Developer Terms | https://telegram.org/tos/bot-developers | Availability, privacy/retention, anti-scraping, rate-limit circumvention, business-bot duties and digital/physical payment rules. |
| S13 | OFFICIAL | Telegram FAQ | https://telegram.org/faq | Product limits/features: groups up to 200,000, unlimited-audience channels, cloud chats, calls/read marks, 2 GB product file baseline, and secret-chat distinction. Page itself warns some content may be outdated. |
| S14 | SPEC | Forums | https://core.telegram.org/api/forum | Group and bot forum topology, topic rights and identifiers. Useful to distinguish product/MTProto details from Bot API topic methods. |
| S15 | OFFICIAL | Bot API changelog | https://core.telegram.org/bots/api-changelog | Current lifecycle evidence: Bot API 10.2 (2026-07-14), 10.1 rich messages, 10.0 guest/bot-to-bot/poll changes, 9.4 private topics. |

**Dropped / down-weighted sources**

- **Bot FAQ** (https://core.telegram.org/bots/faq) — retained only for rate-limit corroboration and as conflict evidence; its categorical “bots cannot see other bots” statement is stale against Bot API 10.0/current Bot Features.
- Search-provider summaries — dropped because searches failed and primary documentation was directly accessible.
- Community SDK documentation, blogs and “userbot” libraries — dropped as unnecessary and inappropriate for establishing official capability.

## 5. Product vs official API boundary

| Surface | End-user/product capability | Ordinary Bot API | TDLib / Telegram API boundary |
|---|---|---|---|
| Identity/login | Phone-number user accounts, multi-device sessions, passkeys/2FA | BotFather bot account + bearer token; no phone login | User-authorized custom client using `api_id`/`api_hash`, phone/email/code/QR/passkey/2FA state machine [S8][S9] |
| Cloud message history | Users browse/search synchronized history | Receives updates; **no general history retrieval** | `getChatHistory`, search and client caches are explicit TDLib capabilities [S8] |
| Contacts/social graph | Address-book contacts, usernames, shared groups | No contacts list/follow graph; only users/chats encountered or explicitly shared | Contacts/search/privacy APIs belong to client surface [S9] |
| Presence/read state | Online/last seen, read checks and typing | Bot has no online status; may send `sendChatAction`; no general inbound presence/read event | TDLib exposes status and inbox/outbox read updates [S8][S13] |
| Calls/live voice | Voice/video/group calls and voice chats | Cannot originate/join calls; media messages are not calls | Call/group-call APIs are client surface [S9][S13] |
| Secret chats | Device-specific E2EE 1:1 secret chats | Not available to bots | TDLib may enable secret chats for logged-in clients [S8][S9] |
| Admin log/full membership | Admin product UI; member lists may be hidden | Admin list/count/single-member lookup, but no full enumeration/admin-log method | `channels.getAdminLog` and richer member access are MTProto/TDLib and rights-sensitive [S10][S11] |
| Stories/feed-style features | Users/channels can post stories; channels broadcast posts | Ordinary channel posts are supported; some story operations are specifically business-account scoped, not a general user story API | Broad stories, feeds/search/recommendations are client APIs [S9] |
| Business delegation | Account owner connects a bot to selected private chats | Official `BusinessConnection`/business message and scoped acting methods | This is delegated Bot API access—not generic user login or unrestricted user-client automation [S1][S3][S12] |
| Mini App | Telegram-hosted webview UX | Bot API supports launch buttons/query answers; web app has validated init data | Mini App context is not general Telegram-client access and does not silently read the surrounding chat [S4] |

**Boundary rule:** an OBCX Telegram “bot adapter” should mean S1/S3 Bot API only. A TDLib connector is a separately named, separately consented **user-client adapter** with different credentials, terms, risk and capability discovery. “Userbot” scripts or MTProto automation must not be presented as ordinary Bot API support.

## 6. Capability evidence table

| Capability | Product support | Ordinary official Bot API support/status | Restrictions / boundary | Evidence | Confidence |
|---|---|---|---|---|---|
| Bot/app identity | Bots are labeled special accounts | **NATIVE**: BotFather creates bot; `getMe`; profile/name/description/commands/profile photo methods | Token is bearer credential; bot username rules; bot has no online status | S1,S2 | HIGH |
| Authentication | User accounts use phone/code/2FA/passkey | **NATIVE** for bot token; **UNSUPPORTED** for user login | HTTPS `bot<token>`; rotate/protect token. User auth belongs to TDLib | S1,S8,S9 | HIGH |
| Multi-account | Many simultaneous user sessions/devices | **API_LIMITED**: one token identifies one bot; a process may host multiple independent bot tokens | No API concept of one bot switching user identities. Business delegation is separate | S1,S2 | HIGH |
| Bot status/presence | Users expose privacy-filtered online/last seen | **UNSUPPORTED** inbound; bot has fixed “bot” label | `sendChatAction` is transient outbound activity, not presence | S2,S13 | HIGH |
| User/profile | Names, usernames, photos, statuses | **API_LIMITED**: `User`, `ChatFullInfo`, profile photos/audios for accessible users | Only data made available in updates/access context; no arbitrary directory | S1,S12 | HIGH |
| Contacts/follow/social graph | Contacts, shared groups, channel subscriptions | **UNSUPPORTED** ordinary Bot API | User/chat chooser shares selected IDs but may not make them accessible; TDLib contacts are separate | S3,S9 | HIGH |
| Private chat / DM | 1:1 cloud chat and secret chat | **API_LIMITED** cloud bot DM; bots cannot initiate a conversation with a user | User must message/start bot or add it first; secret chats unavailable | S1,S2,S8 | HIGH |
| Basic group | Up to 200 product/client members | **NATIVE** messaging and many admin methods | Bot must be added; rights vary; migration service messages must be handled | S1,S8,S10 | HIGH |
| Supergroup | Up to 200,000; shared history | **NATIVE** | Membership/admin rights and Privacy Mode gate access/actions | S1,S3,S10 | HIGH |
| Channel/broadcast | Unlimited subscriber audience | **API_LIMITED** channel posts/events and admin posting | Bot must be member/admin as required; channel is not a “server” with rooms | S1,S10 | HIGH |
| Channel direct messages/community | Product has channel DMs; 10.2 communities | **EXTENSION / API_LIMITED** via direct-message topic fields and `Community` | New, Telegram-specific topology; preserve raw/type fields | S1,S15 | HIGH |
| Forum/thread/topic | Forums split supergroups; private bot topics available | **NATIVE** create/edit/close/delete topic and target via `message_thread_id` | Supergroup management requires admin `can_manage_topics`; private threaded mode is BotFather-configured and fee-policy-sensitive | S1,S14,S15 | HIGH |
| Message create | Broad product message types | **NATIVE** text/rich message and specialized sends | Bot must have access/write permission; text normally 1–4096 chars; rich-message limits differ | S1 | HIGH |
| Message get/history | Product clients browse/search history | Current event is **NATIVE**; archive/history **UNSUPPORTED** | Updates retained ≤24 h; store needed events yourself. TDLib `getChatHistory` ≠ Bot API | S1,S8 | HIGH |
| Edit | Product supports edits | **API_LIMITED** `editMessageText/Caption/Media/ReplyMarkup`, rich and ephemeral variants | Generally own/inline messages; business messages not sent by bot and lacking inline keyboard have 48 h edit limit | S1 | HIGH |
| Delete | Product users/admins can delete according to context | **API_LIMITED** single/batch delete | Usually <48 h; special dice/service restrictions; broader deletion requires admin/business rights; batch 1–100 | S1 | HIGH |
| Reply/quote | Native replies and text quotes | **NATIVE** `ReplyParameters` | Cross-chat/topic and ephemeral constraints; quote must exactly match and ≤1024 chars | S1 | HIGH |
| Forward | Native provenance-preserving forward | **API_LIMITED** `forwardMessage(s)` | Service/protected content cannot be forwarded; batch skips unavailable items and preserves album grouping | S1,S9 | HIGH |
| Copy/repost | Product forwarding/copy semantics | **API_LIMITED** `copyMessage(s)` without original link | Service, paid media, giveaway and invoice types excluded; quizzes require known answers | S1 | HIGH |
| Rich text/mentions | Entities, custom emoji, rich blocks | **NATIVE / API_LIMITED** entities, HTML/Markdown, 10.1+ rich messages | Custom emoji may depend on bot-owner Premium/context; preserve UTF-16 entity offsets where specified | S1,S15 | HIGH |
| Reactions | Users react with supported emoji/custom emoji | **API_LIMITED** set/change reaction; reaction updates | Inbound user reaction requires bot admin + explicit allowed update; anonymous counts delayed; bots cannot use paid reactions | S1 | HIGH |
| Stickers/custom emoji | Static/animated/video sticker platform | **NATIVE / API_LIMITED** send and manage bot-created sets | New set is owned by a user; formats/size constraints; bot can edit set it created | S1,S3 | HIGH |
| Polls/quizzes | Native polls/quizzes | **NATIVE / API_LIMITED** send/stop, poll and answer updates | Bots receive votes only for their own non-anonymous polls; poll-state updates limited to bot polls/manually stopped polls | S1,S15 | HIGH |
| Cards/buttons/forms | Native keyboards, buttons; arbitrary Mini App UI | **NATIVE** reply/inline keyboards, callbacks; **EXTENSION** Mini Apps | Callback payload ≤64 bytes; answer callback promptly. Mini App data must be validated; launch mode controls chat access | S1,S3,S4 | HIGH |
| Inline mode | Invoke bot from any chat | **NATIVE / API_LIMITED** `inline_query`, `answerInlineQuery`, chosen-result feedback | Enable via BotFather; ≤50 results; feedback opt-in/sampled; user selects result | S1,S3,S5 | HIGH |
| Commands/deep links | Slash command UI/menu | **NATIVE** scoped/language commands and `start` parameters | Command up to 32 chars; received command must be reauthorized server-side; deep-link parameter ≤64 chars | S3 | HIGH |
| Images/audio/video/files | Product supports broad media and larger files | **NATIVE / API_LIMITED** specialized send methods, upload/reuse/URL, `getFile` | Hosted API: 20 MB download; multipart 10 MB photo/50 MB other; local server: 2,000 MB upload/unlimited download | S1,S13 | HIGH |
| Media groups/albums | Product albums | **NATIVE** `sendMediaGroup` | 2–10 items; audio/documents grouped only with same type; forward/copy batches preserve grouping | S1 | HIGH |
| Member directory | Product member UI | **API_LIMITED** admins/count/single member | No Bot API enumerate-all-members method; other-user lookup guaranteed only when bot admin | S1 | HIGH |
| Roles/permissions/admin | Granular rights in supergroups/channels | **NATIVE / API_LIMITED** permissions, promote/restrict/ban, tags, invite links, join requests | Bot must be admin and hold each corresponding right; basic-group rights are less granular | S1,S10 | HIGH |
| Moderation | Admins moderate messages/members | **NATIVE / API_LIMITED** delete, ban, restrict, approve/decline, pin, topic management, reactions cleanup | Rights and chat type gate every operation; no generic content-report action for bot moderation contract | S1 | HIGH |
| Audit log | Product/admin client has recent actions | **UNSUPPORTED** Bot API | MTProto `channels.getAdminLog` is a client API, not Bot API | S9,S11 | HIGH |
| Webhooks/event subscriptions | N/A product infrastructure | **NATIVE** webhook and allowed update filter | Webhook HTTPS, ports 443/80/88/8443, 1–100 connections, optional secret header; mutually exclusive with polling | S1 | HIGH |
| Long polling | N/A product infrastructure | **NATIVE** `getUpdates` | 1–100 updates, offset acknowledgment; mutually exclusive with webhook; recalculate offset to avoid duplicates | S1 | HIGH |
| Typing/read receipts | Product has typing and read marks | Outbound typing **API_LIMITED**; inbound typing/read **UNSUPPORTED** | `sendChatAction` lasts briefly; no ordinary update for user typing or message read | S1,S8,S13 | HIGH |
| Feed/post/follow/notification | Channels/stories and client notifications | Channel posts **NATIVE**; generic feed/follow/notification control **UNSUPPORTED** | Bot may send/broadcast where authorized; no subscriber-list/follow API. Stories mostly business/client scoped | S1,S9 | HIGH |
| Voice/video/live spaces | Product calls/group calls/voice chats | **UNSUPPORTED** for participation | Sending voice/video media is not joining a call. TDLib/MTProto call APIs are separate | S9,S13 | HIGH |
| Encryption | Cloud chats encrypted; secret chats E2EE | **API_LIMITED**: Bot API intermediary handles transport/encryption; no secret chat | HTTPS to Bot API; bot-visible plaintext is processed by developer; Mini Apps validate init data | S2,S4,S8 | HIGH |
| Federation/tenant/compliance | Centralized Telegram service | Federation **UNSUPPORTED**; tenant model **UNSUPPORTED** | Bot is global Telegram identity, not per-enterprise tenant. Developer privacy/retention/anti-scraping duties apply | S12 | HIGH |
| Idempotency/order | Client uses message IDs | **API_LIMITED** ingress dedupe via monotonic `update_id`; no documented generic outbound idempotency key | Persist update checkpoint; outbound retry may duplicate; batch methods may partially skip | S1 | HIGH |
| Pagination | Product/client lists/history | **API_LIMITED** method-specific offsets; updates max 100 | No common cursor and no Bot API chat-history pagination. TDLib history limit ≤100 is separate | S1,S8 | HIGH |
| Rate/passive window | Product anti-spam controls | **API_LIMITED** rate limits; no universal “24 h reply window” for normal bot chats | Approx. 1 msg/s/chat, 20/min group, ~30/s free bulk; 429/retry metadata. Business acting has its own recent-chat/access constraints | S2,S3 | HIGH |
| Payload/message limits | Product varies by client/tier | **API_LIMITED** | Normal text 1–4096; media captions commonly 0–1024; media group 2–10; inline ≤50; callback data 1–64 bytes; rich message has distinct larger structural limits | S1 | HIGH |
| Payments | Native invoice/checkout UI | **NATIVE / API_LIMITED** invoices, shipping, checkout, Stars/refund | Digital must use Stars; physical uses third-party provider; pre-checkout response ≤10 seconds; seller bears support/dispute duties | S6,S7,S12 | HIGH |
| Business/Secretary bot | User delegates selected business chats | **NATIVE / API_LIMITED** business connection/update and acting methods | Explicit connection, selected chats, rights (`can_reply`, etc.), bot modes/settings and recent-chat restrictions; never generic account takeover | S1,S3,S12 | HIGH |

## 7. Inbound event inventory

All are JSON `Update` variants delivered by **either** long polling or webhook [S1]:

- `message`, `edited_message` — new/edited private/group/supergroup message known to the bot; includes text/media and many nested service-message variants.
- `channel_post`, `edited_channel_post` — channel posts known to the bot.
- `business_connection`, `business_message`, `edited_business_message`, `deleted_business_messages` — delegated business-account lifecycle/message events.
- `guest_message` — opt-in guest-mode invocation from a chat where the bot is not a member.
- `message_reaction`, `message_reaction_count` — admin + explicit subscription required; anonymous counts may be delayed.
- `inline_query`, `chosen_inline_result` — inline invocation and optional feedback.
- `callback_query` — inline-keyboard/game callback; callback may identify a normal or inline message.
- `shipping_query`, `pre_checkout_query`, `purchased_paid_media` — commerce workflow.
- `poll`, `poll_answer` — constrained to bot polls/manual stop and the bot's non-anonymous polls as described above.
- `my_chat_member`, `chat_member`, `chat_join_request` — membership/status/join flow; `chat_member` requires admin + explicit subscription, join request requires invite right.
- `chat_boost`, `removed_chat_boost` — admin-gated boost lifecycle.
- `managed_bot` — managed-bot create/token/owner lifecycle.
- `subscription` — user payment-subscription change.

**Delivery semantics:** updates are retained at most 24 hours. `update_id` is sequential during normal activity and supports deduplication/checkpointing; after a week without updates, the next ID may be random. `getUpdates(offset)` acknowledges earlier items. Webhooks are HTTPS POSTs, retry non-2xx “a reasonable amount of attempts,” and can carry an `X-Telegram-Bot-Api-Secret-Token`. The docs do not promise exactly-once delivery, a fixed retry schedule, or a dead-letter endpoint [S1]. Privacy Mode and chat/admin rights determine whether an otherwise possible message/event reaches the bot [S3].

## 8. Outbound operation inventory

- **Identity/config:** `getMe`; set name/description/short description/profile photo; set/delete/get commands; default admin rights; menu button; webhook inspection/configuration [S1].
- **Messaging:** send normal/rich text and drafts; photo, audio, document, video, animation, voice, video note, sticker, live photo, paid media, location/live location, venue, contact, dice, poll, game, invoice, album; send chat action [S1][S15].
- **Message lifecycle:** edit text/rich text, caption, media, live location and reply markup; stop live location/poll; delete single/batch/ephemeral; forward/copy single/batch; pin/unpin [S1].
- **Interaction:** answer callback, inline, Web App, guest, shipping, pre-checkout and join-request queries; prepare/share inline messages; deep links are client URL conventions [S1][S3][S4].
- **Reactions/stickers:** set/delete reactions; retrieve sticker sets/custom emoji; upload/create/add/replace/delete/reorder sticker set members [S1].
- **Chat/member/admin:** get chat/admins/count/member; leave; permissions; promote/restrict/ban/unban; invite links and join requests; title/description/photo; forum topics and member tags [S1].
- **Business/commerce:** business-scoped send/edit/delete/profile/gifts/stories where the specific method and granted business right allow it; invoices, Stars balance/transactions, refund, gifts/subscriptions [S1][S7].
- **Files:** reuse `file_id`, pass supported HTTP URL, multipart upload, `getFile` then HTTPS download; local Bot API server changes transport limits [S1].

**Results/errors/asynchrony:** normal HTTPS methods return `{ok,result}` on success or `{ok:false,error_code,description,parameters?}`; `ResponseParameters` can direct migration or retry. Most sends synchronously return a `Message` (albums return arrays; boolean/config methods return their declared result). Webhook-response method calls intentionally provide no success/result signal. TDLib's fully asynchronous request/update model does **not** describe the Bot API [S1][S8]. No generic outbound idempotency token is documented; adapters should treat uncertain retries as potentially duplicating side effects.

## 9. Normalized common-capability candidates

These are safe cross-platform candidates if kept capability-discoverable and rights-aware:

1. `Messaging.SendText`, `SendMedia`, `SendAlbum`, `Reply`, `EditOwn`, `Delete`, `Forward`, `Copy` — preserve platform message/chat IDs and per-operation constraints [§6].
2. `Ingress.EventStream` with `Webhook` and `LongPoll` transport choices, checkpoint/dedup metadata and raw event payload [§7].
3. `Interactions.Command`, `Button`, `Callback`, `Poll`, `Reaction` — optional sub-capabilities; do not assume every reaction/poll update is observable [§6].
4. `Conversation.Direct`, `Group`, `BroadcastChannel`, `Thread` — generalizable shapes, while Telegram-specific forum/direct-message IDs remain extensions [§5–6].
5. `Membership.GetMember`, `Count`, `ListAdmins`, `Restrict`, `Ban`, `Promote`, `JoinRequest` — never imply `ListAllMembers` [§6].
6. `Files.Upload`, `ReuseRemoteHandle`, `Download` with dynamic maximum sizes and transport mode [§6].
7. `Commerce.Invoice`, `ShippingQuery`, `PreCheckout`, `PaymentSucceeded`, `Refund` — provider/currency policy is adapter metadata [§6–8].
8. `CapabilitySnapshot` should include chat type, bot membership/admin rights, privacy exposure, enabled modes (inline/business/threaded/guest), file transport and current limits.

## 10. Required namespaced extensions

- `telegram.forum.message_thread_id`, `telegram.forum.private_threaded_mode` — Telegram topic identity, General-topic rules and BotFather/fee settings are not generic threads [S14][S15].
- `telegram.channel_direct_messages_topic_id` and `telegram.community` — platform-specific topology introduced in recent Bot API versions [S1][S15].
- `telegram.inline.*` — query cache time, chosen-result sampling, switch-to-PM/Mini-App behavior and inline-message IDs [S1][S5].
- `telegram.mini_app.*` — launch mode, signed `initData`, theme/webview/device APIs, `query_id` and attachment-menu approval [S4].
- `telegram.business.*` — `business_connection_id`, delegated rights and business-message lifecycle [S1][S3][S12].
- `telegram.guest.*`, `telegram.ephemeral.*`, `telegram.managed_bot.*`, `telegram.bot_to_bot.*` — new and nonportable interaction/identity semantics [S3][S15].
- `telegram.stars.*`, paid media/subscriptions/gifts and paid broadcast — commercial semantics and eligibility should not leak into a universal currency/rate contract [S1][S7][S12].
- `telegram.file_id` / `file_unique_id` — reusable platform handles with different meanings; retain both.
- A typed/raw `telegram.Update` and method escape hatch is justified for fast Bot API evolution, but must still pass policy, rights and allow-list checks.

## 11. Limits, policy, review, regional and lifecycle risks

- **Rate/flood:** model rate limits as retryable dynamic constraints, using 429 and `retry_after`; do not hard-code FAQ numbers as guarantees or try to evade limits [S1][S2][S12].
- **Files:** hosted and local-server modes have very different limits. Product file size is not the Bot API upload/download size [S1][S13].
- **Visibility/privacy:** Privacy Mode, bot admin status, explicit update subscriptions and channel membership alter ingress. A “connected” bot is not necessarily observing all chat traffic [S1][S3].
- **Payments:** digital sales inside Telegram require Stars; physical sales depend on third-party providers/regions. Pre-checkout timing, refund/support and dispute duties are application obligations [S6][S7][S12].
- **Mini App availability/review:** attachment-menu integration is restricted to major Telegram advertisers in production; client versions control JS feature availability. Validate signed init data server-side [S4].
- **Business:** connection owners choose chats and rights; data may only be used for the authorized business purpose. Do not conceal bot activity [S12].
- **Data:** publish a privacy policy, minimize/secure/delete user data, and do not scrape public groups/channels into datasets or models [S12].
- **Lifecycle:** current 10.x added several major modes in months. Pin generated DTOs to a Bot API version, tolerate unknown fields/update kinds, and monitor `@BotNews`/changelog [S1][S15].
- **Regional/client risk:** terms explicitly allow feature availability to differ or change by user, client and geography without advance notice [S12].
- **Commercial values:** paid-broadcast thresholds, costs, Stars conversion and fees can change; discover/configure them rather than making them protocol constants [S2][S12].

## 12. Conflicts and unknowns

1. **Bot-to-bot conflict:** the older Bot FAQ says bots never see other bots. Bot API 10.0 and current Bot Features now permit opt-in bot-to-bot communication in specified private/group/business contexts. Use current S1/S3/S15; treat FAQ wording as stale.
2. **File-limit confusion:** Telegram consumer clients support files much larger than hosted Bot API upload/download limits, and the official local Bot API server changes limits again. Report limits by surface, never as one Telegram-wide number [S1][S13].
3. **Webhook retry semantics:** “reasonable amount of attempts” is not a reproducible retry schedule; maximum attempts/backoff/dead-letter behavior are **UNKNOWN** [S1].
4. **Global method rate table:** Telegram gives practical FAQ guidance and 429 retry metadata, not a complete stable per-method quota specification. Exact quotas are **UNKNOWN/dynamic** [S1][S2].
5. **Full member list:** absence of a Bot API enumeration method is clear in the current reference, but Telegram does not provide a separate explicit “unsupported” statement. Capability remains **UNSUPPORTED for ordinary Bot API**, while client/member visibility may also be admin-hidden [S1][S10].
6. **History retention for bot cloud storage:** the Bots overview says older processed messages may be removed “shortly,” while updates are definitively queued ≤24 h. No precise post-processing server retention SLA for bot access is published; adapters must persist required data [S1][S2].
7. **New 2026 modes:** guest, ephemeral, communities, rich messages, managed bots and private threaded mode may have uneven client adoption. Exact minimum client matrices are **UNKNOWN**; feature-probe and degrade [S4][S15].

## 13. OBCX design implications

1. **Split adapters by authority boundary.** Define `TelegramBotApiAdapter` for token-authenticated S1 operations and, only if explicitly in scope, a separately deployed `TelegramTdlibClientAdapter` with user authorization, encrypted local database and stronger policy controls. Never silently fall back from bot to user credentials [S8][S9].
2. **Use process-owned transport actors.** A Telegram ingress process owns HTTPS/webhook or long-poll state, token secrets, `update_id` checkpoints, retry/backoff and file transfer. Business actors receive serializable typed events such as `TelegramMessageReceived`, `TelegramCallbackReceived`, `TelegramMembershipChanged` and `TelegramPaymentQuery` plus raw update/version [S1].
3. **Make ingress acknowledgment explicit.** Persist `(bot_id, update_id)` before/with dispatch; acknowledge long-poll offsets only after durable acceptance. Webhook handlers should return 2xx after durable enqueue and deduplicate retries [§7].
4. **Do not create a giant `IBot`.** Offer optional capabilities (`IMessaging`, `IMessageMutation`, `IReactionWriter`, `IPollSender`, `IChatModerator`, `IInlineResponder`, `IPaymentMerchant`, `IBusinessDelegate`, `IForumTopics`) returned by discovery for the current bot/chat/rights [§6].
5. **Model Telegram identity precisely.** Use 64-bit-safe IDs, composite message key `(chat_id,message_id)`, optional `message_thread_id`, `direct_messages_topic_id`, `business_connection_id`, inline message ID and ephemeral ID. Keep `file_id` separate from stable-ish `file_unique_id` [S1].
6. **Represent partial observability.** A chat capability snapshot must expose `privacy_mode`, bot membership/admin rights, allowed update types and enabled modes. “Can send reaction” and “can observe all reaction events” are separate flags [S1][S3].
7. **No history/member-list assumptions.** Maintain an event store/index for messages OBCX needs later; expose Bot API history and full-member enumeration as unavailable rather than empty results. TDLib history must remain a different adapter capability [S1][S8].
8. **Use typed result unions.** Normalize success, `RateLimited(retry_after)`, `ChatMigrated(new_chat_id)`, permission denied, validation/payload too large, not found, protected content, and uncertain transport outcome. Retain Telegram error description/raw parameters because error codes/details can evolve [S1].
9. **Assume outbound retries can duplicate.** Generate an OBCX operation ID for tracing/dedup at the actor boundary, but do not claim Telegram honors it. Where a timeout follows a send, reconcile from stored returned IDs/events when possible or surface `OutcomeUnknown` [S1].
10. **Isolate platform UX/payment extensions.** Mini Apps, inline caching, Stars, business delegation, guest/ephemeral and forum management remain `telegram.*` DTOs/commands. Common contracts should carry only portable message/interaction/payment phases [§9–10].
11. **Externalize limits and policy.** Store default limits from §11 as mutable adapter configuration, honor Telegram `retry_after`, and capability-probe client/mode availability. Never encode paid prices/eligibility as stable API constants [S2][S12][S15].
12. **Enforce data purpose/retention.** Tag business-message and Mini-App-origin data with consent/purpose; support deletion workflows and prohibit bulk public-channel/group scraping in adapter policy [S12].

## 14. Claim-to-source checklist

| Conclusion | Source IDs |
|---|---|
| E1 bot token identity, not user account | S1,S2,S8,S9 |
| E2 polling/webhook exclusivity and 24 h queue | S1 |
| E3 no Bot API history; TDLib history exists | S1,S8 |
| E4 topology and rights | S1,S10,S14 |
| E5 Privacy Mode | S3 |
| E6 broad messaging with restrictions | S1,S9 |
| E7 member/admin capability but no directory/admin log | S1,S10,S11 |
| E8 inline/buttons/Mini Apps and restrictions | S3,S4,S5 |
| E9 payment split and timing | S6,S7,S12 |
| E10 delegated business bots are scoped Bot API | S1,S3,S12 |
| E11 hosted/local file limits | S1 |
| E12 unsupported bot features versus TDLib/client | S8,S9,S13 |
| E13 rate limits are dynamic/policy-sensitive | S1,S2,S12 |
| D1 separate Bot API and TDLib adapters | S8,S9 |
| D2–D3 process-owned transport/checkpointing | S1 |
| D4 optional capability interfaces | S1,S3,S6,S7 |
| D5 Telegram identity/DTO fields | S1 |
| D6 partial observability/privacy | S1,S3 |
| D7 no history/member assumptions | S1,S8,S10 |
| D8 typed errors/retry/migration | S1 |
| D9 no claimed outbound idempotency | S1 |
| D10 namespaced UX/payment/business/topic features | S3,S4,S7,S14,S15 |
| D11 mutable limits/commercial policy | S2,S12,S15 |
| D12 data purpose, retention and anti-scraping | S12 |
