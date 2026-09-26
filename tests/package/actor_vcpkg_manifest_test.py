"""v2 graph export regressions; no configure-first actor discovery."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import ROOT, WorkspaceCase
from obcx_package import PackageError
from obcx_package.io import digest, encoded
from obcx_package.providers import merge_vcpkg_dependencies, vcpkg_manifest
from obcx_package.resolver import write_lock

BASELINE = "0123456789abcdef0123456789abcdef01234567"


class PackageVcpkgManifestTest(WorkspaceCase):
    def setUp(self):
        super().setUp()
        self.packages["example.mapping"]["dependencies"]["system"] = [
            {"id": "fixture.sys", "version": ">=2.0.0,<3.0.0", "targets": ["Fixture::lib"], "visibility": "private"}]
        environment = {"schema_version": 2, "kind": "vcpkg",
                       "inputs": [{"path": "sdk-input.txt", "sha256": digest((self.root / "sdk-input.txt").read_bytes())}],
                       "prefixes": [{"base": "workspace", "path": "vcpkg_installed/x64-linux"}]}
        content = encoded(environment)
        (self.root / "vcpkg-environment.json").write_bytes(content)
        self.provider = {"id": "fixture.sys", "kind": "cmake-module", "package": "Fixture", "components": [],
                         "targets": ["Fixture::lib"], "version": "2.4.0", "version_scheme": "semver",
                         "version_probe": {"kind": "cmake-variable", "name": "Fixture_VERSION"},
                         "package_manager": {"kind": "vcpkg", "port": "fixture-port", "features": [],
                                             "default_features": True, "baseline": BASELINE},
                         "provenance": {"kind": "vcpkg", "path": "vcpkg-environment.json", "sha256": digest(content)}}
        self.workspace["providers"].append(self.provider)
        self.base = {"name": "fixture-core", "version": "1.1.0", "dependencies": ["zlib"]}
        self.base_file = self.root / "base.json"
        self.graph_file = self.root / "resolved-packages.json"
        self.output = self.root / "vcpkg.json"
        self.base_file.write_bytes(encoded(self.base))

    def prepare(self):
        graph = self.resolve()
        write_lock(self.lock, graph)
        self.graph_file.write_bytes(encoded(graph))
        return graph

    def command(self):
        return [sys.executable, str(ROOT / "cmake/gen_vcpkg_manifest.py"),
                "--workspace", str(self.manifest), "--lock", str(self.lock), "--graph", str(self.graph_file),
                "--cache", str(self.cache), "--mode", "development", "--base", str(self.base_file),
                "--baseline", BASELINE, "--output", str(self.output)]

    def test_exports_transitive_dependencies_before_cmake(self):
        marker = self.root / "must-not-exist"
        (self.root / "example.mapping/CMakeLists.txt").write_text(f'file(WRITE "{marker}" "bad")\n')
        self.prepare()
        original_lock = self.lock.read_bytes()
        process = subprocess.run(self.command(), capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stderr)
        output = json.loads(self.output.read_bytes())
        self.assertEqual(output["builtin-baseline"], BASELINE)
        self.assertEqual([item["name"] for item in output["dependencies"]], ["fixture-port", "zlib"])
        self.assertFalse(marker.exists())
        self.assertEqual(original_lock, self.lock.read_bytes())

    def test_rejects_forged_source_mapping_without_overwriting_output(self):
        graph = self.prepare()
        graph["packages"][0]["source_dir"] = str(self.root / "untrusted-source")
        self.graph_file.write_bytes(encoded(graph))
        self.output.write_bytes(b"previous output")
        process = subprocess.run(self.command(), capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("source/metadata mapping drift", process.stderr)
        self.assertEqual(self.output.read_bytes(), b"previous output")

    def test_explicit_port_and_baseline_are_required(self):
        graph = self.prepare()
        for manager, expected in (({"kind": "none"}, "explicit vcpkg port"),
                                  ({**self.provider["package_manager"], "baseline": "a" * 40}, "baseline conflict")):
            changed = copy.deepcopy(graph)
            target = next(p for p in changed["lock"]["providers"] if p["id"] == "fixture.sys")
            target["package_manager"] = manager
            with self.subTest(manager=manager), self.assertRaisesRegex(PackageError, expected):
                vcpkg_manifest(changed, self.base, BASELINE)
        with self.assertRaisesRegex(PackageError, "explicit complete commit"):
            vcpkg_manifest(graph, self.base, "main")

    def test_merge_preserves_core_features_and_rejects_policy_conflicts(self):
        base = ["zlib", {"name": "boost", "features": ["asio"], "version>=": "1.80.0"}]
        additions = [{"name": "boost", "features": ["thread"], "default-features": True}]
        result = merge_vcpkg_dependencies(base, additions)
        self.assertEqual(result[0]["features"], ["asio", "thread"])
        self.assertEqual(result[0]["version>="], "1.80.0")
        self.assertEqual(len(result), 2)
        additions[0]["default-features"] = False
        with self.assertRaisesRegex(PackageError, "conflicting default-features"):
            merge_vcpkg_dependencies(base, additions)


if __name__ == "__main__":
    unittest.main()
