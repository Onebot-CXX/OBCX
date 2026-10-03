# Research: Feishu/Lark (飞书/Lark) platform capability audit

## 1. Lane metadata

- **Platform:** China Feishu (飞书) and international Lark.
- **Scope:** official app bots and Open Platform server APIs; group custom-webhook bots are called out separately. IM, cards, media, contacts/org, events, and the extension implications of Calendar/Docs are covered.
- **Research date / access date:** 2026-08-17 UTC.
- **Freshness caveat:** Feishu documentation has current `server-docs` pages plus older opaque-path/reference pages; some English Lark pages are catalog/reference mirrors. The two products use parallel APIs but no official parity matrix was found. Every target deployment must re-check its endpoint in the appropriate console/API Explorer.
- **Assessment vocabulary:** `NATIVE`, `API_LIMITED`, `EMULATED`, `EXTENSION`, `UNSUPPORTED`, `UNKNOWN` as defined in the lane brief.

## 2. Executive findings

1. Feishu and Lark are separate service environments: use `open.feishu.cn` or `open.larksuite.com` respectively, with separate app credentials/tenants; common `/open-apis/...` paths do not prove cross-region credential or feature portability [S1, S2].
2. The useful automation identity is an **application bot** backed by an app registration. `tenant_access_token` acts as the app in one tenant; `user_access_token` acts for an OAuth-authorizing user. Scope, data range, resource ACL, app availability, and tenant review are separate gates [S3–S6].
3. A **group custom bot webhook is not an app bot**: it is outbound-only, one-group scoped, cannot consume message/events, and its cards are static [S7].
4. Official IM APIs natively send/get/list/reply/forward/recall messages, with thread containers and native reply relationships. Editing is limited to the caller's own `text`/`post` messages and 20 edits [S8–S13].
5. Product-native message types exceed bot-send types. Bots can send text, post, image, file, audio, media/video, sticker, interactive card, chat/user shares; they cannot officially create native polls or calls even though those message types can be read [S8, S14].
6. Reactions, pins, member management, posting moderation, announcements, and constrained read receipts have official APIs. Read events apply only to bot-sent P2P messages; reader lookup is only for bot-sent messages and within seven days [S15–S19].
7. Cards are the primary workflow surface: buttons/selectors/forms generate `card.action.trigger`; callbacks must receive HTTP 200 within three seconds, with constrained deferred/update tokens. Card JSON 2.0 is a rich but platform-specific extension [S20–S22].
8. IM media uses upload-generated `image_key`/`file_key`; message-bound resource download requires chat access. Limits include 10 MB for image upload and 100 MB for the message-resource download endpoint [S23–S25].
9. Contacts are hierarchical tenant data, not a public social graph. App-token results are restricted by API/field scopes and configured Contact data scope; user-token results are restricted by the user's org visibility [S26, S27].
10. Event ingress supports HTTPS callbacks or official SDK WebSocket long connections. V2 envelopes carry unique `event_id`; duplicate delivery must be expected and v1+v2 subscriptions can themselves duplicate events [S28, S29].
11. Rate limiting is endpoint-specific. Normal message send/reply documents 1,000/minute and 50/second plus delivery throttles of 5 QPS per target user/group; `uuid` deduplicates successful sends for one hour [S8, S9].
12. Calendar and Docs are valuable namespaced extensions, not implicit IM capabilities: Calendar change subscription is user-token-only; Docs access requires both scope and a document ACL/application grant, and document subscriptions are resource-scoped [S30–S34].
13. No supported bot APIs were found for presence, typing state, native follow/feed/repost, call initiation, live spaces, or federation. Capability discovery should report these absent rather than expose inert methods [S14, S35].

## 3. Research action log

- Read the lane brief in full before research.
- Queried official Feishu/Lark domains in four clusters: (a) IM messages/chats/reactions/pins/media; (b) identity/tokens/app review/regions; (c) cards/callbacks/forms/events; (d) Contacts, Calendar, Docs, limits and unsupported real-time/social surfaces.
- Entered the official IM API index, event list, token terminology, app-scope/review, Contact scope, card callback, Calendar subscription and Docs permission pages.
- Cross-checked Lark's official server API catalog and SDK domain selection against Feishu pages. Most detailed current pages indexed under Feishu; claims of Lark parity were therefore deliberately avoided unless a Lark source was found.
- Search content fetches for dynamic documentation reported 0 fetched bodies in several batches; search index excerpts and directly indexed official pages remained available. Exact Lark parity, commercial quotas, encryption architecture, audit API, and several product-only UI features remain `UNKNOWN`.
- Dropped SEO tutorials and unofficial SDK/client automation. No secondary source is relied on.

