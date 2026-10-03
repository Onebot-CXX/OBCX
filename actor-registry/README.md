# Package registry conformance snapshot

The publication source is the independent `obcx-actor-registry` repository.
This directory mirrors its v2 metadata, thin command wrapper and deterministic
`index/packages.json` for core conformance checks. No independent metadata
validator or schema is maintained here.

From the core repository:

```sh
python3 cmake/package_tool.py registry-validate --entries actor-registry/entries
python3 cmake/package_tool.py registry-index --entries actor-registry/entries \
  --output actor-registry/index/packages.json --check
```

This is an **unpublished development snapshot**, not a newly pinned release.
The index has status `development-metadata-only` and advertises no downloads.
Old release binaries cannot become verified v2 assets merely by renaming their
metadata. Release tooling pins, inventories and coordinated publication remain
deferred. The old actor-only generator/index/schema are removed, with no fallback.
