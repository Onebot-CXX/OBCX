# Pinned actor sources

Clean CI and container builds restore their actor repositories from the Git
bundles in `bundles/`; they do not require GitHub credentials or an ignored
`local_actor/` directory. `restore-sources.sh` checks out each fixed archived
revision. Where an actor is archived as an upstream base plus an adaptation
patch, `apply-patches.sh` replays that patch with fixed commit metadata. These
pins describe the checked-in offline bundle outputs, not the HEADs of a
developer's independent repositories under `local_actor/`. The scripts verify
that the resulting revisions are:

- bridge: `7b1fb9e5f5d9d094dd68c7185a78948b79c2b91c`
- message store: `06f14ed226369079c99474c62088293628ce94e3`
- actor registry: `986c40978111360e31e314cb9e25d5b641fab8a4`
- actor template: `30a1373f53b637eb8bf00250c8f9c3ca26578512`

The container build restores only `bridge` and `message-store`; CI restores all
four before formatting and conformance. To audit the archived sources without
network access:

```sh
OBCX_ACTOR_SOURCE_ROOT=/tmp/obcx-actors \
  sh packaging/actors/restore-sources.sh
```
