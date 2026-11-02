# Domain identity persistence, encoding version 1

> SDK 2.7.0 removes generic historical conversion/import services. Whole
> current typed references remain the persistence contract. Bridge schema 5
> preserves actual topic facts separately where canonical scope is insufficient.
> See domain-sdk-cutover.md for operator-only offline conversion and matching
> products; runtime constructors perform no automatic data migration.

Ordinary actors persist and restore **whole SDK references**. Obtain references
from typed events/results or explicit `ConversationResolver` configuration; do not assemble
native tuples, split opaque IDs, append topics, or register platform codecs.
The following byte examples document the existing encoding, not a new API for
constructing destinations or proof that any example account/chat exists.

## Encoding and canonical form

Implementation authority:

- `include/core/bot/domain_ids.hpp`: structural encoding, decoding and tags.
- `include/core/bot/ids.hpp`, `validation.hpp`: identifiers and field bounds.
- `include/core/bot/references.hpp`: outer installation and complete references.
- `src/onebot11/bot/domain_binding.cpp`: QQ native vocabulary and validation.
- `src/telegram/bot/domain_binding.cpp`: Telegram vocabulary and topic rules.

The exact wire spelling is `obcx1:<tag>:<lowercase hexadecimal payload>`.
`tag` is one of `conversation`, `user`, `message`, `media`, `media_identity`,
`media_group`, selected by the concrete SDK value type. A different version or
tag is not accepted as that type. The payload is the concatenation of fields:

```text
field(surface) || field(native_kind) || field(component_1) || ...
field(text) = uint32_big_endian(UTF8_byte_length(text)) || UTF8_bytes(text)
```

There is no separator escaping, terminating NUL or field-count prefix. Every
field has an explicit byte length; decoding consumes the complete payload.
Thus delimiter-like bytes cannot merge two fields, and `("a|b", "c")` is
structurally distinct from `("a", "b|c")`. This illustrates framing only:
platform-native validation can reject either tuple for its requested kind.

Bounds apply together, not independently selectable configuration defaults:

| Boundary | Existing rule |
| --- | --- |
| Complete encoded ID | At most 4096 bytes, including version/tag prefix and hex expansion |
| Components after surface/kind | 1 through 8; exactly the count allowed by the native kind at process admission |
| Each payload field | Nonempty, at most 1024 UTF-8 bytes; no ASCII controls below `0x20` or `0x7f` |
| Surface and kind | Additionally 1–128 characters from lowercase ASCII letters, digits, `.`, `_`, `-` |
| Installation ID (outer reference) | Nonempty, at most 256 UTF-8 bytes, without those ASCII controls |

Canonical validation rejects uppercase/non-hex payload characters, odd hex
lengths, truncated length words/fields, extra or missing fields, oversized
components/total IDs, malformed UTF-8, overlong UTF-8, surrogate code points and
code points above `U+10FFFF`. It re-encodes the parsed fields and requires exact
byte equality. No Unicode normalization or case folding is applied to identity
components. Native numeric fields separately require canonical decimal spelling:
no leading `+`, whitespace, leading zeroes or `-0`.

## Native addressing rules

| Surface | Conversation kind | Components and current codec limits |
| --- | --- | --- |
| `onebot11.qq` | `group`, `private` | One positive signed-64-bit decimal ID; the kinds are distinct |
| `telegram.bot_api` | `private` | One decimal ID in `1..2^52-1` |
| `telegram.bot_api` | `group`, `channel` | One decimal ID in `-(2^52-1)..-1`; kinds remain distinct |
| `telegram.bot_api` | `topic` | Negative chat ID in that range, then thread ID in `2..INT32_MAX` |

Telegram `supergroup` ingress normalizes to `group`. A verified supergroup
`message_thread_id=1` normalizes to the main group scope, not a separately
encoded `topic(...,1)`. A thread on another native chat kind is rejected. A
native `is_topic_message=true` without its thread identity is not guessed as
the main group. Historical scope-selection facts remain separately available
through the legacy facade; canonicalizing live routing does not rewrite a
historical session key containing thread one.

QQ users are positive signed-64-bit IDs; QQ message components use canonical
signed-64-bit spelling. Telegram users are in `1..2^52-1`, and Telegram message
components are in `1..INT32_MAX`. Native protocol operation types may impose
additional constraints; successful structural restoration does not bypass them.
QQ does not gain topics or cross-surface addressing from the common wire format.

`OpaqueId::restore` and SDK JSON restoration establish structural validity only.
The process-owned codec additionally validates the surface, native vocabulary,
component count and native ranges at ingress/configuration/legacy/dispatch
boundaries. Outer installation surface must match the encoded surface. A valid
encoding alone is **not** route availability, conversation membership, permission,
observed reply evidence or authority to read media.

