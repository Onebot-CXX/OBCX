"""Local package catalog checks, using the canonical metadata reader.

A source catalog is not evidence of a built release. Download URLs and artifact
availability deliberately require the deferred release/inventory implementation.
"""
from pathlib import Path

from . import PackageError, SCHEMA_VERSION, TOOL_VERSION
from .io import atomic_write, digest, encoded, metadata


def build_index(entries: Path) -> dict:
    entries = entries.resolve(strict=True)
    if any(entries.rglob("actor.toml")):
        raise PackageError("registry: legacy actor.toml entries must be replaced with package.toml")
    paths = sorted(entries.glob("*/package.toml"))
    if not paths:
        raise PackageError("registry: no package.toml entries")
    packages = []
    ids = set()
    for path in paths:
        if path.is_symlink() or path.parent.is_symlink():
            raise PackageError("registry: entry paths must not be symlinks")
        document = metadata(path, "package")
        identity = document["package"]
        if path.parent.name != identity["id"]:
            raise PackageError(f"registry: entry directory must equal package id {identity['id']}")
        if identity["id"] in ids:
            raise PackageError(f"registry: duplicate package id {identity['id']}")
        ids.add(identity["id"])
        packages.append({"metadata": document, "source": {
            "path": path.relative_to(entries).as_posix(), "sha256": digest(path.read_bytes())}})
    return {"schema_version": SCHEMA_VERSION, "tool_version": TOOL_VERSION,
            "status": "development-metadata-only", "packages": packages}


def write_index(entries: Path, output: Path, check: bool) -> dict:
    if output.resolve().is_relative_to(entries.resolve()):
        raise PackageError("registry: index output must be outside entries")
    index = build_index(entries)
    content = encoded(index)
    if check:
        if not output.is_file() or output.read_bytes() != content:
            raise PackageError("registry: generated package index is stale")
    else:
        atomic_write(output, content)
    return index
