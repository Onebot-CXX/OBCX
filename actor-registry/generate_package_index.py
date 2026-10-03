#!/usr/bin/env python3
"""Delegate local registry checks to explicitly supplied OBCX v2 tooling.

This repository does not carry a metadata validator. Release-tool bundle pinning
and verified download assets remain separate from this development entry point.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("validate", "generate"):
        command = commands.add_parser(name)
        command.add_argument("--entries", type=Path, required=True)
        if name == "generate":
            command.add_argument("--output", type=Path, required=True)
            command.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    tool = args.tool.resolve()
    env = os.environ.copy()
    env.pop("PYTHONPATH", None)
    try:
        version = subprocess.run([sys.executable, str(tool), "--version"], env=env,
                                 capture_output=True, text=True, check=True)
        if version.stdout.strip() != "2.0.0":
            raise ValueError("registry requires canonical package-tool 2.0.0")
        command = [sys.executable, str(tool),
                   "registry-validate" if args.command == "validate" else "registry-index",
                   "--entries", str(args.entries.resolve())]
        if args.command == "generate":
            command.extend(["--output", str(args.output.resolve())])
            if args.check:
                command.append("--check")
        return subprocess.run(command, env=env, check=False).returncode
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"package registry: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
