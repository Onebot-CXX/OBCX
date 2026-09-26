"""Isolated, complete v2 fixtures; never fetch real repositories."""
from __future__ import annotations

import copy
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cmake"))
from obcx_package.io import digest, encoded

FIXTURES = ROOT / "tests/fixtures/package_v2"


def fixture(kind: str) -> dict:
    return tomllib.loads((FIXTURES / kind / "package.toml").read_text())


def toml_value(value) -> str:
    if isinstance(value, dict):
        return "{ " + ", ".join(f"{json.dumps(k)} = {toml_value(v)}" for k, v in value.items()) + " }"
    if isinstance(value, list):
        return "[" + ", ".join(toml_value(item) for item in value) + "]"
    return json.dumps(value)


def write_toml(path: Path, document: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(f"{key} = {toml_value(value)}" for key, value in document.items()) + "\n")


def archive(directory: Path) -> bytes:
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w") as tar:
        for path in sorted(directory.rglob("*")):
            tar.add(path, arcname=path.relative_to(directory).as_posix(), recursive=False)
    return stream.getvalue()


class WorkspaceCase(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = self.root / "packages.toml"
        self.lock = self.root / "packages.lock"
        self.cache = self.root / "cache"
        (self.root / "sdk-input.txt").write_text("offline SDK fixture, not a deployment")
        receipt = encoded({"schema_version": 2, "kind": "workspace-sdk",
                           "inputs": [{"path": "sdk-input.txt", "sha256": digest((self.root / "sdk-input.txt").read_bytes())}],
                           "prefixes": [{"base": "workspace", "path": "sdk"}, {"base": "build", "path": "."}]})
        (self.root / "sdk-receipt.json").write_bytes(receipt)
        self.workspace = {"schema_version": 2,
                          "workspace": {"roots": ["example.actor"], "platform": "linux-x86_64", "profile": "production"},
                          "sources": [], "providers": [{"id": "obcx-sdk", "kind": "workspace-sdk",
                          "package": "obcx-sdk", "components": [], "targets": ["obcx::obcx_core"],
                          "version": "1.1.0", "version_scheme": "semver",
                          "version_probe": {"kind": "cmake-variable", "name": "PROJECT_VERSION"},
                          "package_manager": {"kind": "none"},
                          "provenance": {"kind": "workspace-sdk", "path": "sdk-receipt.json", "sha256": digest(receipt)}}]}
        self.packages = {}
        self.add_package(fixture("actor"))
        self.add_package(fixture("library"))
        self.save()

    def add_package(self, document: dict) -> None:
        package_id = document["package"]["id"]
        document = copy.deepcopy(document)
        self.packages[package_id] = document
        self.workspace["sources"].append({"id": package_id, "kind": "path", "path": package_id,
                                           "provenance": {"kind": "working-tree"}})
        write_toml(self.root / package_id / "package.toml", document)

    def library(self, name: str) -> dict:
        document = fixture("library")
        document["package"].update(id=f"example.{name}", name=name)
        document["artifact"].update(name=name, target=f"example_{name}", export_target=f"example::{name}")
        return document

    def edge(self, name: str) -> dict:
        return {"id": f"example.{name}", "version": ">=0.1.0,<0.2.0", "target": f"example::{name}", "visibility": "private"}

    def save(self) -> None:
        for package_id, document in self.packages.items():
            write_toml(self.root / package_id / "package.toml", document)
        write_toml(self.manifest, self.workspace)

    def resolve(self, mode="development", network="deny"):
        from obcx_package.resolver import Resolver
        self.save()
        return Resolver(self.manifest, self.cache, mode, network).resolve()

    def cli(self, command: str) -> list[str]:
        return [sys.executable, str(ROOT / "cmake/package_tool.py"), command,
                "--workspace", str(self.manifest), "--lock", str(self.lock), "--cache", str(self.cache),
                "--mode", "development", "--network", "deny"]
