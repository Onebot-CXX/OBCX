from __future__ import annotations

import copy
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import ROOT, WorkspaceCase, fixture
from obcx_package import PackageError
from obcx_package.contracts import validate
from obcx_package.versions import Version, constraints, satisfies


class PackageContractTest(WorkspaceCase):
    def test_every_package_field_is_required(self):
        def remove_each(document, value, path):
            if not isinstance(value, dict):
                return
            for key in value:
                changed = copy.deepcopy(document)
                parent = changed
                for part in path:
                    parent = parent[part]
                del parent[key]
                with self.subTest(path=(*path, key)), self.assertRaises(PackageError):
                    validate(changed, "package")
                remove_each(document, value[key], (*path, key))
        for kind in ("actor", "library"):
            document = fixture(kind)
            remove_each(document, document, ())

    def test_unknown_fields_rejected_at_every_package_table(self):
        for kind in ("actor", "library"):
            document = fixture(kind)
            for table in [None, *[k for k, v in document.items() if isinstance(v, dict)]]:
                changed = copy.deepcopy(document)
                (changed if table is None else changed[table])["surprise"] = True
                with self.subTest(kind=kind, table=table), self.assertRaisesRegex(PackageError, "unknown field"):
                    validate(changed, "package")

    def test_no_mutation_or_implicit_defaults(self):
        before = copy.deepcopy(self.workspace)
        validate(self.workspace, "workspace")
        self.assertEqual(before, self.workspace)
        for field in self.workspace:
            changed = copy.deepcopy(self.workspace)
            del changed[field]
            with self.assertRaises(PackageError):
                validate(changed, "workspace")

    def test_library_cannot_supply_actor_fields(self):
        document = fixture("library")
        document["actor"] = fixture("actor")["actor"]
        with self.assertRaises(PackageError):
            validate(document, "package")

    def test_actor_must_declare_sdk_and_correct_kind(self):
        document = fixture("actor")
        document["dependencies"]["system"] = []
        with self.assertRaisesRegex(PackageError, "explicitly declare obcx-sdk"):
            validate(document, "package")
        document = fixture("actor")
        document["artifact"]["kind"] = "static-library"
        with self.assertRaises(PackageError):
            validate(document, "package")

    def test_library_actor_edge_and_test_leakage_declaration_rejected(self):
        document = fixture("library")
        document["dependencies"]["actors"] = [{"id": "example.actor", "version": "=0.2.0"}]
        with self.assertRaisesRegex(PackageError, "must not depend on actors"):
            validate(document, "package")
        document = fixture("actor")
        document["test_dependencies"]["libraries"] = copy.deepcopy(document["dependencies"]["libraries"])
        with self.assertRaisesRegex(PackageError, "duplicate"):
            validate(document, "package")

    def test_header_only_requires_interface_dependencies(self):
        document = fixture("library")
        document["artifact"]["kind"] = "header-only"
        document["dependencies"]["libraries"] = [self.edge("other")]
        with self.assertRaisesRegex(PackageError, "interface"):
            validate(document, "package")
        document["dependencies"]["libraries"][0]["visibility"] = "interface"
        validate(document, "package")

    def test_integers_are_not_booleans(self):
        document = fixture("actor")
        document["actor"]["abi"] = True
        with self.assertRaises(PackageError):
            validate(document, "package")

    def test_reserved_sdk_identity(self):
        document = fixture("library")
        document["package"]["id"] = "obcx-sdk"
        with self.assertRaisesRegex(PackageError, "reserved"):
            validate(document, "package")
        self.workspace["sources"][0]["id"] = "obcx-sdk"
        with self.assertRaisesRegex(PackageError, "reserved"):
            validate(self.workspace, "workspace")

    def test_unique_source_provider_ids_and_export_targets(self):
        self.workspace["sources"].append(copy.deepcopy(self.workspace["sources"][0]))
        with self.assertRaises(PackageError):
            validate(self.workspace, "workspace")
        self.workspace["sources"].pop()
        binding = copy.deepcopy(self.workspace["providers"][0])
        binding.update(id="fake", kind="cmake-config")
        binding["provenance"]["kind"] = "environment"
        self.workspace["providers"].append(binding)
        with self.assertRaisesRegex(PackageError, "duplicate export"):
            validate(self.workspace, "workspace")

    def test_provider_missing_or_extra_fields(self):
        binding = self.workspace["providers"][0]
        for key in list(binding):
            changed = copy.deepcopy(self.workspace)
            del changed["providers"][0][key]
            with self.subTest(key=key), self.assertRaises(PackageError):
                validate(changed, "workspace")
        binding["unknown"] = "bad"
        with self.assertRaisesRegex(PackageError, "unknown field"):
            validate(self.workspace, "workspace")

    def test_fixed_sources_reject_mutable_refs_and_escaping_subdirs(self):
        source = {"id": "example.remote", "kind": "git", "repository": "https://example.invalid/repo",
                  "commit": "a" * 40, "subdir": "."}
        self.workspace["sources"] = [source]
        validate(self.workspace, "workspace")
        for field, value in (("commit", "main"), ("commit", "a" * 12), ("subdir", "../escape"),
                             ("subdir", "/escape"), ("repository", "https://user:secret@example.invalid/repo"),
                             ("repository", "http://example.invalid/repo"), ("repository", "https://example.invalid/repo?token=secret")):
            changed = copy.deepcopy(self.workspace)
            changed["sources"][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(PackageError):
                validate(changed, "workspace")

    def test_absolute_workspace_paths_rejected(self):
        self.workspace["sources"][0]["path"] = "/home/person/package"
        with self.assertRaises(PackageError):
            validate(self.workspace, "workspace")

    def test_library_shapes_and_platform_explicit(self):
        for shape in ("header-only", "static-library", "shared-library"):
            document = fixture("library")
            document["artifact"]["kind"] = shape
            validate(document, "package")
        document["artifact"]["platforms"] = []
        with self.assertRaises(PackageError):
            validate(document, "package")

    def test_cli_modes_are_required_not_defaulted(self):
        result = subprocess.run([sys.executable, str(ROOT / "cmake/package_tool.py"), "resolve"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        for option in ("--mode", "--network", "--workspace", "--cache", "--graph"):
            self.assertIn(option, result.stderr)


class VersionTest(unittest.TestCase):
    def test_semver_precedence(self):
        versions = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                    "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "2.0.0", "10.0.0"]
        self.assertEqual(sorted(map(Version.parse, reversed(versions))), list(map(Version.parse, versions)))

    def test_build_metadata_has_no_precedence(self):
        self.assertEqual(Version.parse("1.2.3+abc"), Version.parse("1.2.3+def"))
        self.assertTrue(satisfies("1.2.3+def", "=1.2.3+abc", "semver"))

    def test_range_intersection(self):
        for version, expected in (("0.9.9", False), ("1.0.0", True), ("1.5.1", True), ("2.0.0", False)):
            self.assertEqual(satisfies(version, ">=1.0.0,<2.0.0", "semver"), expected)
        self.assertFalse(satisfies("1.0.0", ">2.0.0,<1.0.0", "semver"))
        self.assertTrue(satisfies("1.0.0", ">0.9.0,<=1.0.0", "semver"))

    def test_prerelease_requires_explicit_same_core(self):
        self.assertFalse(satisfies("1.2.0-rc.1", ">=1.0.0,<2.0.0", "semver"))
        self.assertTrue(satisfies("1.2.0-rc.1", ">=1.2.0-alpha,<2.0.0", "semver"))
        self.assertFalse(satisfies("1.3.0-alpha", ">=1.2.0-alpha,<2.0.0", "semver"))

    def test_unsupported_and_noncanonical_versions(self):
        for value in ("01.0.0", "1.01.0", "1.0", "1.0.0-01", "1.0.0-", "1.0.0+", "1.0.0\n"):
            with self.subTest(value=value), self.assertRaises(PackageError):
                Version.parse(value)
        for value in ("1.0.0", "^1.0.0", "~1.0.0", "*", ">=1.0.0 || <2.0.0", ">=1.0.0,", ""):
            with self.subTest(value=value), self.assertRaises(PackageError):
                constraints(value, "semver")

    def test_provider_version_scheme_is_explicit(self):
        self.assertTrue(satisfies("2.12.0", ">=2.9,<3", "numeric"))
        self.assertTrue(satisfies("2.0", "=2", "numeric"))
        with self.assertRaises(PackageError):
            satisfies("2.12", ">=2", "semver")
        with self.assertRaises(PackageError):
            satisfies("2", ">=1", "guess")


if __name__ == "__main__":
    unittest.main()
