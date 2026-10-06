#!/usr/bin/env python3
"""Data-only entry points used by OBCX CMake integration."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from obcx_package import PackageError
from obcx_package.contracts import validate_provider_binding
from obcx_package.io import atomic_write, encoded
from obcx_package.providers import verify_environment, verify_provider
from obcx_package.versions import satisfies


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("check-version")
    command.add_argument("--version", required=True)
    command.add_argument("--requirement", required=True)
    command.add_argument("--scheme", required=True, choices=("semver", "numeric"))
    for name in ("check-environment", "verify-provider"):
        command = commands.add_parser(name)
        command.add_argument("--binding", required=True, type=Path)
        command.add_argument("--workspace", required=True, type=Path)
        command.add_argument("--build-dir", required=True, type=Path)
        if name == "verify-provider":
            command.add_argument("--observed", required=True, type=Path)
            command.add_argument("--output", required=True, type=Path)
    command = commands.add_parser("plan")
    for name in ("workspace", "cache", "build-dir", "output", "current-graph"):
        command.add_argument("--" + name, required=True, type=Path)
    command.add_argument("--mode", required=True, choices=("development", "release"))
    for name in ("platform", "compiler-id", "compiler-version"):
        command.add_argument("--" + name, required=True)
    for name in ("audit", "audit-generated"):
        command = commands.add_parser(name)
        command.add_argument("--graph", required=True, type=Path)
        command.add_argument("--snapshot", required=True, type=Path)
        if name == "audit-generated":
            command.add_argument("--evaluated", required=True, type=Path)
            command.add_argument("--build-dir", required=True, type=Path)
            command.add_argument("--configuration", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "plan":
            from obcx_package.cmake_plan import plan
            plan(args)
        elif args.command in {"audit", "audit-generated"}:
            from obcx_package.target_audit import audit
            audit(args)
        elif args.command == "check-version":
            if not satisfies(args.version, args.requirement, args.scheme):
                raise PackageError("actual version does not satisfy the explicit requirement")
        else:
            binding = json.loads(args.binding.read_bytes())
            validate_provider_binding(binding, "provider")
            if args.command == "check-environment":
                verify_environment(binding, [], args.workspace, args.build_dir)
            else:
                observation = json.loads(args.observed.read_bytes())
                result = verify_provider(binding, observation, args.workspace, args.build_dir)
                atomic_write(args.output, encoded(result))
        return 0
    except PackageError as error:
        print(f"package-cmake: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("package-cmake: invalid/unreadable build input", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
