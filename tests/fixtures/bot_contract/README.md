# Bot contract migration baseline

`production-baseline.json` freezes the SDK JSON semantics at root revision
`fb80ac6`, before `decouple-bot-platform-contracts` changes the SDK layout.
It contains only synthetic identities, bytes and `example.test` URLs.

- Production surfaces: `onebot11.qq`, `telegram.bot_api`.
- Recipes: `onebot11.qq.websocket`, `onebot11.qq.http`,
  `telegram.bot_api.http`.
- OneBot recipes advertise 7 actions; Telegram advertises 8. Their union is
  exactly the 13 keys in `operations`, sharing group-send and delete.
- Without a prepared same-installation uploader, Telegram advertises the 7
  IDs in `telegram_without_uploader`, excluding multipart group upload.
- Each action records its normalized SDK request and successful result wrapper.
  All 10 common error codes are recorded, including retry metadata and uncertain
  submission. These are SDK DTOs, not provider envelopes.

`BotOperationGoldenTest` replays frozen input through the actual codecs and
compares normalized serialized output; it also checks recipe manifests without
constructing transports. Existing `BotOperationComponentTest` coverage checks
live capability publication with/without the uploader and mock provider I/O.
Do not regenerate the JSON to make an incompatible codec change pass. Migrate
only the test's C++ names/includes when moving DTO ownership.
