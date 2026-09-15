from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "cmake" / "actor_metadata.py"
SPEC = importlib.util.spec_from_file_location("actor_metadata", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import {MODULE_PATH}")
metadata = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = metadata
SPEC.loader.exec_module(metadata)


def valid_document() -> dict[str, object]:
    return {
        "schema_version": 1,
        "actor": {
            "id": "onebot-cxx.example",
            "name": "example",
            "version": "1.2.3-rc.1+build.7",
            "abi": 2,
        },
        "artifact": {
            "name": "example",
            "target": "example_actor",
            "kind": "shared-library",
            "entrypoint": "obcx_create_actor_v2",
            "platforms": ["linux-x86_64"],
        },
        "dependencies": {
            "packages": ["nlohmann-json", "sqlite3"],
            "actors": [
                {"id": "onebot-cxx.message-store", "version": ">=1.0.0,<2.0.0"}
            ],
        },
        "compatibility": {
            "obcx": ">=1.1.0,<2.0.0",
            "actor_abi_min": 2,
            "actor_abi_max": 2,
            "cpp_standard": 26,
            "compiler": "gcc>=16.1",
            "reflection_macro": 202506,
            "input_contract_schema": 2,
        },
        "publication": {
            "repository": "https://github.com/Onebot-CXX/example-actor",
            "homepage": "https://github.com/Onebot-CXX/example-actor",
            "license": "MIT",
            "description": "Example native OBCX actor",
        },
    }


class ActorMetadataValidationTest(unittest.TestCase):
    def test_accepts_complete_canonical_contract(self) -> None:
        self.assertEqual(metadata.validate_metadata(valid_document()), [])

    def test_reports_every_missing_contract_section(self) -> None:
        errors = metadata.validate_metadata({"schema_version": 1})
        self.assertIn("[actor] table is required", errors)
        self.assertIn("[artifact] table is required", errors)
        self.assertIn("[dependencies] table is required", errors)
        self.assertIn("[compatibility] table is required", errors)
        self.assertIn("[publication] table is required", errors)
        self.assertIn("[actor].id must be a non-empty string", errors)
        self.assertIn("[actor].abi must be an integer", errors)

    def test_rejects_unknown_top_level_metadata_without_special_rules(self) -> None:
        errors = metadata.validate_metadata(
            {
                "schema_version": 1,
                "extension": {"name": "unsupported", "version": "1.0.0"},
            }
        )
        self.assertIn("unexpected top-level field [extension]", errors)

    def test_rejects_unknown_fields_inside_contract_tables(self) -> None:
        document = valid_document()
        document["publication"]["download"] = "https://example.test"  # type: ignore[index]
        errors = metadata.validate_metadata(document)
        self.assertIn("[publication].download is not supported", errors)

    def test_rejects_unsupported_abi_and_entrypoint(self) -> None:
        document = valid_document()
        document["actor"]["abi"] = 3  # type: ignore[index]
        document["artifact"]["entrypoint"] = "unsupported_entrypoint"  # type: ignore[index]
        errors = metadata.validate_metadata(document)
        self.assertIn("[actor].abi must equal the supported ABI 2, got 3", errors)
        self.assertIn(
            "[artifact].entrypoint must equal 'obcx_create_actor_v2'", errors
        )

    def test_rejects_missing_or_unknown_artifact_platforms(self) -> None:
        document = valid_document()
        document["artifact"]["platforms"] = []  # type: ignore[index]
        errors = metadata.validate_metadata(document)
        self.assertIn(
            "[artifact].platforms must declare at least one platform", errors
        )

        document["artifact"]["platforms"] = ["linux"]  # type: ignore[index]
        errors = metadata.validate_metadata(document)
        self.assertIn(
            "[artifact].platforms contains unsupported platform 'linux'", errors
        )

    def test_rejects_duplicate_and_self_dependencies(self) -> None:
        document = valid_document()
        document["dependencies"] = {
            "packages": ["sqlite3", "sqlite3"],
            "actors": [
                {"id": "onebot-cxx.example", "version": "=1.2.3"},
                {"id": "onebot-cxx.example", "version": ">=1.0.0"},
            ],
        }
        errors = metadata.validate_metadata(document)
        self.assertIn("[dependencies].packages must not contain duplicates", errors)
        self.assertIn("[dependencies].actors must not contain duplicate ids", errors)
        self.assertIn(
            "[dependencies].actors must not depend on the actor itself", errors
        )

    def test_rejects_unknown_input_contract_schema(self) -> None:
        document = valid_document()
        document["compatibility"]["input_contract_schema"] = 999
        self.assertIn(
            "[compatibility].input_contract_schema must equal 2",
            metadata.validate_metadata(document),
        )

    def test_rejects_missing_publication_field_by_name(self) -> None:
        document = valid_document()
        del document["publication"]["repository"]  # type: ignore[index]
        errors = metadata.validate_metadata(document)
        self.assertIn(
            "[publication].repository must be a non-empty string", errors
        )


class ActorMetadataCliTest(unittest.TestCase):
    def write_document(self, directory: Path) -> Path:
        path = directory / "actor.toml"
        path.write_text(
            """\
schema_version = 1

[actor]
id = "onebot-cxx.example"
name = "example"
version = "1.2.3"
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
""",
            encoding="utf-8",
        )
        return path

    def test_cmake_inspection_is_stable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_document(Path(temporary))
            result = subprocess.run(
                [
                    sys.executable,
                    str(MODULE_PATH),
                    "inspect",
                    str(path),
                    "--format",
                    "cmake",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
        self.assertEqual(
            result.stdout.strip(),
            "onebot-cxx.example|example|1.2.3|2|example|example_actor",
        )

    def test_dependency_output_uses_canonical_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_document(Path(temporary))
            result = subprocess.run(
                [sys.executable, str(MODULE_PATH), "dependencies", str(path)],
                check=True,
                capture_output=True,
                text=True,
            )
        self.assertEqual(result.stdout.splitlines(), ["sqlite3", "tomlplusplus"])


if __name__ == "__main__":
    unittest.main()