## 4. Source register

All sources accessed 2026-08-17.

| ID | Authority | Title / relevant section | URL | What it proves (precise paraphrase) |
|---|---|---|---|---|
| S1 | OFFICIAL | Feishu SDK: Call server API / domain | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/server-side-sdk/python--sdk/invoke-server-api?lang=en-US | SDK selects Feishu vs Lark API domain; app ID/secret configure client. |
| S2 | OFFICIAL | Lark server API list | https://open.larksuite.com/document/ukTMukTMukTM/uYTM5UjL2ETO14iNxkTN/server-api-list | Lark's official API catalog and `/open-apis` surface. |
| S3 | OFFICIAL | App types: custom and store apps | https://open.feishu.cn/document/home/app-types-introduction/self-built-apps-and-store-apps | Custom apps are tenant-specific; store apps are marketplace distributed. |
| S4 | OFFICIAL | API scopes / request permissions | https://open.feishu.cn/document/server-docs/application-scope/introduction?lang=en-US | Permission requests and tenant/platform approval gates. |
| S5 | OFFICIAL | API terminology / access tokens | https://open.feishu.cn/document/server-docs/api-call-guide/terminology?lang=zh-CN | Tenant token represents app in tenant and is short lived; user token represents user. |
| S6 | OFFICIAL | Configure app data permissions | https://open.feishu.cn/document/home/introduction-to-scope-and-authorization/configure-app-data-permissions | Sensitive resource range is an additional gate beyond API scope. |
| S7 | OFFICIAL | Custom bot usage guide | https://open.feishu.cn/document/ukTMukTMukTM/ucTM5YjL3ETO24yNxkjN?lang=zh-CN | One-group outbound webhook; no events/data permissions; 100/min, 5/sec, 20 KB; signature/IP/keyword; static cards. |
| S8 | OFFICIAL | Send message (`POST /im/v1/messages`) | https://open.feishu.cn/document/server-docs/im-v1/message/create?lang=zh-CN | Send types, identifiers, `uuid`, payload and rate limits. |
| S9 | OFFICIAL | Reply message | https://open.feishu.cn/document/server-docs/im-v1/message/reply?lang=zh-CN | Native reply, `reply_in_thread`, send limits and idempotency. |
| S10 | OFFICIAL | List chat history | https://open.feishu.cn/document/server-docs/im-v1/message/list?lang=zh-CN | Chat/thread containers, time/sort/pagination; thread retrieval behavior. |
| S11 | OFFICIAL | Edit message | https://open.feishu.cn/document/server-docs/im-v1/message/update | Only own text/post; max 20; not recalled/expired. |
| S12 | OFFICIAL | Recall message | https://open.feishu.cn/document/server-docs/im-v1/message/delete?lang=zh-CN | Recall semantics, ownership/role and age constraints. |
| S13 | OFFICIAL | Forward message | https://open.feishu.cn/document/server-docs/im-v1/message/forward | Forward API and excluded message types. |
| S14 | OFFICIAL | Receive-message content structures | https://open.feishu.cn/document/server-docs/im-v1/message-content-description/message_content | Readable types include vote/video_chat/system/location beyond outbound types. |
| S15 | OFFICIAL | Reaction create / event | https://open.feishu.cn/document/server-docs/im-v1/message-reaction/create?lang=zh-CN | Add reaction API and chat/recalled/system constraints. |
| S16 | OFFICIAL | Pin/list pins | https://open.feishu.cn/document/server-docs/im-v1/pin/list | Paginated pinned messages for a chat; bot must belong. |
| S17 | OFFICIAL | Chat/member overview and add members | https://open.feishu.cn/document/server-docs/group/chat-member/create?lang=en-US | Add up to 50 users or 5 bots; role/invite restrictions and serialization caveat. |
| S18 | OFFICIAL | Update chat moderation | https://open.feishu.cn/document/server-docs/group/chat/update | Posting policies and owner/creator/operate-as-owner rules. |
| S19 | OFFICIAL | Message read event | https://open.feishu.cn/document/server-docs/im-v1/message/events/message_read?lang=zh-CN | Read event limited to bot-sent P2P; reader query limitations. |
| S20 | OFFICIAL | Card callback communication | https://open.feishu.cn/document/feishu-cards/card-callback-communication?lang=zh-CN | `card.action.trigger`, 3-second response, action/form values and update token. |
| S21 | OFFICIAL | Card JSON 2.0 release notes | https://open.feishu.cn/document/feishu-cards/card-json-v2-breaking-changes-release-notes | 14-day interactivity/update lifetime and JSON 2.0 constraints. |
| S22 | OFFICIAL | Card input/form component | https://open.feishu.cn/document/feishu-cards/card-json-v2-components/interactive-components/input | Named form fields, grouped submit, required validation. |
| S23 | OFFICIAL | Upload image | https://open.feishu.cn/document/server-docs/im-v1/image/create | Multipart IM image upload, formats, 10 MB, returns `image_key`. |
| S24 | OFFICIAL | Upload file | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/reference/im-v1/file/create | Upload audio/video/file and receive `file_key`. |
| S25 | OFFICIAL | Get resource from message | https://open.feishu.cn/document/server-docs/im-v1/message/get-2?lang=zh-CN | Message-bound image/audio/video/file binary retrieval, chat membership, 100 MB. |
| S26 | OFFICIAL | Contact permission range | https://open.feishu.cn/document/server-docs/contact-v3/scope/scope_authority?lang=zh-CN | App data scope vs user org-visibility scope; department inheritance. |
| S27 | OFFICIAL | Get user | https://open.feishu.cn/document/server-docs/contact-v3/user/get | User/field permission and out-of-scope behavior. |
| S28 | OFFICIAL | Event overview | https://open.feishu.cn/document/server-docs/event-subscription-guide/overview?from=from_parent_docs | HTTPS POST and SDK long-connection/WebSocket delivery. |
| S29 | OFFICIAL | Configure/subscription event case | https://open.feishu.cn/document/server-docs/event-subscription-guide/event-subscription-configure-/subscription-event-case?lang=zh-CN | V2 `event_id`, app/user identity visibility, v1+v2 duplicate warning. |
| S30 | OFFICIAL | Calendar event create | https://open.feishu.cn/document/server-docs/calendar-v4/calendar-event/create | App/user CRUD, calendar role and idempotency requirements. |
| S31 | OFFICIAL | Subscribe calendar changes | https://open.feishu.cn/document/server-docs/calendar-v4/calendar/subscription?lang=zh-CN | Calendar subscriptions require user token; endpoint-specific limits. |
| S32 | OFFICIAL | Docx overview | https://open.feishu.cn/document/server-docs/docs/docs/docx-v1/docx-overview | Documents are block trees with read/mutation APIs. |
| S33 | OFFICIAL | Docs permission overview | https://open.feishu.cn/document/server-docs/docs/permission/overview | Scope alone does not grant document access; resource ACL/application grant applies. |
| S34 | OFFICIAL | Subscribe cloud-document events | https://open.feishu.cn/document/server-docs/docs/drive-v1/event/subscribe?lang=zh-CN | Per-resource owner/manager subscription and event limits. |
| S35 | OFFICIAL | Bot overview | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/bot-v3/bot-overview | Documented bot surface: conversations, messages/cards, custom menus; no presence/typing/call API. |
| S36 | OFFICIAL | IM event list | https://open.feishu.cn/document/ukTMukTMukTM/uYDNxYjL2QTM24iN0EjN/event-list | Enumerates IM/chat/contact event names. |
| S37 | OFFICIAL | Create chat / group overview | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/reference/im-v1/chat/create | API-created group and chat/thread message form; topic-group creation limitation. |
| S38 | OFFICIAL | Message overview / urgent APIs | https://open.feishu.cn/document/uAjLw4CM/ukTMukTMukTM/reference/im-v1/introduction | In-app/SMS/phone urgent APIs, batch operations and reader/card-related inventory. |

