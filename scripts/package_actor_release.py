#!/usr/bin/env python3
"""Prepare deterministic, coordinated actor-only release artifacts."""

from __future__ import annotations

import argparse
from datetime import date
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
from typing import Any, Iterable, Sequence


SOURCE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE_ROOT / "cmake"))
from obcx_package import PackageError
from obcx_package.io import metadata as read_metadata
ARCHITECTURE_ALIASES = {
    "amd64": "x86_64",
    "x86_64": "x86_64",
    "aarch64": "arm64",
    "arm64": "arm64",
}
PLATFORM_SUFFIXES = {
    "linux-x86_64": ".so",
    "linux-arm64": ".so",
}
class PackagingFailure(RuntimeError):
    """Raised when release input is incomplete or inconsistent."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_check(command: Sequence[str | Path]) -> None:
    normalized = [str(part) for part in command]
    print("[check] " + " ".join(normalized), flush=True)
    result = subprocess.run(normalized, cwd=SOURCE_ROOT, check=False)
    if result.returncode != 0:
        raise PackagingFailure(
            f"release prerequisite failed with exit code {result.returncode}"
        )


def clean_output_directory(output: Path, deployment: Path, clean: bool) -> None:
    output = output.resolve()
    protected = (SOURCE_ROOT.resolve(), deployment.resolve())
    if output == Path(output.anchor):
        raise PackagingFailure("output directory cannot be a filesystem root")
    for item in protected:
        if output == item or output in item.parents or item in output.parents:
            raise PackagingFailure(
                "output directory must be separate from source and deployment"
            )
    if output.exists() and any(output.iterdir()):
        if not clean:
            raise PackagingFailure(
                "output directory is not empty; pass --clean to replace it"
            )
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)


def normalized_tar_info(info: tarfile.TarInfo) -> tarfile.TarInfo | None:
    if "__pycache__" in Path(info.name).parts or info.name.endswith(".pyc"):
        return None
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    info.pax_headers = {}
    return info


def deterministic_tar(
    output: Path, members: Iterable[tuple[Path, str]]
) -> None:
    with output.open("wb") as raw_stream:
        with gzip.GzipFile(
            filename="", mode="wb", fileobj=raw_stream, mtime=0
        ) as gzip_stream:
            with tarfile.open(
                fileobj=gzip_stream, mode="w", format=tarfile.PAX_FORMAT
            ) as archive:
                for source, archive_name in sorted(
                    members, key=lambda member: member[1]
                ):
                    archive.add(
                        source,
                        arcname=archive_name,
                        recursive=False,
                        filter=normalized_tar_info,
                    )


def deployment_members(
    deployment: Path, prefix: str
) -> list[tuple[Path, str]]:
    result: list[tuple[Path, str]] = []
    for path in deployment.rglob("*"):
        if not path.is_file() and not path.is_symlink():
            continue
        relative = path.relative_to(deployment)
        if relative.parts[:1] == ("libexec",):
            continue
        if relative.parts[:3] in {
            ("lib", "obcx", "actors"),
            ("share", "obcx", "packages"),
        }:
            continue
        result.append((path, str(Path(prefix) / relative)))
    return result


def registry_members(registry: Path) -> list[tuple[Path, str]]:
    result: list[tuple[Path, str]] = []
    for path in registry.rglob("*"):
        relative = path.relative_to(registry)
        if (
            not path.is_file()
            or "__pycache__" in relative.parts
            or ".git" in relative.parts
        ):
            continue
        result.append((path, str(Path("actor-registry") / relative)))
    return result


def release_platform_name(
    system_name: str | None = None, machine_name: str | None = None
) -> str:
    system_value = (system_name or platform.system()).strip().lower()
    if system_value != "linux":
        raise PackagingFailure(f"unsupported release operating system: {system_value}")
    machine_value = (machine_name or platform.machine()).strip().lower()
    architecture = ARCHITECTURE_ALIASES.get(machine_value)
    if architecture is None:
        raise PackagingFailure(
            f"unsupported release architecture: {machine_value}"
        )
    result = f"linux-{architecture}"
    if result not in PLATFORM_SUFFIXES:
        raise PackagingFailure(f"unsupported release platform: {result}")
    return result


def actor_metadata(path: Path) -> dict[str, Any]:
    document = read_metadata(path, "package")
    if document["package"]["kind"] != "actor":
        raise PackagingFailure("actor archive requires actor package metadata")
    # Do not pretend the old, single-DSO archive is a verified v2 closure.
    # Inventory-driven release packaging remains a separate implementation.
    if document["dependencies"]["libraries"]:
        raise PackagingFailure("declared libraries require v2 inventory/closure packaging, not a single-DSO archive")
    return {"id": document["package"]["id"], "name": document["package"]["name"],
            "version": document["package"]["version"], "abi": document["actor"]["abi"],
            "artifact": document["artifact"]["name"], "platforms": document["artifact"]["platforms"],
            "repository": document["publication"]["repository"]}


def core_version(deployment: Path) -> str:
    header = deployment / "include" / "obcx" / "obcx" / "version.hpp"
    content = header.read_text(encoding="utf-8")
    match = re.search(r'#define OBCX_VERSION_STRING "([^"]+)"', content)
    if match is None:
        raise PackagingFailure(f"cannot read OBCX version from {header}")
    return match.group(1)


def validate_recorded_date(value: str) -> str:
    try:
        date.fromisoformat(value)
    except ValueError as error:
        raise PackagingFailure(
            "recorded date must use YYYY-MM-DD"
        ) from error
    return value


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--deployment", type=Path, required=True)
    result.add_argument("--output-dir", type=Path, required=True)
    result.add_argument("--recorded-date", default=date.today().isoformat())
    result.add_argument("--clean", action="store_true")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    args = parser().parse_args(argv)
    deployment = args.deployment.resolve()
    output = args.output_dir.resolve()

    try:
        recorded_date = validate_recorded_date(args.recorded_date)
        # Keep the archive primitives and their safety regressions, but do not
        # execute the historical coordinator against schema-v2 packages.
        raise PackagingFailure(
            "coordinated releases are unavailable after package-v2 cutover; "
            "inventory/closure packaging is deferred (see "
            "docs/architecture/actor-runtime-v2-operations.md)"
        )

        # Historical pre-v2 orchestration; not current release evidence.
        clean_output_directory(output, deployment, args.clean)

        run_check(
            [sys.executable, "cmake/package_tool.py", "registry-validate", "--entries", "actor-registry/entries"]
        )
        run_check(
            [
                sys.executable,
                "cmake/package_tool.py", "registry-index",
                "--entries", "actor-registry/entries",
                "--output", "actor-registry/index/packages.json", "--check",
            ]
        )

        version = core_version(deployment)
        platform_name = release_platform_name()
        suffix = PLATFORM_SUFFIXES[platform_name]

        actor_inputs: list[tuple[dict[str, Any], Path, Path]] = []
        actors = (
            ("bridge", SOURCE_ROOT / "local_actor" / "obcx-message-bridge"),
            (
                "message_store",
                SOURCE_ROOT / "local_actor" / "obcx-message-store",
            ),
        )
        for artifact_stem, source in actors:
            metadata_path = source / "package.toml"
            metadata = actor_metadata(metadata_path)
            if platform_name not in metadata["platforms"]:
                raise PackagingFailure(
                    f"{metadata['id']} does not declare {platform_name} as a "
                    "built and verified artifact platform"
                )
            installed_metadata = (
                deployment
                / "share"
                / "obcx"
                / "packages"
                / metadata["id"]
                / "package.toml"
            )
            if metadata_path.read_bytes() != installed_metadata.read_bytes():
                raise PackagingFailure(
                    f"installed metadata differs for {metadata['id']}"
                )
            installed_actor = (
                deployment
                / "lib"
                / "obcx"
                / "actors"
                / f"{artifact_stem}{suffix}"
            )
            if not installed_actor.is_file():
                raise PackagingFailure(f"missing installed actor: {installed_actor}")
            actor_inputs.append((metadata, installed_metadata, installed_actor))

        core_archive = output / f"obcx-core-{version}-{platform_name}.tar.gz"
        deterministic_tar(
            core_archive,
            deployment_members(deployment, f"obcx-core-{version}"),
        )

        artifacts: list[dict[str, Any]] = []
        for metadata, installed_metadata, installed_actor in actor_inputs:
            raw_asset = output / (
                f"{metadata['artifact']}-{platform_name}{suffix}"
            )
            shutil.copyfile(installed_actor, raw_asset)
            actor_archive = output / (
                f"{metadata['name']}-{metadata['version']}-{platform_name}.tar.gz"
            )
            deterministic_tar(
                actor_archive,
                [
                    (
                        installed_actor,
                        str(Path("lib/obcx/actors") / installed_actor.name),
                    ),
                    (
                        installed_metadata,
                        str(
                            Path("share/obcx/packages")
                            / metadata["id"]
                            / "package.toml"
                        ),
                    ),
                ],
            )
            artifacts.extend(
                [
                    {
                        "kind": "actor-release-asset",
                        "actor_id": metadata["id"],
                        "version": metadata["version"],
                        "repository": metadata["repository"],
                        "file": raw_asset.name,
                    },
                    {
                        "kind": "actor-install-archive",
                        "actor_id": metadata["id"],
                        "version": metadata["version"],
                        "repository": metadata["repository"],
                        "file": actor_archive.name,
                    },
                ]
            )

        registry_archive = output / f"obcx-actor-registry-{recorded_date}.tar.gz"
        deterministic_tar(
            registry_archive,
            registry_members(
                SOURCE_ROOT / "local_actor" / "obcx-actor-registry"
            ),
        )
        artifacts.extend(
            [
                {
                    "kind": "core-install-archive",
                    "version": version,
                    "repository": "https://github.com/Onebot-CXX/OBCX",
                    "file": core_archive.name,
                },
                {
                    "kind": "actor-registry-archive",
                    "recorded_date": recorded_date,
                    "repository": "https://github.com/Onebot-CXX/actor-registry",
                    "file": registry_archive.name,
                },
            ]
        )

        for artifact in artifacts:
            artifact_path = output / artifact["file"]
            artifact["sha256"] = sha256(artifact_path)
            artifact["size_bytes"] = artifact_path.stat().st_size
        artifacts.sort(key=lambda artifact: artifact["file"])

        manifest = {
            "schema_version": 1,
            "status": "prepared-not-published",
            "recorded_date": recorded_date,
            "platform": platform_name,
            "obcx_version": version,
            "actor_abi": 2,
            "artifacts": artifacts,
        }
        manifest_path = output / "coordinated-release-manifest.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

        checksummed = [output / item["file"] for item in artifacts]
        checksummed.append(manifest_path)
        checksum_path = output / "SHA256SUMS"
        checksum_path.write_text(
            "".join(
                f"{sha256(path)}  {path.name}\n"
                for path in sorted(checksummed, key=lambda item: item.name)
            ),
            encoding="utf-8",
        )
        print(f"prepared {len(artifacts)} coordinated artifacts in {output}")
        return 0
    except (OSError, PackagingFailure, PackageError, json.JSONDecodeError) as error:
        print(f"release packaging failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
