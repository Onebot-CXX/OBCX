from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import WorkspaceCase
from obcx_package import PackageError
from obcx_package.io import digest, encoded
from obcx_package.resolver import Resolver, paths_to


class PackageResolutionTest(WorkspaceCase):
    def test_empty_roots_explicit_sdk_only(self):
        self.workspace["workspace"]["roots"] = []
        self.workspace["sources"][0]["path"] = "does-not-exist"
        graph = self.resolve()
        self.assertEqual(graph["order"], [])
        self.assertEqual(len(graph["unused_sources"]), 2)

    def test_source_order_and_metadata_array_order_preserve_semantics(self):
        self.add_package(self.library("other"))
        self.packages["example.actor"]["dependencies"]["libraries"].append(self.edge("other"))
        first = self.resolve()
        self.workspace["sources"].reverse()
        self.packages["example.actor"]["dependencies"]["libraries"].reverse()
        self.packages["example.mapping"]["artifact"]["platforms"].reverse()
        second = self.resolve()
        for graph in (first, second):
            for node in graph["packages"]:
                del node["source_receipt"]  # Equivalent TOML may have different raw bytes.
        self.assertEqual(first, second)

    def test_paths_are_relative_to_manifest_not_cwd(self):
        first = self.resolve()
        previous = Path.cwd()
        try:
            os.chdir(self.root / "example.mapping")
            second = Resolver(self.manifest, self.cache, "development", "deny").resolve()
        finally:
            os.chdir(previous)
        self.assertEqual(first, second)

    def test_missing_transitive_binding_gives_full_chain(self):
        self.packages["example.mapping"]["dependencies"]["libraries"] = [self.edge("missing")]
        with self.assertRaisesRegex(PackageError, "example.actor -> example.mapping -> example.missing"):
            self.resolve()

    def test_unused_remote_binding_is_not_fetched(self):
        self.workspace["sources"].append({"id": "unused.remote", "kind": "git", "repository": "https://example.invalid/no-network",
                                           "commit": "a" * 40, "subdir": "."})
        with patch("obcx_package.sources.Sources.prepare_git", side_effect=AssertionError("must not fetch")):
            graph = self.resolve()
        self.assertEqual(graph["unused_sources"], ["unused.remote"])

    def test_identity_mismatch(self):
        self.packages["example.mapping"]["package"]["id"] = "different.mapping"
        with self.assertRaisesRegex(PackageError, "package.id is different.mapping"):
            self.resolve()

    def test_duplicate_source_rejected_before_read(self):
        self.workspace["sources"].append(copy.deepcopy(self.workspace["sources"][1]))
        with self.assertRaises(PackageError):
            self.resolve()

    def test_target_kind_mismatch(self):
        self.packages["example.actor"]["dependencies"]["actors"] = [{"id": "example.mapping", "version": "=0.1.0"}]
        self.packages["example.actor"]["dependencies"]["libraries"] = []
        with self.assertRaisesRegex(PackageError, "expects actor, got library"):
            self.resolve()

    def test_library_edge_cannot_link_actor_dso(self):
        other = copy.deepcopy(self.packages["example.actor"])
        other["package"].update(id="example.other", name="other")
        other["actor"]["name"] = "other"
        other["artifact"].update(target="other_actor", export_target="example::other")
        other["dependencies"]["libraries"] = []
        self.add_package(other)
        self.packages["example.actor"]["dependencies"]["libraries"] = [self.edge("other")]
        with self.assertRaisesRegex(PackageError, "expects library, got actor"):
            self.resolve()

    def test_missing_and_colliding_targets(self):
        self.packages["example.actor"]["dependencies"]["libraries"][0]["target"] = "example::wrong"
        with self.assertRaisesRegex(PackageError, "does not match export"):
            self.resolve()
        self.packages["example.actor"]["dependencies"]["libraries"][0]["target"] = "example::mapping"
        self.packages["example.mapping"]["artifact"]["target"] = "example_actor"
        with self.assertRaisesRegex(PackageError, "target example_actor: conflict"):
            self.resolve()

    def test_diamond_shares_one_node_and_explains_all_chains(self):
        for name in ("a", "b", "leaf"):
            self.add_package(self.library(name))
        self.packages["example.actor"]["dependencies"]["libraries"] = [self.edge("a"), self.edge("b")]
        for name in ("a", "b"):
            self.packages[f"example.{name}"]["dependencies"]["libraries"] = [self.edge("leaf")]
        graph = self.resolve()
        self.assertEqual(sum(p["id"] == "example.leaf" for p in graph["packages"]), 1)
        chains = paths_to(graph["roots"], graph["edges"], "example.leaf")
        self.assertEqual(chains, [["example.actor", "example.a", "example.leaf"], ["example.actor", "example.b", "example.leaf"]])
        self.packages["example.b"]["dependencies"]["libraries"][0]["version"] = ">=2.0.0"
        with self.assertRaises(PackageError) as caught:
            self.resolve()
        error = str(caught.exception)
        for expected in ("selected 0.1.0", "example.actor -> example.a -> example.leaf", "example.actor -> example.b -> example.leaf", ">=2.0.0"):
            self.assertIn(expected, error)

    def test_transitive_cycle_and_self_cycle_rejected(self):
        self.add_package(self.library("other"))
        self.packages["example.mapping"]["dependencies"]["libraries"] = [self.edge("other")]
        self.packages["example.other"]["dependencies"]["libraries"] = [self.edge("mapping")]
        with self.assertRaisesRegex(PackageError, "example.mapping -> example.other -> example.mapping"):
            self.resolve()
        self.packages["example.mapping"]["dependencies"]["libraries"] = [self.edge("mapping")]
        with self.assertRaisesRegex(PackageError, "self dependency"):
            self.resolve()

    def test_actor_logical_cycle_rejected(self):
        other = copy.deepcopy(self.packages["example.actor"])
        other["package"].update(id="example.other", name="other")
        other["actor"]["name"] = "other"
        other["artifact"].update(target="other_actor", export_target="example::other")
        other["dependencies"]["actors"] = [{"id": "example.actor", "version": "=0.2.0"}]
        self.add_package(other)
        self.packages["example.actor"]["dependencies"]["actors"] = [{"id": "example.other", "version": "=0.2.0"}]
        with self.assertRaisesRegex(PackageError, "dependency cycle"):
            self.resolve()

    def test_test_dependencies_only_enter_tests_profile(self):
        self.packages["example.mapping"]["test_dependencies"]["libraries"] = [self.edge("testing")]
        self.resolve()
        self.workspace["workspace"]["profile"] = "tests"
        with self.assertRaisesRegex(PackageError, "missing binding.*example.testing"):
            self.resolve()
        self.add_package(self.library("testing"))
        graph = self.resolve()
        edge = next(e for e in graph["edges"] if e["to"] == "example.testing")
        self.assertEqual(edge["scope"], "test")

    def test_platform_mismatch(self):
        self.workspace["workspace"]["platform"] = "linux-arm64"
        with self.assertRaisesRegex(PackageError, "artifact.platforms"):
            self.resolve()

    def test_provider_target_and_provenance_checks(self):
        self.workspace["providers"][0]["targets"] = ["obcx::wrong"]
        with self.assertRaisesRegex(PackageError, "unauthorized system targets"):
            self.resolve()
        self.workspace["providers"][0]["targets"] = ["obcx::obcx_core"]
        (self.root / "sdk-receipt.json").write_text("tampered")
        with self.assertRaisesRegex(PackageError, "provenance.sha256: drift"):
            self.resolve()

    def test_provider_compatibility_and_version_checks(self):
        self.workspace["providers"][0]["version"] = "2.0.0"
        with self.assertRaisesRegex(PackageError, "compatibility.obcx"):
            self.resolve()
        self.workspace["providers"][0]["version"] = "1.1.0"
        self.packages["example.actor"]["dependencies"]["system"][0]["version"] = ">=1.2.0"
        with self.assertRaisesRegex(PackageError, "version conflict"):
            self.resolve()

    def test_missing_provider_reports_chain(self):
        self.workspace["providers"] = []
        with self.assertRaisesRegex(PackageError, "example.actor -> obcx-sdk"):
            self.resolve()

    def test_development_edits_update_current_source_receipts(self):
        first = self.resolve()
        (self.root / "example.mapping" / "implementation.cpp").write_text("int changed = 1;\n")
        second = self.resolve()
        self.assertEqual(first["edges"], second["edges"])
        self.assertNotEqual(first["packages"], second["packages"])

    def test_resolver_never_executes_cmake(self):
        marker = self.root / "should-not-exist"
        (self.root / "example.mapping" / "CMakeLists.txt").write_text(f'file(WRITE "{marker}" "bad")\n')
        self.resolve()
        self.assertFalse(marker.exists())

    def test_cli_resolve_check_explain_and_parallel_atomic_writers(self):
        graph_path = self.root / "resolved-packages.json"
        command = self.cli("resolve") + ["--graph", str(graph_path)]
        # At least six processes exercise the persistent flock inode/atomic rename.
        with ThreadPoolExecutor(max_workers=8) as workers:
            results = list(workers.map(lambda _: subprocess.run(command, capture_output=True, text=True), range(8)))
        for result in results:
            self.assertEqual(result.returncode, 0, result.stderr)
        graph = json.loads(graph_path.read_bytes())
        self.assertEqual(json.loads(results[0].stdout)["graph_sha256"], digest(encoded(graph)))
        before = graph_path.stat().st_mtime_ns
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, graph_path.stat().st_mtime_ns)
        result = subprocess.run(self.cli("check"), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run(self.cli("explain") + ["example.mapping"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["chains"], [["example.actor", "example.mapping"]])
        result = subprocess.run(self.cli("prepare") + ["--graph", str(graph_path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["system_requirements"][0]["id"], "obcx-sdk")

    def test_graph_output_cannot_overwrite_workspace_or_package_inputs(self):
        for output in (self.manifest, self.root / "example.mapping/package.toml", self.root / "sdk-receipt.json"):
            before = output.read_bytes()
            result = subprocess.run(self.cli("resolve") + ["--graph", str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(before, output.read_bytes())

    def test_cli_does_not_echo_credential_value(self):
        self.workspace["providers"][0]["provenance"]["path"] = "/secret/machine/path"
        self.save()
        result = subprocess.run(self.cli("check"), capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertNotIn("/secret/machine/path", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