## 5. Product vs official API boundary

| Product capability | Official automation boundary | Assessment |
|---|---|---|
| Users chat in P2P and groups; groups may use ordinary/chat, thread-form, or product topic forms. | Apps can message P2P/group, create group chats and create thread replies. API can create only `group`; true `topic` groups require the client, though a group can be created with `group_message_type=thread` [S9, S10, S37]. | Product: `NATIVE`; API: `API_LIMITED` |
| Users create/edit/forward/recall many message types. | Bots send a documented subset; edits only own text/post; recall/forward are policy/type constrained [S8, S11–S14]. | `API_LIMITED` |
| Native polls and meeting/call messages exist. | Poll and `video_chat` structures are readable but absent from bot-send types; card-emulated polls are not native polls [S8, S14]. | Product: `NATIVE`; API: `UNSUPPORTED` / poll workflow `EMULATED` |
| Reactions, pins, replies, rich posts, stickers, media. | Official reaction/pin/reply/post/sticker/media APIs exist, each with membership/ownership/key constraints [S9, S15, S16, S23–S25]. | Mostly `NATIVE`; stickers/media `API_LIMITED` |
| Interactive cards/forms. | App bots send interactive messages and receive card callbacks. Custom-webhook-bot cards cannot callback or update [S7, S20–S22]. | App bot `NATIVE`; webhook bot `API_LIMITED` |
| Org contacts and departments. | Contact v3 reads/writes/events exist but are tenant/admin/data-scope controlled. This is not a follow graph [S26, S27, S36]. | `API_LIMITED` |
| Read state. | P2P bot-message read events and time-limited reader lookup; no general group read stream [S19]. | `API_LIMITED` |
| Calendar and Docs. | Extensive separate APIs, ACLs and resource subscriptions; not implicit permissions of an IM bot [S30–S34]. | `EXTENSION` |
| Presence, typing, calls/live. | No supported server bot operation found. Media messages are not calls [S14, S35]. | API: `UNSUPPORTED` (product presence/calls likely native; exact UI audit not performed) |
| Group custom bot. | Webhook posts only to its configured group; no inbound/user/group APIs [S7]. | `API_LIMITED`, separate adapter mode |

