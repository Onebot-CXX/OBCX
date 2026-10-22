#!/usr/bin/env python3
"""Freeze an existing SDK and build actors without a core compilation target."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "cmake"))
from obcx_package import PackageError
from obcx_package.io import atomic_write, encoded
from obcx_package.resolver import Resolver

# Only build inputs, never arbitrary environment variables or credentials.
BUILD_ENVIRONMENT = (
    "NIX_CC", "NIX_BINTOOLS", "NIX_CFLAGS_COMPILE", "NIX_CFLAGS_LINK",
    "NIX_LDFLAGS", "NIX_CXXSTDLIB_COMPILE", "NIX_CXXSTDLIB_LINK",
    "NIX_ENFORCE_PURITY", "NIX_DONT_SET_RPATH", "NIX_HARDENING_ENABLE",
    "PKG_CONFIG_PATH", "PKG_CONFIG_LIBDIR", "CMAKE_PREFIX_PATH",
    "CPATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cache_values(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"([^/#][^:]*):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    return values


def run(arguments: list[str], **kwargs) -> None:
    print("+ " + shlex.join(arguments), flush=True)
    subprocess.run(arguments, check=True, **kwargs)


def inventory(root: Path) -> dict[str, str]:
    return {path.relative_to(root).as_posix(): sha256(path)
            for path in sorted(root.rglob("*")) if path.is_file()
            and path.relative_to(root).as_posix() != "sdk-snapshot.json"}


def load_sdk(root: Path) -> dict:
    manifest = json.loads((root / "sdk-snapshot.json").read_text())
    require(manifest["schema_version"] == 1, "Unsupported SDK snapshot version")
    require(inventory(root) == manifest["files"],
            "SDK snapshot changed; create a new snapshot, never overwrite a live SDK")
    for tool in ("cmake", "ninja", "compiler"):
        require(Path(manifest[tool]).is_file(), f"Pinned {tool} is missing: {manifest[tool]}")
    return manifest


def build_environment(manifest: dict) -> dict[str, str]:
    environment = dict(os.environ)
    for name, value in manifest["environment"].items():
        if value is None:
            environment.pop(name, None)
        else:
            environment[name] = value
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    # Never let a caller's staging prefix redirect the SDK installation.
    environment.pop("DESTDIR", None)
    return environment


def snapshot_sdk(args) -> None:
    build = args.core_build.resolve(strict=True)
    values = cache_values(build / "CMakeCache.txt")
    require(values["CMAKE_PROJECT_NAME"] == "OBCX", "Expected an OBCX core build")
    require(values["CMAKE_GENERATOR"] == "Ninja", "SDK snapshots require single-config Ninja")
    require(values["CMAKE_CXX_COMPILER"].startswith("/nix/store/"),
            "SDK snapshots currently require the repository's pinned Nix toolchain")
    require(not args.output.exists() and not args.output.is_symlink(),
            f"Refusing to overwrite SDK snapshot: {args.output}")
    # This is deliberately a dry run: SDK creation must never update live core.
    planned = subprocess.check_output(
        [values["CMAKE_MAKE_PROGRAM"], "-C", str(build), "-n", "obcx_core"],
        text=True, stderr=subprocess.STDOUT)
    require("ninja: no work to do." in planned and not re.search(r"^\[\d+/", planned, re.M),
            "Core has pending build work. Build/restart it explicitly before snapshotting:\n" + planned)
    original = build / "src/libobcx_core.so"
    before = sha256(original)
    output = args.output.resolve()
    output.mkdir(parents=True)
    manifest = {
        "schema_version": 1,
        "core_build": str(build),
        "core_sha256": before,
        "version": values["CMAKE_PROJECT_VERSION"],
        "compiler": values["CMAKE_CXX_COMPILER"],
        "cmake": values["CMAKE_COMMAND"],
        "ninja": values["CMAKE_MAKE_PROGRAM"],
        "environment": {name: os.environ.get(name) for name in BUILD_ENVIRONMENT},
        "cache": {},
    }
    manifest["external_prefixes"] = sorted(set(re.findall(
        r"/nix/store/[a-z0-9]{32}-[A-Za-z0-9+._?=-]+",
        json.dumps([values, manifest["environment"]]))))
    # Preserve discovered Nix/provider locations and explicit compilation flags.
    for name, value in values.items():
        if (value.startswith("/nix/store/") and name.endswith(
                ("_DIR", "_INCLUDE_DIR", "_LIBRARY", "_LIBRARY_RELEASE", "_EXECUTABLE"))
                and not name.startswith(("CMAKE_", "OBCX_", "_"))):
            manifest["cache"][name] = value
        if name.startswith(("CMAKE_CXX_FLAGS", "CMAKE_SHARED_LINKER_FLAGS")) and "-" not in name:
            manifest["cache"][name] = value
    # Install only src/'s SDK rules, not the app or any actor; no compilation.
    run([manifest["cmake"], f"-DCMAKE_INSTALL_PREFIX={output}",
         "-DCMAKE_INSTALL_LOCAL_ONLY=ON", "-DCMAKE_INSTALL_DO_STRIP=OFF",
         f"-DCMAKE_INSTALL_CONFIG_NAME={values['CMAKE_BUILD_TYPE']}",
         "-P", str(build / "src/cmake_install.cmake")], env=build_environment(manifest))
    configs = list(output.glob("*/cmake/obcx-sdk/obcx-sdk-config.cmake"))
    require(len(configs) == 1, "Expected exactly one installed SDK config")
    manifest["sdk_dir"] = configs[0].parent.relative_to(output).as_posix()
    installed = configs[0].parents[2] / "libobcx_core.so"
    require(sha256(original) == before == sha256(installed),
            "Installed SDK core differs from the built core; do not use this snapshot for reload")
    manifest["files"] = inventory(output)
    atomic_write(output / "sdk-snapshot.json", encoded(manifest))
    # New generations use new paths. Prevent accidental install-over/update.
    for path in output.rglob("*"):
        path.chmod(path.stat().st_mode & ~0o222)
    output.chmod(output.stat().st_mode & ~0o222)
    print(f"Frozen SDK: {output}\nCore SHA-256: {before}")


def inline(value) -> str:
    if isinstance(value, dict):
        return "{ " + ", ".join(json.dumps(k) + " = " + inline(v)
                                for k, v in value.items()) + " }"
    if isinstance(value, list):
        return "[" + ", ".join(inline(v) for v in value) + "]"
    return json.dumps(value, ensure_ascii=False)


def configure(args) -> None:
    sdk = args.sdk.resolve(strict=True)
    manifest = load_sdk(sdk)
    workspace = args.workspace.resolve(strict=True)
    output = args.output.resolve()
    require(not output.exists() and not args.output.is_symlink(),
            f"Configure requires a new actor build directory: {output}")
    resolver = Resolver(workspace, args.cache.resolve(), args.mode, "deny")
    resolver.options["roots"] = sorted(set(args.actors))
    graph = resolver.resolve()
    nodes = {node["id"]: node for node in graph["packages"]}
    require(all(nodes[root]["kind"] == "actor" for root in args.actors),
            "Specify actor package IDs, not implementation target names")
    require(graph["profile"] == "production", "Use a production workspace for reloadable actors")
    output.mkdir(parents=True)
    generated = output / "workspace"
    generated.mkdir()
    document = copy.deepcopy(resolver.workspace)
    document["sources"] = []
    for node in graph["packages"]:
        source = copy.deepcopy(node["source"])
        if source["kind"] == "path":
            source["path"] = os.path.relpath(node["source_dir"], generated)
            if source["provenance"]["kind"] == "archive":
                source["provenance"]["path"] = os.path.relpath(
                    workspace.parent / source["provenance"]["path"], generated)
        document["sources"].append(source)
    document["providers"] = copy.deepcopy(graph["providers"])
    for provider in document["providers"]:
        relative = f"providers/{provider['id']}.json"
        if provider["id"] == "obcx-sdk":
            provider["kind"] = "installed-sdk"
            provider["provenance"] = {"kind": "installed-sdk", "path": relative}
            provider["version_probe"] = {"kind": "target-property", "target": "obcx::obcx_core",
                                         "property": "OBCX_SDK_VERSION"}
            environment = {"schema_version": 2, "kind": "installed-sdk",
                           "prefixes": [{"base": "absolute", "path": path}
                                        for path in [str(sdk), *manifest["external_prefixes"]]]}
        else:
            environment = json.loads((workspace.parent / provider["provenance"]["path"]).read_text())
            for prefix in environment["prefixes"]:
                require(prefix["base"] != "build",
                        "External provider prefixes must be workspace-relative or absolute, not build-relative")
                if prefix["base"] == "workspace":
                    prefix["path"] = str((workspace.parent / prefix["path"]).resolve())
                    prefix["base"] = "absolute"
            provider["provenance"]["path"] = relative
            if provider["version_probe"]["kind"] == "receipt":
                receipt = workspace.parent / provider["version_probe"]["path"]
                provider["version_probe"]["path"] = f"providers/{provider['id']}.receipt.json"
                atomic_write(generated / provider["version_probe"]["path"], receipt.read_bytes())
        atomic_write(generated / relative, encoded(environment))
    require(any(p["id"] == "obcx-sdk" for p in document["providers"]), "Actor graph has no SDK provider")
    atomic_write(generated / "packages.toml",
                 "".join(k + " = " + inline(v) + "\n" for k, v in document.items()).encode())
    actors = [node["metadata"]["artifact"] for node in graph["packages"] if node["kind"] == "actor"]
    state = {"sdk": str(sdk), "sdk_manifest_sha256": sha256(sdk / "sdk-snapshot.json"),
             "targets": sorted(actor["target"] for actor in actors),
             "artifacts": sorted(actor["name"] + ".so" for actor in actors)}
    run([manifest["cmake"], "-S", str(REPO / "cmake/actors"), "-B", str(output / "cmake"), "-G", "Ninja",
         f"-Dobcx-sdk_DIR={sdk / manifest['sdk_dir']}",
         f"-DCMAKE_CXX_COMPILER={manifest['compiler']}", f"-DCMAKE_MAKE_PROGRAM={manifest['ninja']}",
         f"-DCMAKE_BUILD_TYPE={args.configuration}", f"-DENABLE_DEBUG_TRACE={args.debug_trace}",
         f"-DOBCX_PACKAGES_WORKSPACE={generated / 'packages.toml'}",
         f"-DOBCX_PACKAGES_CACHE={args.cache.resolve()}", f"-DOBCX_PACKAGES_MODE={args.mode}",
         f"-DOBCX_PACKAGES_STATE_DIR={output / 'package-state'}",
         *[f"-D{name}={value}" for name, value in manifest["cache"].items()]],
        env=build_environment(manifest))
    verify_imported_sdk(output, sdk)
    atomic_write(output / "actor-build.json", encoded(state))
    print(f"Isolated actors: {', '.join(state['targets'])}\nBuild directory: {output}")


def verify_imported_sdk(output: Path, sdk: Path) -> None:
    snapshot = json.loads((output / "package-state/targets.json").read_text())
    name = snapshot["aliases"]["obcx::obcx_core"]
    target = snapshot["targets"][name]
    require(target["IMPORTED"] == "TRUE", "Refusing a buildable core target in the actor workspace")
    require(target["locations"] and all(
        Path(path).resolve().is_relative_to(sdk) for path in target["locations"]),
            "Actor workspace resolved a different SDK")


def build_actors(args) -> None:
    output = args.output.resolve(strict=True)
    state = json.loads((output / "actor-build.json").read_text())
    sdk = Path(state["sdk"])
    require(sha256(sdk / "sdk-snapshot.json") == state["sdk_manifest_sha256"],
            "SDK selection changed; configure a new actor build directory")
    manifest = load_sdk(sdk)
    require(args.jobs >= 6, "Use at least six parallel compilation workers")
    verify_imported_sdk(output, sdk)
    run([manifest["cmake"], "--build", str(output / "cmake"), "--parallel", str(args.jobs),
         "--target", *state["targets"]], env=build_environment(manifest))
    load_sdk(sdk)
    verify_imported_sdk(output, sdk)
    for artifact in state["artifacts"]:
        path = output / "cmake/actors" / artifact
        require(path.is_file(), f"Actor artifact is missing: {path}")
        print(path)
    print("SDK unchanged. Nothing was deployed or restarted.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("snapshot-sdk", help="Install/freeze an already-built core SDK; never compile")
    command.add_argument("--core-build", type=Path, required=True)
    command.add_argument("--output", type=Path, required=True)
    command = commands.add_parser("configure", help="Create an isolated actor build using a frozen SDK")
    for option in ("sdk", "workspace", "cache", "output"):
        command.add_argument("--" + option, type=Path, required=True)
    command.add_argument("--actors", nargs="+", required=True)
    command.add_argument("--mode", choices=("development", "release"), required=True)
    command.add_argument("--configuration", choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"), required=True)
    command.add_argument("--debug-trace", choices=("ON", "OFF"), required=True)
    command = commands.add_parser("build", help="Incrementally rebuild only the isolated actor closure")
    command.add_argument("--output", type=Path, required=True)
    command.add_argument("--jobs", type=int, required=True)
    args = parser.parse_args()
    {"snapshot-sdk": snapshot_sdk, "configure": configure, "build": build_actors}[args.command](args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, PackageError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"actor-build: {error}", file=sys.stderr)
        sys.exit(1)
