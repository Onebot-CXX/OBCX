#!/usr/bin/env python3
"""Enter a Nix shell with shell.nix dependencies from the selected package graph."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "cmake"))

from obcx_package import PackageError
from obcx_package.resolver import Resolver


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--mode", required=True, choices=("development", "release"))
    parser.add_argument("--network", required=True, choices=("allow", "deny"))
    parser.add_argument("--nix-system", required=True)
    parser.add_argument("--shell", required=True, choices=("workspace", "workspace-ci"))
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--print-inputs", action="store_true",
                        help="emit workspace shell inputs as JSON without invoking Nix")
    output.add_argument("--print-env", action="store_true",
                        help="emit Bash environment for the automatic flake shell hook")
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="optional command after --, instead of an interactive shell")
    args = parser.parse_args()
    if args.print_env and args.command:
        parser.error("--print-env cannot be combined with a command")
    if args.print_inputs and args.command:
        parser.error("--print-inputs cannot be combined with a command")
    try:
        # Reuse the same graph/validation as CMake, including test-profile edges,
        # pinned remote sources and version constraints. Never scan local_actor.
        graph = Resolver(args.workspace, args.cache, args.mode, args.network).resolve()
        nodes = {node["id"]: node for node in graph["packages"]}
        shells = []
        for package_id in graph["order"]:
            entry = Path(nodes[package_id]["source_dir"]) / "shell.nix"
            if not entry.exists():
                if entry.is_symlink():
                    raise PackageError(f"{package_id}: shell.nix is a broken symlink")
                continue
            if not entry.is_file():
                raise PackageError(f"{package_id}: shell.nix must be a file")
            shells.append({"id": package_id, "path": str(entry)})
            print(f"nix-develop: {package_id}: {entry}", file=sys.stderr)
        # Keep the existing core environment when no selected package adds Nix
        # dependencies. In particular, empty roots do not evaluate another flake.
        inputs = json.dumps({"system": args.nix_system, "shells": shells})
        if (args.print_env or args.print_inputs) and not shells:
            if args.print_inputs:
                print(inputs)
            return 0
        expected = {
            "linux-x86_64": "x86_64-linux",
            "linux-arm64": "aarch64-linux",
            "darwin-arm64": "aarch64-darwin",
        }.get(graph["platform"])
        if expected is None:
            raise PackageError(f"unsupported workspace platform {graph['platform']}")
        # A supported Nix system has to be the workspace target. Other systems
        # load package shells for the system that is actually entering the shell.
        if args.nix_system in {"x86_64-linux", "aarch64-linux", "aarch64-darwin"} and args.nix_system != expected:
            raise PackageError(
                f"workspace platform {graph['platform']} does not match Nix system {args.nix_system}"
            )
        if args.print_inputs:
            print(inputs)
            return 0
        env = os.environ.copy()
        env["OBCX_NIX_SHELL_INPUTS"] = inputs
        command = args.command
        if command[:1] == ["--"]:
            command = command[1:]
        # direnv enables these only for its own Nix process. This nested
        # `print-dev-env` / `develop` must request them itself.
        invocation = [
            "nix",
            "--extra-experimental-features", "nix-command flakes",
            "print-dev-env" if args.print_env else "develop",
            "--impure", "--no-update-lock-file", f"{ROOT}#{args.shell}",
        ]
        if command:
            invocation.extend(["--command", *command])
        return subprocess.run(invocation, env=env, check=False).returncode
    except (PackageError, OSError, UnicodeError) as error:
        print(f"nix-develop: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