## 6. Capability evidence table

| Surface | Product support | Official API support/status | Restrictions / semantics | Evidence | Confidence |
|---|---|---|---|---|---|
| App/bot identity | App bot installable in tenant | `NATIVE` | Custom vs store app; one app registration has app bot identity; tenant availability/admin gates | S3–S6, S35 | HIGH |
| Authentication / tenant | Tenant and user authorization | `API_LIMITED` | tenant token = app in tenant; user OAuth token = user; short-lived credentials; credentials not shown portable Feishu↔Lark | S1–S6 | HIGH |
| Multi-account / multiple tenants | Store app can install across tenants | `API_LIMITED` | Keep tenant-keyed tokens and installations. Multiple bot personas under one app: `UNKNOWN` | S3, S5 | MEDIUM |
| Bot status/presence | Product users have status-related UI | `UNSUPPORTED` | No server bot presence/status API found | S35 | MEDIUM |
| User/profile | Tenant directory profiles | `API_LIMITED` | Field scopes and data range; ID types (`open_id`, `user_id`, `union_id`) require explicit modeling | S26, S27 | HIGH |
| Follow/social graph | Not core enterprise-directory model | `UNSUPPORTED` | Contact hierarchy/group membership is not follow/follower semantics | S26 | HIGH |
| DM/P2P | Native | `NATIVE` | Bot may DM users in app availability/data context; message permissions apply | S8, S35 | HIGH |
| Group/room/chat | Native | `API_LIMITED` | Create/get/update/list, join/member APIs; membership and owner/admin policy gates | S17, S18, S37 | HIGH |
| Guild/server/space/channel | No direct matching hierarchy | `EMULATED` only as generic conversation | Do not map tenant→guild or department→channel without namespaced semantics | S26, S37 | MEDIUM |
| Threads/topics | Native thread-form and topic product modes | `API_LIMITED` | Reply with `reply_in_thread`; history container `thread`; true topic group not API-creatable | S9, S10, S37 | HIGH |
| Message create | Native | `NATIVE` for supported types | `receive_id_type`; JSON-string `content`; scope, chat membership/availability, limits | S8 | HIGH |
| Get / history | Native history | `API_LIMITED` | Bot must have conversation/data access; paginated; thread replies require thread container | S10 | HIGH |
| Edit | Native | `API_LIMITED` | Only operator's own text/post, max 20 edits, not recalled/expired | S11 | HIGH |
| Delete | Recall product semantic | `API_LIMITED` | Recall, not arbitrary hard delete; ownership/admin and age rules | S12 | HIGH |
| Reply / quote | Native | `NATIVE` reply; quote representation `API_LIMITED` | parent/root/thread IDs must be retained; no separately proven arbitrary quote-send API | S9, S10 | HIGH/MEDIUM |
| Forward | Native | `API_LIMITED` | Excludes vote, voice, system, encrypted and other documented types | S13 | HIGH |
| Rich text / mentions | Native | `NATIVE` | `post` and text mention encoding; payload limits | S8, S14 | HIGH |
| Reactions | Native | `NATIVE` | Add/list/delete; in-chat; no recalled/system messages; created/deleted events | S15, S36 | HIGH |
| Stickers | Native | `API_LIMITED` | Bot may send sticker using a sticker `file_key` it received; download unavailable | S8, S14 | MEDIUM |
| Polls | Native product vote | `UNSUPPORTED`; card poll `EMULATED` | Vote readable but not bot-send type | S8, S14, S20 | HIGH |
| Cards/buttons/forms | Native product card renderer | `NATIVE` for app bot; `EXTENSION` contract | callback 3s; JSON 2.0 lifetime/update/form constraints; custom webhook static | S7, S20–S22 | HIGH |
| Image | Native | `NATIVE` | Upload to key; 10 MB image upload; access restrictions | S23, S25 | HIGH |
| Audio/video/file | Native | `API_LIMITED` | Upload to file key; video is `media`; message-bound download and size/access constraints | S24, S25 | HIGH |
| Media groups/albums | Product may group visuals | `UNKNOWN` | No official atomic media-group send operation evidenced | — | LOW |
| Pins | Native | `NATIVE` | create/list/delete; in-chat and role policy | S16 | HIGH |
| Members | Native | `API_LIMITED` | list/add/remove/join; max 50 users or 5 bots per add; serialize mutation | S17 | HIGH |
| Roles / permissions | Owner/admin/moderator behaviors | `API_LIMITED` | Not a generic RBAC system; group owner/admin and posting moderation APIs | S17, S18 | HIGH |
| Moderation | Native group controls | `API_LIMITED` | posting policy only in studied surface; broad content moderation API not established | S18 | MEDIUM |
| Audit/compliance export | Product/admin capabilities may exist | `UNKNOWN` | No official general audit-log/export API established in this lane | — | LOW |
| Commands | Bot menu + text messages | Menu `EXTENSION`; slash commands `UNSUPPORTED` | `application.bot.menu_v6`; applications can parse messages but that is not native slash-command registration | S35, S36 | HIGH |
| Interactions | Cards/menu | `NATIVE`/`EXTENSION` | typed card action and menu events; rapid acknowledgement | S20, S35, S36 | HIGH |
| Webhooks/events | Native Open Platform | `NATIVE` | HTTPS POST or official SDK long connection; dedupe event ID | S28, S29 | HIGH |
| Presence/typing | Product feature not fully audited | `UNSUPPORTED` API | No operation/event found | S35 | MEDIUM |
| Read receipt | Native | `API_LIMITED` | Bot-sent P2P event; lookup only own sent message and seven-day window | S19 | HIGH |
| Feed/post/repost/quote/follow | Not core IM surface | `UNSUPPORTED` | `post` means rich-text message, not social feed post | S8, S14 | HIGH |
| Notification/urgent | Native urgency | `API_LIMITED` / `EXTENSION` | in-app, SMS, phone urgency only on current bot's normal message; target/conversation, scope and enterprise quota constraints | S38 | HIGH |
| Voice/video/live/space | Product meetings exist | Bot call initiation `UNSUPPORTED` | Audio/video file messages supported; do not equate with a call/live session | S14, S24 | HIGH |
| Encryption | Product security exists, exact mode not audited | `UNKNOWN` | Forward docs mention “encrypted message” exclusion, but no bot-visible E2EE/key API established | S13 | LOW |
| Federation | No evidence | `UNSUPPORTED` as official bot capability | Separate Feishu/Lark environments are not federation | S1, S2 | MEDIUM |
| Tenant/compliance | Tenant-scoped enterprise platform | `API_LIMITED` | Region/domain, admin review, availability, scopes and data range are independent controls | S1–S6 | HIGH |
| Idempotency | — | `NATIVE` per operation | Message/reply UUID succeeds at most once/hour; chat create UUID window differs; not universal | S8, S9, S37 | HIGH |
| Pagination | — | `NATIVE` | history, pins, members, contacts use page token/size patterns; endpoint DTOs vary | S10, S16, S17, S26 | HIGH |
| Rate limits | — | `API_LIMITED` | Endpoint-specific; send/reply 1,000/min, 50/sec and 5 QPS target; custom webhook 100/min, 5/sec | S7–S9 | HIGH |
| Passive reply window | — | `UNKNOWN` / generally not modeled | No universal “reply within N hours” rule found; card action response is 3 seconds, distinct from messaging | S20 | MEDIUM |
| Payload/message limits | Product renderer | `API_LIMITED` | send: text 150 KB; post/card 30 KB; custom webhook 20 KB; card component/lifetime limits | S7, S8, S21 | HIGH |

