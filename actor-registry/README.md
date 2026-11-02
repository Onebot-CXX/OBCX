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

The six maintained actor manifests match the independent registry and their
current source packages: SDK/core `>=2.7.0,<3.0.0`, input schema 3, ABI V2.
See [coordinated cutover requirements](../packaging/actors/domain-sdk-cutover.md)
for package versions, matching backup/rollback scope and remaining acceptance.

This is an **unpublished development snapshot**, not a newly pinned release.
The index has status `development-metadata-only` and advertises no downloads.
Old release binaries cannot become verified v2 assets merely by renaming their
metadata. Release tooling pins, inventories and coordinated publication remain
deferred. The old actor-only generator/index/schema are removed, with no fallback.