## Fixed persistence examples

These illustrative identities were calculated from the length-prefixed format
and checked against the source rules above. They are documentation vectors,
not a provider execution or a claim of a new runtime test.

### QQ group and private address with the same numeric text

```json
{
  "installation": {"installation_id": "qq-main", "surface": "onebot11.qq"},
  "id": "obcx1:conversation:0000000b6f6e65626f7431312e71710000000567726f757000000003313233"
}
```

This is the complete conversation reference for the illustrative group `123`.
The private address `123` in that installation instead has:

```text
obcx1:conversation:0000000b6f6e65626f7431312e7171000000077072697661746500000003313233
```

For the group payload, `0000000b` precedes the 11 bytes of `onebot11.qq`,
`00000005` precedes `group`, and `00000003` precedes `123`. The private
reference differs by the kind field, even though the final numeric bytes match.

### Telegram main group and two topics in the same group

The outer installation is
`{"installation_id":"tg-main","surface":"telegram.bot_api"}`.
The illustrative chat is `-100`:

| Scope | Complete `ConversationId` string |
| --- | --- |
| Main group | `obcx1:conversation:0000001074656c656772616d2e626f745f6170690000000567726f7570000000042d313030` |
| Topic 2 | `obcx1:conversation:0000001074656c656772616d2e626f745f61706900000005746f706963000000042d3130300000000132` |
| Topic 3 | `obcx1:conversation:0000001074656c656772616d2e626f745f61706900000005746f706963000000042d3130300000000133` |
| Private 123 | `obcx1:conversation:0000001074656c656772616d2e626f745f617069000000077072697661746500000003313233` |

Topic 2 and topic 3 are different complete destinations. An actor replies using
the whole `event.message.conversation`; it does not extract or pass a thread ID.
Main-group permissions and exact-topic permissions remain separately modeled by
the configuration/command resolver; ID conversion must not widen or narrow ACLs.

### Same native identity in two installations

```json
[
  {
    "installation": {"installation_id": "qq-main", "surface": "onebot11.qq"},
    "id": "obcx1:conversation:0000000b6f6e65626f7431312e71710000000567726f757000000003313233"
  },
  {
    "installation": {"installation_id": "qq-backup", "surface": "onebot11.qq"},
    "id": "obcx1:conversation:0000000b6f6e65626f7431312e71710000000567726f757000000003313233"
  }
]
```

The opaque ID bytes intentionally match; the complete `ConversationRef` values
do not. Installation is an outer identity component, not an implicit singleton
or an omitted part of the persistence key. Persisting only `id` loses scope.

### A message keeps the full conversation plus its own ID

```json
{
  "conversation": {
    "installation": {"installation_id": "tg-main", "surface": "telegram.bot_api"},
    "id": "obcx1:conversation:0000001074656c656772616d2e626f745f61706900000005746f706963000000042d3130300000000132"
  },
  "id": "obcx1:message:0000001074656c656772616d2e626f745f617069000000076d657373616765000000023432"
}
```

This represents illustrative message `42` in topic 2. Equal message-ID bytes in
another conversation or installation do not make equal `MessageRef` values.
Copying a message ID into another conversation does not establish existence or
reply authority. Persist `UserRef`, `MediaRef`, `MediaIdentity` and
`MediaGroupRef` with their respective installation/conversation scope as well.

## Typed persistence, media and recovery

Message Store schema 5 persists only the unified typed message and lookup
columns. Chat/Bridge read that snapshot directly; there is no native-archive
restoration, conversion or backfill. Old schemas are rejected without deleting
or rewriting data. Current configuration supplies explicit kind/native ID/topic
to `ConversationResolver`, never a historical chat:/group: storage key.

Native source lookup keys and application pair/destination keys are not opaque
references or authority. Generic reference conversion is removed. Current
platform display views consume scoped UserRefs; they do not import native IDs
or archived events. Missing facts remain unavailable: no assumed topic, fabricated
timestamp, provider lookup, replay or new admission is authorized by restoration.

`MediaIdentity` is optional stable comparison evidence, not a locator or read
capability. Do not derive it from a temporary URL. QQ live received-media IDs
encode opaque process-private receipt tokens; persistence does not retain the
store entry or revive an expired/evicted/closed/origin-invalid receipt. Structural
restoration after restart does not manufacture URL or local-file access.
Explicit native sensitive views remain separate from ordinary media reads;
there is no historical URL/file-import fallback.

Actor input schema 3, domain DTO wire schema 1, MessageStored wire schema 2 and
Store database schema 5 are independent. The scheduler ABI remains V2. A matching label is not complete C++ ABI proof or
a substitute for using the matching installed SDK and core. Any future opaque
encoding version needs an explicit migration decision rather than silently
reinterpreting version-1 strings or overwriting legacy persistent identities.
