#!/usr/bin/env python3
"""Resolve actor IDs and snapshot a minimal production workspace without editing the original."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tomllib


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def inline(value):
    if isinstance(value, dict):
        return "{ " + ", ".join(json.dumps(k) + " = " + inline(v) for k, v in value.items()) + " }"
    if isinstance(value, list):
        return "[" + ", ".join(inline(v) for v in value) + "]"
    return json.dumps(value, ensure_ascii=False)


def snapshot(source, destination, *, verified_tree):
    destination.mkdir(parents=True, exist_ok=True)
    if verified_tree:
        paths = sorted(p.relative_to(source).as_posix() for p in source.rglob("*")
                       if (p.is_file() or p.is_symlink())
                       and not {".git", ".hg", ".svn"}.intersection(p.relative_to(source).parts))
    else:
        listed = subprocess.check_output(["git", "-C", str(source), "ls-files", "-z", "--cached", "--others", "--exclude-standard"])
        paths = sorted(set(listed.decode().split("\0")) - {""})
    require(paths, f"No source files found in {source}")
    records = {}
    for relative in paths:
        original = source / relative
        if not original.exists() and not original.is_symlink():
            continue
        target = destination / relative
        require(not target.exists() and not target.is_symlink(), f"Snapshot path collision: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        if original.is_symlink():
            require(not Path(os.readlink(original)).is_absolute(), f"Absolute source symlink is not snapshot-safe: {relative}")
            require(original.resolve().is_relative_to(source), f"Source symlink escapes its root: {relative}")
            content = os.readlink(original).encode()
            target.symlink_to(os.readlink(original))
            mode = "symlink"
        else:
            require(original.is_file(), f"Non-file source input: {relative}")
            content = original.read_bytes()
            target.write_bytes(content)
            mode = "executable" if original.stat().st_mode & 0o111 else "file"
            target.chmod(0o755 if mode == "executable" else 0o644)
        records[relative] = {"sha256": hashlib.sha256(content).hexdigest(), "mode": mode}
    for relative, record in records.items():
        p = source / relative
        content = os.readlink(p).encode() if p.is_symlink() else p.read_bytes()
        require(hashlib.sha256(content).hexdigest() == record["sha256"], f"Source changed while snapshotting: {relative}")
    return {"source": str(source), "sha256": hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest(), "files": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--actors", nargs="+", required=True, help="Explicit package IDs, not guessed short names")
    parser.add_argument("--output", type=Path, required=True, help="New output directory")
    parser.add_argument("--platform", choices=("linux-x86_64", "linux-arm64"), required=True)
    args = parser.parse_args()
    repo = args.repo.resolve(strict=True)
    native = {"x86_64": "linux-x86_64", "aarch64": "linux-arm64"}
    require(platform.system() == "Linux" and native.get(platform.machine()) == args.platform,
            "This workflow requires native Linux compilation for the requested platform")
    require(not args.output.is_symlink(), "Output must not be a symlink")
    output = args.output.resolve()
    require(not output.exists(), f"Output already exists: {output}")
    if output.is_relative_to(repo):
        ignored = subprocess.run(["git", "-C", str(repo), "check-ignore", "--quiet", "--no-index", str(output)])
        require(ignored.returncode == 0, "An in-repository output directory must be Git-ignored")
    output.mkdir(mode=0o700, parents=True)
    sys.path.insert(0, str(repo / "cmake"))
    from obcx_package.io import metadata
    from obcx_package.resolver import Resolver
    resolver = Resolver(repo / "packages.toml", output / "resolved-sources", "development", "deny")
    resolver.options["roots"] = list(dict.fromkeys(args.actors))
    resolver.options["profile"] = "production"
    resolver.options["platform"] = args.platform
    graph = resolver.resolve()
    nodes = {n["id"]: n for n in graph["packages"]}
    require(all(nodes[p]["kind"] == "actor" for p in args.actors), "Every requested root must be an actor")
    source = output / "source"
    snapshots = {"core": snapshot(repo, source, verified_tree=False)}
    workspace = tomllib.loads((repo / "packages.toml").read_text())
    workspace["workspace"] = {"roots": resolver.options["roots"], "platform": args.platform, "profile": "production"}
    workspace["sources"] = []
    for key, node in sorted(nodes.items()):
        relative = Path("image-packages") / key
        require(len(relative.parts) == 2 and key not in (".", ".."), f"Invalid package ID: {key}")
        original = Path(node["source_dir"])
        snapshots[key] = snapshot(original, source / relative, verified_tree=node["source"]["kind"] == "git")
        require(metadata(source / relative / "package.toml", "package") == node["metadata"],
                f"Package metadata changed during resolution: {key}; prepare a new snapshot")
        workspace["sources"].append({"id": key, "kind": "path", "path": relative.as_posix(), "provenance": {"kind": "working-tree"}})
    used = {p["id"] for p in graph["providers"]}
    workspace["providers"] = [p for p in workspace["providers"] if p["id"] in used]
    for provider in workspace["providers"]:
        paths = [provider["provenance"]["path"]]
        if provider["version_probe"]["kind"] == "receipt":
            paths.append(provider["version_probe"]["path"])
        for relative in paths:
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(repo / relative, target)
    (source / "packages.toml").write_text("".join(k + " = " + inline(v) + "\n" for k, v in workspace.items()))
    actors = sorted(n["metadata"]["artifact"]["name"] for n in nodes.values() if n["kind"] == "actor")
    report = {"source": str(source), "actors": actors, "packages": sorted(nodes), "platform": args.platform,
              "snapshot_sha256": hashlib.sha256(json.dumps({k: v["sha256"] for k, v in snapshots.items()}, sort_keys=True).encode()).hexdigest(),
              "snapshots": snapshots, "resolved_graph": graph}
    (output / "build-plan.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("source", "actors", "packages", "platform", "snapshot_sha256")}, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Workspace preparation failed: {error}", file=sys.stderr)
        sys.exit(1)
