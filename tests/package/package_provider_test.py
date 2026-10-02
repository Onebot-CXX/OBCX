from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import ROOT, WorkspaceCase
from obcx_package import PackageError
from obcx_package.io import digest, encoded
from obcx_package.providers import verify_provider


class PackageProviderTest(WorkspaceCase):
    def setUp(self):
        super().setUp()
        self.prefix = self.root / "system"
        (self.prefix / "include").mkdir(parents=True)
        self.header = self.prefix / "include/version.hpp"
        self.header.write_text("#define FIXTURE_VERSION 20400\n")
        self.environment = {"schema_version": 2, "kind": "environment",
                            "inputs": [{"path": "system/include/version.hpp", "sha256": digest(self.header.read_bytes())}],
                            "prefixes": [{"base": "workspace", "path": "system"}]}
        self.binding = {"id": "fixture.sys", "kind": "cmake-config", "package": "Fixture", "components": [],
                        "targets": ["Fixture::lib"], "version": "2.4.0", "version_scheme": "semver",
                        "version_probe": {"kind": "cmake-variable", "name": "Fixture_VERSION"},
                        "package_manager": {"kind": "none"},
                        "provenance": {"kind": "environment", "path": "provider-env.json", "sha256": "0" * 64}}
        self.refresh_environment()
        self.observed = {"id": "fixture.sys", "version": "2.4.0", "targets": [
            {"name": "Fixture::lib", "type": "INTERFACE_LIBRARY", "locations": [],
             "include_directories": [str(self.prefix / "include")]}]}

    def refresh_environment(self):
        content = encoded(self.environment)
        (self.root / "provider-env.json").write_bytes(content)
        self.binding["provenance"]["sha256"] = digest(content)

    def verify(self):
        return verify_provider(self.binding, self.observed, self.root, self.root / "build")

    def receipt(self):
        value = {"schema_version": 2, "id": "fixture.sys", "version": "2.4.0", "version_scheme": "semver",
                 "targets": [{"name": "Fixture::lib", "files": [{"path": str(self.header), "sha256": digest(self.header.read_bytes())}]}]}
        content = encoded(value)
        (self.root / "version-receipt.json").write_bytes(content)
        self.binding["version_probe"] = {"kind": "receipt", "path": "version-receipt.json", "sha256": digest(content)}
        return value

    def configure(self, body: str):
        source = self.root / "driver"
        source.mkdir(exist_ok=True)
        build = self.root / "build"
        config = self.prefix / ("FindFixture.cmake" if self.binding["kind"] == "cmake-module" else "FixtureConfig.cmake")
        config.write_text(body)
        (source / "binding.json").write_bytes(encoded(self.binding))
        (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.30)
project(ProviderFixture VERSION 1.1.0 LANGUAGES NONE)
list(APPEND CMAKE_MODULE_PATH "{ROOT / 'cmake'}" "{self.prefix}")
list(APPEND CMAKE_PREFIX_PATH "{self.prefix}")
include(OBCXPackageProviders)
file(READ "${{CMAKE_CURRENT_SOURCE_DIR}}/binding.json" binding)
obcx_find_declared_provider("${{binding}}" "{self.root}" "${{CMAKE_BINARY_DIR}}/providers")
''')
        return subprocess.run(["cmake", "-S", str(source), "-B", str(build)], capture_output=True, text=True)

    def config_body(self, version="2.4.0"):
        return f'''set(Fixture_VERSION "{version}")
add_library(Fixture::lib INTERFACE IMPORTED)
set_target_properties(Fixture::lib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "{self.prefix / 'include'}")
'''

    def test_installed_same_version_from_wrong_prefix_is_rejected(self):
        self.observed["targets"][0]["include_directories"] = [str(self.root / "system-other/include")]
        with self.assertRaisesRegex(PackageError, "outside declared environment prefixes"):
            self.verify()

    def test_nix_anchor_cannot_authorize_whole_store(self):
        self.binding["provenance"]["kind"] = "nix"
        self.environment["kind"] = "nix"
        self.environment["prefixes"] = [{"base": "absolute", "path": "/nix/store"}]
        self.refresh_environment()
        with self.assertRaisesRegex(PackageError, "concrete store outputs"):
            self.verify()

    def test_unversioned_header_library_requires_related_pinned_evidence(self):
        self.observed["version"] = ""
        self.receipt()
        self.assertEqual(self.verify()["version"], "2.4.0")
        (self.prefix / "elsewhere").mkdir()
        self.observed["targets"][0]["include_directories"] = [str(self.prefix / "elsewhere")]
        with self.assertRaisesRegex(PackageError, "unrelated to actual target"):
            self.verify()

    def test_receipt_digest_cannot_be_ignored(self):
        self.receipt()
        (self.root / "version-receipt.json").write_text("changed")
        with self.assertRaisesRegex(PackageError, "receipt digest mismatch"):
            self.verify()

    def test_receipt_must_cover_binary_locations(self):
        self.receipt()
        artifact = self.prefix / "libfixture.a"
        artifact.write_bytes(b"fixture binary identity")
        self.observed["targets"][0].update(type="STATIC_LIBRARY", locations=[str(artifact)])
        with self.assertRaisesRegex(PackageError, "omits observed binary"):
            self.verify()

    def test_cmake_wrong_version_fails_configure(self):
        result = self.configure(self.config_body("2.5.0"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("actual version 2.5.0 differs from locked 2.4.0", " ".join(result.stderr.split()))

    def test_cmake_missing_authorized_target_fails_configure(self):
        result = self.configure('set(Fixture_VERSION "2.4.0")\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing declared provider target", result.stderr)

    def test_cmake_missing_version_is_not_assumed_compatible(self):
        self.binding["version_probe"]["name"] = "Undefined_VERSION"
        result = self.configure(self.config_body())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no reported version", result.stderr)

    def test_changed_environment_rejected_before_config_code_executes(self):
        self.header.write_text("changed")
        marker = self.root / "should-not-run"
        result = self.configure(f'file(WRITE "{marker}" "bad")\n' + self.config_body())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("environment rejected before discovery", result.stderr)
        self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
