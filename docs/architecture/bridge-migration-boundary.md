# Bridge Actor Boundary

Status: actor cutover complete in the checked-out bridge repository
(2026-07-13)

## Repository and entry point

The bridge lives at `local_actor/obcx-message-bridge` and builds one dynamic
entry point: the ABI 2 `bridge` actor. Its package identity, artifact,
dependencies, compatibility range, and publication information are declared
in `actor.toml`.

`BridgeActor::handle` consumes
`obcx::message_store::events::MessageStored`, resolves the runtime
services it needs, and suspends its `ActorTask` through
`ActorContext::await_asio` while existing QQ/Telegram forwarding coroutines
run. The callback boundary republishes the actor continuation to the native
scheduler.

## Ownership boundary

Core owns:

- actor ABI, library loading, scheduling, cancellation, and pipeline dispatch;
- `DbManager`, process-owned bot installations/dispatcher, and installed SDK
  exports;
- installed SDK surface validation and generation-scoped configuration tests
  implemented with root-owned generic fixtures.

The bridge package owns:

- QQ-to-Telegram and Telegram-to-QQ forwarding behavior;
- group/topic mapping, reply/recall mapping, media conversion, and retry state;
- bridge-specific configuration and schema migrations;
- forwarding, mapping, media, retry, suspension, failure, and shutdown tests.

The bridge depends only on installed SDK headers. QQ and Telegram operations
are sent through `BotOperationGateway` with exact installation/surface DTOs.
Provider components, transports, and authenticated URLs remain process-owned.
Generic HTTP work uses the installed `HttpClient` API.

## Pipeline contract

The representative flow is:

```text
obcx::core::events::RawMessageEvent -> message_store ->
obcx::message_store::events::MessageStored -> bridge
```

The bridge emits `bridge::events::MessageForwarded` on success and
`bridge::events::MessageForwardFailed` with
a retryable failure on transient delivery errors. `source_platform` and
`conversation_id` form the recommended partition key so ordering is preserved
per conversation while unrelated conversations can run concurrently.

## Verification

Message Bridge owns its standalone build, installation, behavior, reload, and
integration tests. Run that repository's suite against an installed matching
OBCX SDK. Root CTest does not inspect or build the external bridge checkout;
its SDK and runtime contracts use root-owned generic fixtures.
