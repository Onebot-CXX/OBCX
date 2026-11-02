# Authoritative receive-origin handoff

Process-only seam; native transport consumption and execution remain owner gates. Native owners must run their owning-codec validation before direct cache observation/publication; cache itself validates the common values and exact installation, not provider JSON. Registry observation additionally invokes the registered native codec.

## Frozen API

`core/bot/self_identity_cache.hpp`, namespace `obcx::core`:

- `SelfIdentityOrigin`: opaque, copyable process token. Only SelfIdentityCache constructs it; it contains a weak cache-instance identity and authoritative epoch. No numeric getter, public-ID/native-JSON constructor, serializer, actor DTO or separate identity cache.
- `SelfObservationStatus`: `Accepted`, `Transitioned`, `RejectedOrigin`, `Closed`, `InvalidIdentity`.
- `SelfObservationResult { SelfObservationStatus status; std::optional<SelfIdentityOrigin> current_origin; bool accepted() const noexcept; }`. Only Accepted/Transitioned are accepted. An accepted cache operation returns the current authoritative origin. Transitioned includes conflicts against retained expected identity EVEN WITHOUT a readable snapshot. Rejected operations do not mutate expected identity, snapshot or evidence.
- `SelfIdentityCache::capture_origin() const -> std::optional<SelfIdentityOrigin>`. Returns no origin before initial connection establishment, while disconnected, or after close. Disconnected observation/publication returns RejectedOrigin (closed returns Closed); no dependent capture callback runs.
- `SelfIdentityCache::observe_user(const SelfIdentityOrigin &, bot::UserRef) -> SelfObservationResult`.
- `SelfIdentityCache::observe_user_and_capture(const SelfIdentityOrigin &, std::optional<bot::UserRef>, const std::function<void()> &) -> SelfObservationResult`.
- `SelfIdentityCache::publish_self(const SelfIdentityOrigin &, const bot::SelfInfo &) -> SelfObservationResult` atomically observes identity and publishes the complete validated profile. Use this for successful getMe/login responses, not observe-then-rebase-then-publish.
- `SelfIdentityCache::Loader = std::function<boost::asio::awaitable<Result>(SelfIdentityOrigin)>`. Core captures the origin before scheduling the loader and passes it unchanged. Loader returns the existing typed Result. The loader lambda/coroutine must own its token BY VALUE across awaits; do not accept a dangling reference to std::function's temporary argument. The same-token core fallback publication is harmless if the native loader has already published a legitimate transition; it cannot overwrite the new epoch.

`core/bot/operation_registry.hpp`:

- `capture_origin() const -> std::optional<SelfIdentityOrigin>` delegates under registry admission lock.
- `observe_self_user(const SelfIdentityOrigin &, bot::UserRef) -> SelfObservationResult` performs native identity validation then delegates with the supplied origin.
- `MessageAdmissionGeneration { SelfObservationResult observation; std::optional<std::uint64_t> evidence_generation; }`.
- `capture_message_admission_generation(const SelfIdentityOrigin &, std::optional<bot::UserRef>) -> MessageAdmissionGeneration` checks origin, observes optional self and captures evidence generation atomically in registry→cache→evidence lock order.

`core/bot/bot_event_components.hpp`:

- `BotEventContext::source_origin` is `std::optional<SelfIdentityOrigin>`, carried unchanged through publication and queued process message callbacks.
- `BotEventCapability::set_origin_capture_handler(std::function<std::optional<SelfIdentityOrigin>()>)` is configured once before activation by main.
- `BotEventCapability::capture_origin() const -> std::optional<SelfIdentityOrigin>` invokes that configured authority OUTSIDE the capability mutex. Capture before originating I/O or callback scheduling, never on an old callback's resumption.
- `BotEventCapability::publish(const common::Event &, const SelfIdentityOrigin &) const -> SelfObservationResult`. For message events it returns the authoritative admission observation, including Transitioned. Missing handler/origin fails closed. Inactive publication returns Closed. Exceptions in admission return InvalidIdentity.
- The existing `void publish(const common::Event &) const` remains only for notices/heartbeats; it drops messages rather than sampling current authority. Non-message publication retains its compatibility path; its token overload's Accepted/null-origin result is NOT an authoritative identity or message admission check.
- `MessageAdmissionResult { SelfObservationResult observation; std::optional<MessageIngressAdmission> admission; }` is the admission-handler return type. Main wraps the registry observation/generation with the existing receive timestamp.

The obsolete unfenced dispatcher/registry observation signature is removed; there is no implicit current-origin overload. Parent narrowly approved removal of that dispatcher declaration/definition only. Invocation, completion and send-notification methods are unchanged.

Publication status records the identity/admission decision, not durable handler delivery. If delivery closes or scheduling throws after a transition commits, the authoritative Transitioned result is still returned so native retirement is not skipped. Refresh-post failure likewise cannot hide an already committed transition.

## Initial connection ordering and disconnect eligibility

Connection eligibility is now synchronous under the SAME cache mutex as epoch invalidation. `connection_changed(false)` immediately disables capture, observation/admission, refresh admission and profile publication; it does not wait for the strand callback. Reads remain unavailable while disconnected. The loader checks eligibility and its original token both before starting and after awaiting I/O; publication independently checks again under lock. A queued refresh cannot obtain a usable post-disconnect token during a timer-callback gap.