## 7. Inbound event inventory

Delivery is either **HTTP POST callback** or official SDK **WebSocket long connection** [S28]. Prefer V2 envelopes; deduplicate on `header.event_id`; do not subscribe to equivalent v1 and v2 together [S29]. Availability always depends on event subscription, bot capability, scopes, app/user identity, app visibility and resource access.

| Family | Official events evidenced | Important boundary |
|---|---|---|
| Messages | `im.message.receive_v1`, `im.message.recalled_v1`, `im.message.message_read_v1` | Receive visibility differs for P2P, group @mention, broader group scopes; read only bot-sent P2P [S19, S36]. |
| Reactions | `im.message.reaction.created_v1`, `im.message.reaction.deleted_v1` | Chats containing the bot [S15, S36]. |
| Chat lifecycle/config | `im.chat.disbanded_v1`, `im.chat.updated_v1` | Includes relevant group configuration/ownership changes; subscription visibility applies [S36]. |
| Chat users | `im.chat.member.user.added_v1`, `.deleted_v1`, `.withdrawn_v1` | Joined/removed/left/invitation withdrawal [S36]. |
| Chat bots | `im.chat.member.bot.added_v1`, `.deleted_v1` | Removed event may be visible only to removed bot; do not assume global roster audit [S36]. |
| P2P entry | `im.chat.access_event.bot_p2p_chat_entered_v1` | Product-specific signal that user entered bot DM [S36]. |
| Card action | `card.action.trigger` | Buttons, selects and form submit; 3-second HTTP acknowledgement contract [S20]. |
| Bot menu | `application.bot.menu_v6` | Custom/self-built app bot menu event; platform-namespaced [S35, S36]. |
| Contacts | `contact.user.created_v3`, `.updated_v3`, `.deleted_v3`; `contact.department.created_v3`, `.updated_v3`, `.deleted_v3` | Restricted to authorized Contact data/fields [S26, S36]. |
| Calendar/Docs | Calendar and resource-specific Drive/Docs change events | Calendar subscription user-token only; Docs per-resource owner/manager subscription [S31, S34]. |

