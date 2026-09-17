# OBCX Actor Registry

This directory is the actor-only publication registry for OBCX ABI 2
packages. A submission is a canonical `entries/<actor-id>/actor.toml`; no
second metadata dialect is accepted. Process-local BotInstallation components
and capabilities are compiled into OBCX and are not registry packages.

Validate entries and confirm that the checked-in index is current:

```bash
python3 actor-registry/generate_actor_index.py validate
python3 actor-registry/generate_actor_index.py generate --check
```

Regenerate `index/actors.json` after adding or updating an entry:

```bash
python3 actor-registry/generate_actor_index.py generate
```

Resolve a release artifact deterministically:

```bash
python3 actor-registry/generate_actor_index.py resolve \
  --id vollate.bridge --version 0.1.0 --platform linux-x86_64
```

`artifact.platforms` is the authoritative list of binary assets actually
built and verified for a package version. The generator emits only those
platforms and names each release asset with its OS/architecture triple; it
never fabricates downloads for unverified operating systems.

The entry schema is `schemas/actor-registry-entry.schema.json`; the generated
index schema is `schemas/actor-index.schema.json`. Generation validates every
submission with the same canonical metadata validator used by OBCX CMake and
sorts entries by actor id and version, so identical inputs produce identical
bytes.