The existing `connected_` flag is exclusively mutex-owned; no strand callback writes it. Timer cancellation, expiry, in-flight scheduling and coalescing remain strand-owned. Queued lifecycle callbacks cannot restore eligibility using an old captured connection flag. Reconnection advances the epoch again, so it never revives earlier origins.

Initial establishment must proceed in this order:

1. Configure the cache loader and invalidation hook, and wire the event authority/admission handlers before activation, as before.
2. Establish the native transport using its OWN lifecycle/connection-epoch checks. `connection_changed(true)` does not require a process origin: this is the lifecycle establishment path, not a message/profile publication path. Do not wait for a cache origin before establishing that transport.
3. Once that native connection is current and ready for its I/O, notify `connection_changed(true)` under the native lifecycle synchronization. This synchronously enables the new cache epoch; timer/refresh work follows on the strand. Do not issue the notification early while the transport is still unusable.
4. Only THEN capture origins for NEW domain message/profile I/O or callback scheduling. Cache-driven loaders receive their already captured origin by value. Pre-establishment handshake data may establish native lifecycle state but cannot be relabeled with a newly captured origin to publish a message or self profile: perform fresh origin-bearing I/O instead. Do not replay queued disconnected/old-connection data after reconnect.
5. On disconnect, validate the native lifecycle callback's own connection epoch and call `connection_changed(false)` promptly. Retire old native work and prevent offset/state mutation as below. Core rejects old publication immediately, even if the timer strand is busy.

API signatures and status variants are unchanged by this correction. Native connection/cursor retirement remains an owner gate; compilation does not validate its ordering.

## Native owner sequence

1. Under your transport's originating-connection synchronization, capture BOTH connection epoch and authoritative origin before scheduling a websocket callback/read or issuing an HTTP poll/getMe/login. If authority is unavailable, do not start a domain-message origin. Carry the opaque token unchanged with the originating client/connection.
2. On completion, first check your original connection/client epoch. Do not replace the process token by calling capture_origin here. Convert/validate native data, then call `events->publish(event, origin)` or `cache->publish_self(origin, info)` with the original token. Core rejects foreign-instance, expired, stale and closed origins under the cache lock BEFORE observing self or admitting evidence.
3. Use the returned status, never `read().ok()`/snapshot comparisons, to determine authoritative account transition. On Transitioned, retire outstanding old I/O/poll origins and update your connection/account publication state under YOUR transport lock. `current_origin` is for NEW I/O only: never rebase old callback data or republish old batches using it. The event which legitimately first observed a new account can be admitted atomically in that new generation; all other old-origin work becomes stale.
4. A getMe/login loader uses its supplied origin. After native validation, call publish_self under your existing transport publication synchronization; inspect the result before mutating transport account state. Return a typed failure for rejected origins. A successful Transitioned publication already installs the new profile; the core loader's old-token completion must not be rebased or used to undo it.
5. Retire stale responses BEFORE polling offsets, deduplication state, native account fields or other transport state are changed. This remains native-owner responsibility, not a core source-ID comparison. If an atomic origin check plus simple state commit is needed, `observe_user_and_capture(origin, std::nullopt, callback)` runs the callback only for a current origin under the cache lock. Any transport synchronization must ALREADY be held; the callback MUST NOT acquire transport locks, re-enter cache/registry, await, throw or perform I/O. It may only perform the already synchronized short commit. Do not invoke publish/publish_self recursively from such a callback.
6. Native connection lifecycle notifications remain responsible for connection-epoch validity before calling connection_changed. A stale transport callback must not manufacture an authoritative lifecycle transition.

QQ: capture for each HTTP read and before websocket callback scheduling, not merely connection construction. A long-lived websocket callback must freeze the token in its closure; retire/rearm the closure/read origins after transition. Never close over a mutable latest-token slot and read it when old work resumes. Carry account origin alongside the existing connection/poll epoch through conversion/publication. A valid event self observation can return Transitioned even when no self snapshot is readable; retire remaining old-token work.

Telegram: the supplied getMe loader token and each poll token are independent originating operations. Use publish_self's authoritative status instead of `previous.ok() && previous.user != info.user`. Conflicts against expected B while the snapshot is unavailable still return Transitioned for fresh C, whereas a stale B/C response is RejectedOrigin and cannot alter expected identity. Old getUpdates responses must not mutate offsets before the native owner has checked the origin and retired them.

## Lifecycle and lock constraints

The existing cache remains the sole authority. Its synchronous generation hook invalidates existing reply evidence on every account/connection/close generation advance, before a dependent admission can capture evidence. No cache-locked callback acquires transport locks or re-enters cache. Native publication may hold its transport lock BEFORE entering registry→cache→evidence, provided no reverse lock path exists.

Refreshes remain strand-coalesced with one in-flight loader; a stale in-flight refresh queues at most the pending current-generation refresh. Same-generation failure retains the last good snapshot. Disconnected/closed/stale publication cannot revive identity. Successful current-origin account transitions publish the new snapshot rather than permanently failing against the old token. No configuration default, public wire value or new identity cache is introduced.

This lane does not implement native connection epochs, retirement, offsets, platform codecs, public DTOs, resolver changes, or provider execution. Their owners must consume/review this API before QQ/TG origin findings can close.
