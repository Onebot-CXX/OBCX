from __future__ import annotations

import importlib.util
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "cmake" / "gen_vcpkg_manifest.py"
sys.path.insert(0, str(ROOT / "cmake"))
SPEC = importlib.util.spec_from_file_location("gen_vcpkg_manifest", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
generator = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generator
SPEC.loader.exec_module(generator)


ACTOR_TOML = """\
schema_version = 1

[actor]
id = "onebot-cxx.example"
name = "example"
version = "1.0.0"
abi = 2

[artifact]
name = "example"
target = "example_actor"
kind = "shared-library"
entrypoint = "obcx_create_actor_v2"
platforms = ["linux-x86_64"]

[dependencies]
packages = ["sqlite3", "tomlplusplus"]
actors = []

[compatibility]
obcx = ">=1.1.0,<2.0.0"
actor_abi_min = 2
actor_abi_max = 2
cpp_standard = 26
compiler = "gcc>=16.1"
reflection_macro = 202506
input_contract_schema = 2

[publication]
repository = "https://github.com/Onebot-CXX/example-actor"
license = "MIT"
description = "Example actor"
"""


class ActorVcpkgManifestTest(unittest.TestCase):
    def test_collects_only_dependencies_from_canonical_actor_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            actor = root / "local_actor" / "example"
            actor.mkdir(parents=True)
            (actor / "actor.toml").write_text(ACTOR_TOML, encoding="utf-8")
            manifest = root / "actors.toml"
            manifest.write_text(
                """\
schema_version = 1
[[actors]]
path = "local_actor/example"
""",
                encoding="utf-8",
            )
            dependencies, per_actor = generator.collect_actor_dependencies(
                manifest, root
            )
        self.assertEqual(dependencies, ["sqlite3", "tomlplusplus"])
        self.assertEqual(
            per_actor,
            {"onebot-cxx.example": ["sqlite3", "tomlplusplus"]},
        )

    def test_invalid_metadata_fails_instead_of_falling_back(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            actor = root / "actor"
            actor.mkdir()
            (actor / "unrelated.toml").write_text(
                "[package]\nname='unsupported'\n", encoding="utf-8"
            )
            manifest = root / "actors.toml"
            manifest.write_text(
                "schema_version=1\n[[actors]]\npath='actor'\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "actor.toml does not exist"):
                generator.collect_actor_dependencies(manifest, root)

    def test_merge_is_deterministic_and_preserves_feature_dependencies(self) -> None:
        base = ["zlib", {"name": "boost", "features": ["asio"]}]
        merged = generator.merge_dependencies(base, ["sqlite3", "zlib"])
        self.assertEqual(
            [generator._dependency_key(item) for item in merged],
            sorted(generator._dependency_key(item) for item in merged),
        )
        self.assertEqual(len(merged), 3)

    def test_remote_actor_uses_configured_binary_directory(self) -> None:
        repository = "https://example.invalid/actor.git"
        revision = "0123456789abcdef0123456789abcdef01234567"
        key = hashlib.md5(
            f"{repository}@{revision}".encode("utf-8"), usedforsecurity=False
        ).hexdigest()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary_dir = root / "build" / "actor-dev"
            actor = binary_dir / "_actors" / key
            actor.mkdir(parents=True)
            (actor / "actor.toml").write_text(ACTOR_TOML, encoding="utf-8")
            manifest = root / "actors.toml"
            manifest.write_text(
                f'''\
schema_version = 1
[[actors]]
repository = "{repository}"
revision = "{revision}"
''',
                encoding="utf-8",
            )
            dependencies, per_actor = generator.collect_actor_dependencies(
                manifest, root, binary_dir
            )
        self.assertEqual(dependencies, ["sqlite3", "tomlplusplus"])
        self.assertIn("onebot-cxx.example", per_actor)


if __name__ == "__main__":
    unittest.main()