No general typing/presence event, native poll-vote event usable as a bot poll framework, or call-control event was established.

## 8. Outbound operation inventory

- **Tokens/auth:** exchange app credentials for tenant/app tokens; OAuth authorization code/refresh for user token. Responses use platform error codes; cache tokens by environment/app/tenant/identity [S5].
- **Chats:** create/get/list/update/delete/disband where permitted; list/add/remove members, bot join public chat, owner/admin/moderation and announcement operations [S17, S18, S37].
- **Messages:** create, reply (including thread), get, list, edit, recall, forward; batch send/progress/recall; read-user lookup; urgent app/SMS/phone; each endpoint has its own token, scope and role matrix [S8–S13, S19, S38]. Normal creates return a message resource synchronously; batch sends return a task/batch ID and require progress polling.
- **Reactions/pins:** create/list/delete reactions; create/list/delete pins [S15, S16].
- **Media:** multipart image/file upload returns reusable key within documented context; image/file/message-resource downloads return binary streams [S23–S25].
- **Cards:** send as `msg_type=interactive`; update sent card/message, callback-token delayed update, and CardKit JSON 2.0 partial/batch updates. Preserve update sequence/version and callback token expiry [S20–S22].
- **Contacts:** get/list/search/create/update/delete users/departments/groups where scopes, app type and admin data range permit [S26, S27].
- **Calendar/Docs:** calendar CRUD/attendees/ACL/subscription and Docx block/permission/event operations are separate resource adapters [S30–S34].
- **Result/error semantics:** normal API calls return JSON `{code,msg,data}`-style results; binary downloads and webhook callback acknowledgements differ. Treat 429 as retryable only with endpoint-aware backoff. Preserve raw platform code/request ID for diagnostics. Message `uuid` is an operation-local idempotency key, not a global exactly-once guarantee [S8, S9].

## 9. Normalized common-capability candidates

These can safely join a cross-platform contract, with capability discovery and explicit restrictions:

