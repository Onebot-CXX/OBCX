from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import WorkspaceCase, fixture, write_toml
from obcx_package import PackageError
from obcx_package.io import encoded
from obcx_package.registry import build_index, write_index


class PackageRegistryTest(WorkspaceCase):
    def setUp(self):
        super().setUp()
        self.entries = self.root / "entries"
        for kind in ("actor", "library"):
            document = fixture(kind)
            write_toml(self.entries / document["package"]["id"] / "package.toml", document)
        self.index = self.root / "index/packages.json"

    def test_kind_aware_catalog_is_deterministic_without_inventing_downloads(self):
        first = write_index(self.entries, self.index, False)
        self.assertEqual({p["metadata"]["package"]["kind"] for p in first["packages"]}, {"actor", "library"})
        self.assertEqual(first["status"], "development-metadata-only")
        self.assertNotIn("/releases/download/", encoded(first).decode())
        self.assertEqual(self.index.read_bytes(), encoded(build_index(self.entries)))
        write_index(self.entries, self.index, True)
        document = fixture("actor")
        document["publication"]["description"] += " changed"
        write_toml(self.entries / document["package"]["id"] / "package.toml", document)
        with self.assertRaisesRegex(PackageError, "stale"):
            write_index(self.entries, self.index, True)

    def test_entry_identity_and_canonical_validation_are_not_bypassed(self):
        document = fixture("library")
        path = self.entries / document["package"]["id"] / "package.toml"
        invalid = copy.deepcopy(document)
        invalid["actor"] = fixture("actor")["actor"]
        write_toml(path, invalid)
        with self.assertRaises(PackageError):
            build_index(self.entries)
        document["package"]["id"] = "wrong.directory"
        write_toml(path, document)
        with self.assertRaisesRegex(PackageError, "directory must equal"):
            build_index(self.entries)


if __name__ == "__main__":
    unittest.main()
