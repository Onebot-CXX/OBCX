#!/usr/bin/env python3
"""Export vcpkg requirements directly from explicit v2 workspace declarations.

No actor checkout, CMake configure, registry lookup or network access occurs here.
SDK/core baseline dependencies remain in the explicitly supplied base manifest.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from obcx_package import PackageError
from obcx_package.build_inputs import validate_outputs
from obcx_package.io import atomic_write, digest, encoded
from obcx_package.providers import vcpkg_manifest
from obcx_package.resolver import Resolver


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--mode", required=True, choices=("development", "release"))
    parser.add_argument("--base", required=True, type=Path)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.output.resolve() == args.base.resolve():
            raise PackageError("vcpkg output must not overwrite core base")
        graph = Resolver(args.workspace, args.cache, args.mode, "deny").resolve()
        validate_outputs(args.workspace, graph, [args.output])
        base = json.loads(args.base.read_bytes())
        if not isinstance(base, dict):
            raise PackageError("vcpkg core base must be an object")
        output = vcpkg_manifest(graph, base, args.baseline)
        atomic_write(args.output, encoded(output))
        print(f"Generated {args.output} from current package graph {digest(encoded(graph))}")
        return 0
    except PackageError as error:
        print(f"vcpkg export: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("vcpkg export: invalid/unreadable input", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
