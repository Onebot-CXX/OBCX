#!/usr/bin/env python3
"""OBCX v2 package-tool: explicit sources, current graphs, no build execution."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

from obcx_package import PackageError, TOOL_VERSION
from obcx_package.build_inputs import validate_outputs
from obcx_package.contracts import schema
from obcx_package.io import atomic_write, digest, encoded, exclusive, metadata, read_json
from obcx_package.resolver import Resolver, paths_to


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version=TOOL_VERSION)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("validate", "inspect"):
        command = commands.add_parser(name)
        command.add_argument("path", type=Path)
        command.add_argument("--kind", required=True, choices=("package", "workspace", "resolved-packages", "package-build-receipt",
                                                              "provider-receipt", "provider-environment"))
    command = commands.add_parser("schema")
    command.add_argument("--kind", required=True, choices=("package", "workspace", "resolved-packages", "package-build-receipt",
                                                          "provider-receipt", "provider-environment"))
    command.add_argument("--output", required=True, type=Path)
    for name in ("registry-validate", "registry-index"):
        command = commands.add_parser(name)
        command.add_argument("--entries", required=True, type=Path)
        if name == "registry-index":
            command.add_argument("--output", required=True, type=Path)
            command.add_argument("--check", action="store_true")
    for name in ("resolve", "prepare", "check", "explain"):
        command = commands.add_parser(name)
        command.add_argument("--workspace", required=True, type=Path)
        command.add_argument("--cache", required=True, type=Path)
        command.add_argument("--mode", required=True, choices=("development", "release"))
        command.add_argument("--network", required=True, choices=("allow", "deny"))
        if name in {"resolve", "prepare"}:
            command.add_argument("--graph", required=True, type=Path)
        if name == "explain":
            command.add_argument("id")
    args = parser.parse_args(argv)
    try:
        if args.command in {"validate", "inspect"}:
            document = metadata(args.path, args.kind) if args.kind in {"package", "workspace"} else read_json(args.path, args.kind)
            result = document if args.command == "inspect" else {"valid": True, "kind": args.kind}
        elif args.command in {"registry-validate", "registry-index"}:
            from obcx_package.registry import build_index, write_index
            index = (build_index(args.entries) if args.command == "registry-validate" else
                     write_index(args.entries, args.output, args.check))
            result = {"valid": True, "packages": len(index["packages"]), "status": index["status"]}
        elif args.command == "schema":
            atomic_write(args.output, encoded(schema(args.kind)))
            result = {"schema": args.kind, "tool_version": TOOL_VERSION}
        else:
            graph = Resolver(args.workspace, args.cache, args.mode, args.network).resolve()
            if hasattr(args, "graph"):
                validate_outputs(args.workspace, graph, [args.graph])
                with exclusive(args.graph.with_name(args.graph.name + ".guard")):
                    atomic_write(args.graph, encoded(graph))
            result = {"valid": True, "graph_sha256": digest(encoded(graph)), "order": graph["order"]}
            if args.command == "prepare":
                result["system_requirements"] = graph["providers"]
            elif args.command == "explain":
                nodes = {p["id"]: p for p in graph["packages"]}
                nodes.update({p["id"]: p for p in graph["providers"]})
                if args.id not in nodes:
                    raise PackageError("explain: requested ID is not in the selected graph")
                result = {"selected": nodes[args.id],
                          "chains": paths_to(graph["roots"], graph["edges"], args.id),
                          "requirements": [e for e in graph["edges"] if e["to"] == args.id]}
        sys.stdout.write(encoded(result).decode())
        return 0
    except PackageError as error:
        print(f"package-tool: {error}", file=sys.stderr)
        return 1
    except (OSError, UnicodeError):
        print("package-tool: filesystem/encoding operation failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