1. `BotInstallation { platform, region, app_id, tenant_id, bot_open_id?, scopes, availability }` — never infer portability between Feishu and Lark.
2. `ConversationRef` with kind `direct|group`, plus optional `thread_id`; keep Feishu `chat_mode`, `chat_type`, `group_message_type` in extension fields.
3. `MessageRef`, `MessageCreate`, `Message`, `ReplyRef { parent_id, root_id, thread_id }`; common operations: send/get/list/reply/edit/delete(recall semantic flag)/forward.
4. Portable content variants: text, rich text, mention, image, audio, video, file, sticker reference. Preserve unknown inbound `msg_type` raw JSON.
5. `Reaction { id, emoji_key, actor, created_at }` and add/list/remove capability.
6. `Pin { message_id, chat_id, operator, created_at }` and create/list/remove.
7. Member list/add/remove and coarse role labels, but not a universal RBAC API.
8. `InteractiveActionEvent` for button/select/form submit, while the rendered card schema remains namespaced.
9. Upload/download media as typed requests/results with streaming/blob handles owned by the transport process, never actor-held sockets.
10. `Page<T> { items, next_cursor, has_more }`, `RateLimitError { retry_after?, platform_code }`, and operation-local `idempotency_key`.
11. Typed ingress envelopes carrying `event_id`, `event_type`, tenant/app identity, receive time, typed payload, and optional raw payload.

## 10. Required namespaced extensions

- `lark.card.*`: Card JSON 1/2, CardKit component IDs/sequences, callback tokens, toast/update response and form values are not portable renderer semantics [S20–S22].
- `lark.chat.group_message_type`, `lark.chat.topic_mode`, `lark.chat.moderation`, `lark.chat.announcement`: distinct group/thread/product configuration [S18, S37].
- `lark.bot.menu` and P2P-chat-entered event: unlike universal slash commands [S35, S36].
- `lark.message.urgent_app|urgent_sms|urgent_phone` and enterprise quota metadata [S38].
- `lark.contact.department`, `open_department_id`, Contact data scope and org-visibility rules [S26, S27].
- `lark.calendar.*` and `lark.docx/drive.*`: separate ACL/resource/event semantics [S30–S34].
- `lark.custom_webhook_bot.*`: webhook secret, keyword/IP/signature controls and static-card restriction [S7].
- Raw escape hatch: inbound unknown `msg_type`, card action payload, and platform error details; never use it to expose unofficial client automation.

## 11. Limits, policy, review, regional and lifecycle risks

- **Regions:** hard-configure service environment. Feishu host is `open.feishu.cn`; Lark host is `open.larksuite.com`. Register and authorize apps in the matching environment [S1, S2]. Feature parity remains unproven.
- **Review:** custom app versions/permission and availability changes may require tenant-admin approval; store apps additionally face marketplace review. Approval status belongs in installation capability state [S3, S4].
- **Layered authorization:** endpoint app type + bot capability + token identity + API/field scope + app availability/data range + chat/resource ACL/role can all matter. A granted scope is not sufficient [S4, S6, S17, S26, S33].
- **Rate:** do not install a global constant. Message sends have endpoint and per-target throttles; custom webhook has a smaller independent limit; Calendar/Docs endpoints differ [S7–S9, S31, S34]. Backoff on 429 and queue by tenant/operation/target.
- **Payload/lifetime:** custom webhook 20 KB; app message text 150 KB and post/card 30 KB; Card JSON 2.0 interaction/update lifetime 14 days [S7, S8, S21].
- **Idempotency:** one-hour message UUID windows and different chat-create windows require adapter-specific retention. Events are at-least-once in practical design and must be deduplicated [S8, S9, S29, S37].
- **Lifecycle/versioning:** prefer V2 event envelope and current `server-docs`; isolate legacy v1 events and older card versions. Do not dual-subscribe equivalent event versions [S29].
- **Commercial/tenant controls:** phone/SMS urgent messages consume tenant quota; avoid encoding pricing or quotas as stable contract [S38].

## 12. Conflicts and unknowns

