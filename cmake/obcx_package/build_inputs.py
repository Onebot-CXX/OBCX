"""Bind prepared graphs to current metadata without changing source locks."""
from __future__ import annotations

from pathlib import Path

from . import PackageError
from .io import read_json
from .resolver import Resolver, frozen


def verified_graph(workspace: Path, lock: Path, graph: Path, cache: Path, mode: str) -> dict:
    prepared = read_json(graph, "resolved-packages")
    actual = Resolver(workspace, cache, mode, "deny").resolve()
    frozen(lock, actual)
    if prepared["lock"] != actual["lock"] or prepared["lock_sha256"] != actual["lock_sha256"]:
        raise PackageError("prepared graph does not match frozen lock; run explicit resolve")
    # Development implementation edits are allowed. Their current content is
    # returned for build evidence; they never silently rewrite the source lock.
    def metadata_only(value):
        return [{key: item for key, item in node.items() if key != "source_receipt"}
                for node in value["packages"]]
    if metadata_only(prepared) != metadata_only(actual) or prepared["unused_sources"] != actual["unused_sources"]:
        raise PackageError("prepared graph source/metadata mapping drift; run explicit resolve")
    return actual
