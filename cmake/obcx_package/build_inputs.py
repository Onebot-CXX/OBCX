"""Keep generated package state separate from its authoritative inputs."""
from __future__ import annotations

from pathlib import Path

from . import PackageError


def validate_outputs(workspace: Path, graph: dict, outputs: list[Path]) -> None:
    resolved = [path.resolve() for path in outputs]
    if len(set(resolved)) != len(resolved) or workspace.resolve() in resolved:
        raise PackageError("output/workspace paths must be distinct")
    if any(path.is_relative_to(Path(node["source_dir"]).resolve())
           for path in resolved for node in graph["packages"]):
        raise PackageError("generated state must be outside package source roots")
    inputs = {(workspace.resolve().parent / provider["provenance"]["path"]).resolve()
              for provider in graph["providers"]}
    if inputs.intersection(resolved):
        raise PackageError("generated state must not overwrite provider evidence")
