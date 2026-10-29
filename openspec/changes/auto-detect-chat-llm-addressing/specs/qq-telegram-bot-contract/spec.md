## MODIFIED Requirements

### Requirement: The first operation set is closed to current QQ and Telegram calls
The contract SHALL define only the base operations `message.send_group`, `message.send_private`, `message.delete`, `telegram.message.send_topic`, `telegram.message.edit_text`, `telegram.media.send_photo`, `telegram.media.send_group_urls`, `telegram.media.send_group_uploads`, `telegram.media.fetch_file`, `onebot11.group_member.get`, `onebot11.forward_message.get`, `onebot11.group_file.resolve`, `onebot11.private_file.resolve`, and `onebot11.group.poke`, together with provider-specific operations explicitly added by subsequent capability specifications. This change SHALL explicitly admit the common `bot.self.get` cached identity operation and provider-specific `onebot11.message.get` for exact native reply resolution. Existing `onebot11.login_info.get` retains its request/result shape but SHALL also read the same cache without provider I/O; no platform-specific self lookup is required of actors. It MUST NOT define portable history, contact, moderation, reaction, poll, membership-list, request-handling, credential, arbitrary API dispatch or unsupported-platform operations in this change.

#### Scenario: Current actor operation is represented
- **WHEN** an actor or generation-owned command service needs one of the listed QQ or Telegram calls
- **THEN** it can construct the corresponding typed request without a live bot object

#### Scenario: Private text reply is requested
- **WHEN** an authorized command service needs to reply to an exact Telegram or OneBot private user
- **THEN** it can construct `message.send_private` with an exact `PrivateTarget` and serializable message segments

#### Scenario: Unused legacy method is requested
- **WHEN** a caller attempts to express an IBot action outside the closed list and explicitly specified extensions
- **THEN** the contract provides no matching request type and the dispatcher cannot advertise it

#### Scenario: Another platform is considered
- **WHEN** code attempts to construct an operation for Discord, official QQ, WeChat, WeCom, Lark, DingTalk, Matrix, X, or another unsupported surface
- **THEN** validation rejects the surface and no compatibility alias maps it to Telegram or OneBot

#### Scenario: Bot self identity is requested
- **WHEN** a caller submits the common GetSelfRequest for a QQ or Telegram installation
- **THEN** the gateway returns the same SelfInfo result contract from the cache, or explicit unavailable, without provider I/O or credential exposure

#### Scenario: Exact QQ reply author is requested
- **WHEN** a caller submits an exact scoped OneBot message reference
- **THEN** the gateway performs get_msg and only reports a usable result after verifying message ID and conversation identity

## ADDED Requirements

### Requirement: Self and reply lookup operations are finite typed read-only contracts
`bot.self.get` SHALL expose validated installation, native user ID, display name and optional username through common GetSelfRequest/SelfInfo types for both platforms. Its implementation SHALL read the installation cache only. Background refresh SHALL validate native responses, including Telegram is_bot, before publishing identity. `onebot11.message.get` SHALL take an exact scoped group BotMessageRef and return validated message identity, group and sender needed for reply recognition, not unrestricted history. Both operations SHALL be registered through the normal typed request/result codecs, operation traits, capability discovery and deadline/cancellation path. Errors MUST retain existing typed error and submission-safety semantics.

#### Scenario: Self response lacks identity
- **WHEN** background getMe returns an invalid or missing ID or does not describe a bot
- **THEN** the refresh is rejected and does not populate a successful self cache entry

#### Scenario: QQ response belongs to a different group
- **WHEN** get_msg returns a message from a group different from the requested reference
- **THEN** the operation rejects the scoped result and the actor cannot use it as self-reply evidence

#### Scenario: Actor uses an installed SDK
- **WHEN** these request/result values round-trip across the installed SDK boundary
- **THEN** installation, action and native identity are preserved and surface/action mismatches are rejected before provider I/O