1. **Feishu/Lark parity:** parallel paths and Lark catalog exist, but no authoritative feature-by-feature parity table was found. All Feishu-only evidence is not automatically a Lark guarantee.
2. **Topic terminology:** docs distinguish `chat_mode=topic` product groups from API-created group chats using `group_message_type=thread`. Adapters must not collapse them [S37].
3. **Message update events:** the event catalog includes `im.chat.updated_v1`, not a proven general `im.message.updated_v1`. Polling/get is required if edited-message synchronization is essential [S11, S36].
4. **Multiple bot identities per app:** not established. Model multiple installations/apps, not undocumented persona switching.
5. **Media groups/albums, general audit export, presence/typing, call control, E2EE/key management and federation:** insufficient or absent official evidence; statuses remain `UNKNOWN` or `UNSUPPORTED` as tabled.
6. **Exact Lark limits/review differences:** likely endpoint-specific; validate inside the international developer console before production.
7. **Product-only UI breadth:** this is an Open Platform audit, not a full end-user SKU comparison. A feature visible in client UI is not claimed automatable unless sourced.

## 13. OBCX design implications

1. **Split capabilities, not a giant `IBot`.** Suggested discoverable capability families: `Messaging`, `History`, `Threads`, `MessageMutation`, `Reactions`, `Pins`, `Media`, `Cards`, `ChatMembership`, `Directory`, `ReadReceipts`, `Urgency`, `CalendarExtension`, `DocsExtension`. This directly reflects distinct scope/role/identity gates [S4, S8–S34].
2. **Installation is tenant-and-region keyed.** `PlatformEnvironment = FeishuCN | LarkIntl`; token cache key includes environment, app, tenant and actor identity. Never route solely by API path [S1, S2, S5].
3. **Business actors receive serializable messages only.** The process-owned adapter handles HTTP/WebSocket, token refresh, callback verification, binary streaming, throttling and retries; actors see typed `InboundEvent`, `ApiRequest`, `ApiResult`, and blob references [S5, S25, S28].
4. **Represent product/API mismatch.** `MessageTypeCapabilities` advertises sendable vs readable types separately. Native `vote` and `video_chat` may be decoded inbound but must not become send operations [S8, S14].
5. **Model recall, not generic delete.** Result should report `RecallResult` and policy failures; edit capability advertises allowed types/ownership/window/count [S11, S12].
6. **Thread DTOs preserve three relations.** Keep `message_id`, `parent_id`, `root_id`, `thread_id`, `chat_id`; history accepts a typed `ChatContainer` or `ThreadContainer` [S9, S10].
7. **Cards use normalized actions plus namespaced rendering.** Common business events can expose action ID/form values, but outbound schema is `lark.card.v2`. Callback ingress must ack within three seconds and enqueue slow work; update token/sequence stays adapter-owned [S20–S22].
8. **Ingress is deduplicated durably.** Key by environment/app/tenant/`event_id`; retain message IDs as a secondary guard. Acknowledge callbacks independent of business completion [S20, S29].
9. **Rate limiting is hierarchical.** Scheduler keys include app/tenant/endpoint and message target. Accept caller idempotency key, map to Feishu `uuid`, and retain it for the documented window [S7–S9].
10. **Directory is not social graph.** DTOs expose users, departments and memberships with visibility provenance; omit inaccessible fields rather than manufacturing empty values [S26, S27].
11. **Calendar/Docs require separate authorization state.** Store user OAuth grants and resource ACL/subscription metadata apart from bot tenant token; user-only Calendar watches cannot be implemented as a tenant-wide headless bot [S31, S33, S34].
12. **Keep custom webhook bot as a minimal egress adapter.** It supports notification send only, not the app-bot capability set; card actions must be reported unavailable [S7].

## 14. Claim-to-source checklist

| Conclusion | Sources |
|---|---|
| Separate Feishu/Lark environments and no assumed portability | S1, S2 |
| App/user identity, tenant, review and layered permission model | S3–S6, S26, S33 |
| Custom webhook bot is one-way and static-card only | S7 |
| Message CRUD/reply/thread/forward limitations | S8–S14, S37 |
| Reaction/pin/member/moderation/read support | S15–S19 |
| Card forms/actions and 3-second contract | S20–S22 |
| Media key and download model | S23–S25 |
| Contact hierarchy, not social graph | S26, S27, S36 |
| HTTPS/WebSocket events and deduplication | S28, S29, S36 |
| Calendar/Docs are separately authorized extensions | S30–S34 |
| Unsupported presence/typing/call/social operations | S14, S35 |
| Urgent/batch notifications are namespaced and constrained | S38 |
| OBCX capability-discovery, typed DTO and process-owned transport recommendations | S1, S4–S10, S20, S25–S29, S31–S34 |
