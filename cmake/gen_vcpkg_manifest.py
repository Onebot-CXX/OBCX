#!/usr/bin/env python3
"""Export vcpkg requirements from a prepared, frozen v2 package graph.

No actor checkout, CMake configure, registry lookup or network access occurs here.
SDK/core baseline dependencies remain in the explicitly supplied base manifest.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from obcx_package import PackageError
from obcx_package.build_inputs import verified_graph
from obcx_package.io import atomic_write, encoded
from obcx_package.providers import vcpkg_manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--graph", required=True, type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--mode", required=True, choices=("development", "release"))
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.output.resolve() in {p.resolve() for p in (args.workspace, args.lock, args.graph, args.base)}:
            raise PackageError("vcpkg output must not overwrite workspace, lock, graph or core base")
        graph = verified_graph(args.workspace, args.lock, args.graph, args.cache, args.mode)
        base = json.loads(args.base.read_bytes())
        if not isinstance(base, dict):
            raise PackageError("vcpkg core base must be an object")
        output = vcpkg_manifest(graph, base, args.baseline)
        atomic_write(args.output, encoded(output))
        print(f"Generated {args.output} from frozen package graph {graph['lock_sha256']}")
        return 0
    except PackageError as error:
        print(f"vcpkg export: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("vcpkg export: invalid/unreadable input", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
