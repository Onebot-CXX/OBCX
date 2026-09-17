#!/usr/bin/env python3
"""Validate and inspect the canonical OBCX actor.toml contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import tomllib
from typing import Any
from urllib.parse import urlparse


SCHEMA_VERSION = 1
SUPPORTED_ABI = 2
REQUIRED_SECTIONS = {
    "actor",
    "artifact",
    "dependencies",
    "compatibility",
    "publication",
}
SECTION_FIELDS = {
    "actor": {"id", "name", "version", "abi"},
    "artifact": {"name", "target", "kind", "entrypoint", "platforms"},
    "dependencies": {"packages", "actors"},
    "compatibility": {
        "obcx",
        "actor_abi_min",
        "actor_abi_max",
        "cpp_standard",
        "compiler",
        "reflection_macro",
        "input_contract_schema",
    },
    "publication": {
        "repository",
        "homepage",
        "license",
        "description",
    },
}
SEMVER_PATTERN = (
    r"(?:0|[1-9]\d*)\."
    r"(?:0|[1-9]\d*)\."
    r"(?:0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
)
SEMVER_RE = re.compile(rf"^{SEMVER_PATTERN}$")
VERSION_RANGE_RE = re.compile(
    rf"^\s*(?:(?:>=|<=|>|<|=|\^|~)?\s*{SEMVER_PATTERN})"
    rf"(?:\s*,\s*(?:>=|<=|>|<|=|\^|~)?\s*{SEMVER_PATTERN})*\s*$"
)
ACTOR_ID_RE = re.compile(r"^[a-z0-9]+(?:[._-][a-z0-9]+)*$")
NAME_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
TARGET_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.+-]*$")
PACKAGE_RE = re.compile(r"^[a-z0-9][a-z0-9._+-]*$")
LICENSE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.+-]*(?:\s+(?:AND|OR)\s+[A-Za-z0-9][A-Za-z0-9.+-]*)*$")
SUPPORTED_ARTIFACT_PLATFORMS = {
    "linux-x86_64",
    "linux-arm64",
}


class ActorMetadataError(ValueError):
    """Raised when actor metadata cannot be loaded or is invalid."""

    def __init__(self, path: Path, errors: list[str]):
        self.path = path
        self.errors = errors
        super().__init__("\n".join(f"{path}: {error}" for error in errors))


def load_metadata(path: Path | str) -> dict[str, Any]:
    metadata_path = Path(path)
    try:
        with metadata_path.open("rb") as stream:
            document = tomllib.load(stream)
    except FileNotFoundError as error:
        raise ActorMetadataError(
            metadata_path, ["actor.toml does not exist"]
        ) from error
    except tomllib.TOMLDecodeError as error:
        raise ActorMetadataError(
            metadata_path, [f"invalid TOML: {error}"]
        ) from error
    if not isinstance(document, dict):
        raise ActorMetadataError(
            metadata_path, ["metadata root must be a table"]
        )
    return document


def _table(
    document: dict[str, Any], name: str, errors: list[str]
) -> dict[str, Any]:
    value = document.get(name)
    if not isinstance(value, dict):
        errors.append(f"[{name}] table is required")
        return {}
    return value


def _required_string(
    table: dict[str, Any], section: str, field: str, errors: list[str]
) -> str:
    value = table.get(field)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"[{section}].{field} must be a non-empty string")
        return ""
    return value.strip()


def _required_integer(
    table: dict[str, Any], section: str, field: str, errors: list[str]
) -> int | None:
    value = table.get(field)
    if isinstance(value, bool) or not isinstance(value, int):
        errors.append(f"[{section}].{field} must be an integer")
        return None
    return value


def _string_array(
    table: dict[str, Any], section: str, field: str, errors: list[str]
) -> list[str]:
    value = table.get(field)
    if not isinstance(value, list):
        errors.append(f"[{section}].{field} must be an array")
        return []
    if any(not isinstance(item, str) or not item.strip() for item in value):
        errors.append(
            f"[{section}].{field} must contain only non-empty strings"
        )
        return []
    normalized = [item.strip() for item in value]
    if len(normalized) != len(set(normalized)):
        errors.append(f"[{section}].{field} must not contain duplicates")
    return normalized


def _valid_url(value: str) -> bool:
    parsed = urlparse(value)
    return parsed.scheme == "https" and bool(parsed.netloc)


def validate_metadata(document: dict[str, Any]) -> list[str]:
    """Return deterministic, field-specific validation errors."""

    errors: list[str] = []
    allowed_top_level = REQUIRED_SECTIONS | {"schema_version"}
    for field in sorted(set(document) - allowed_top_level):
        errors.append(f"unexpected top-level field [{field}]")

    schema_version = document.get("schema_version")
    if isinstance(schema_version, bool) or not isinstance(schema_version, int):
        errors.append("schema_version must be an integer")
    elif schema_version != SCHEMA_VERSION:
        errors.append(
            f"schema_version must equal {SCHEMA_VERSION}, got {schema_version}"
        )

    actor = _table(document, "actor", errors)
    artifact = _table(document, "artifact", errors)
    dependencies = _table(document, "dependencies", errors)
    compatibility = _table(document, "compatibility", errors)
    publication = _table(document, "publication", errors)

    sections = {
        "actor": actor,
        "artifact": artifact,
        "dependencies": dependencies,
        "compatibility": compatibility,
        "publication": publication,
    }
    for section, table in sections.items():
        for field in sorted(set(table) - SECTION_FIELDS[section]):
            errors.append(f"[{section}].{field} is not supported")

    actor_id = _required_string(actor, "actor", "id", errors)
    if actor_id and not ACTOR_ID_RE.fullmatch(actor_id):
        errors.append(
            "[actor].id must use lowercase dot/dash/underscore-separated identifiers"
        )
    name = _required_string(actor, "actor", "name", errors)
    if name and not NAME_RE.fullmatch(name):
        errors.append(
            "[actor].name must start with a lowercase letter and contain only lowercase letters, digits, '_' or '-'"
        )
    version = _required_string(actor, "actor", "version", errors)
    if version and not SEMVER_RE.fullmatch(version):
        errors.append("[actor].version must be a semantic version")
    abi = _required_integer(actor, "actor", "abi", errors)
    if abi is not None and abi != SUPPORTED_ABI:
        errors.append(
            f"[actor].abi must equal the supported ABI {SUPPORTED_ABI}, got {abi}"
        )

    artifact_name = _required_string(
        artifact, "artifact", "name", errors
    )
    if artifact_name and not NAME_RE.fullmatch(artifact_name):
        errors.append("[artifact].name must be a canonical lowercase name")
    target = _required_string(artifact, "artifact", "target", errors)
    if target and not TARGET_RE.fullmatch(target):
        errors.append("[artifact].target is not a valid CMake target name")
    kind = _required_string(artifact, "artifact", "kind", errors)
    if kind and kind != "shared-library":
        errors.append("[artifact].kind must equal 'shared-library'")
    entrypoint = _required_string(
        artifact, "artifact", "entrypoint", errors
    )
    if entrypoint and entrypoint != "obcx_create_actor_v2":
        errors.append(
            "[artifact].entrypoint must equal 'obcx_create_actor_v2'"
        )
    platforms = _string_array(artifact, "artifact", "platforms", errors)
    if not platforms:
        errors.append("[artifact].platforms must declare at least one platform")
    for artifact_platform in platforms:
        if artifact_platform not in SUPPORTED_ARTIFACT_PLATFORMS:
            errors.append(
                "[artifact].platforms contains unsupported platform "
                f"{artifact_platform!r}"
            )

    packages = _string_array(
        dependencies, "dependencies", "packages", errors
    )
    for package in packages:
        if not PACKAGE_RE.fullmatch(package):
            errors.append(
                f"[dependencies].packages contains invalid package {package!r}"
            )

    actor_dependencies = dependencies.get("actors")
    if not isinstance(actor_dependencies, list):
        errors.append("[dependencies].actors must be an array")
        actor_dependencies = []
    dependency_ids: list[str] = []
    for index, dependency in enumerate(actor_dependencies):
        prefix = f"[dependencies].actors[{index}]"
        if not isinstance(dependency, dict):
            errors.append(f"{prefix} must be a table")
            continue
        unexpected = set(dependency) - {"id", "version"}
        for field in sorted(unexpected):
            errors.append(f"{prefix}.{field} is not supported")
        dependency_id = dependency.get("id")
        requirement = dependency.get("version")
        if not isinstance(dependency_id, str) or not ACTOR_ID_RE.fullmatch(
            dependency_id
        ):
            errors.append(f"{prefix}.id must be a canonical actor id")
        else:
            dependency_ids.append(dependency_id)
        if not isinstance(requirement, str) or not VERSION_RANGE_RE.fullmatch(
            requirement
        ):
            errors.append(f"{prefix}.version must be a semantic-version range")
    if len(dependency_ids) != len(set(dependency_ids)):
        errors.append("[dependencies].actors must not contain duplicate ids")
    if actor_id and actor_id in dependency_ids:
        errors.append("[dependencies].actors must not depend on the actor itself")

    obcx_range = _required_string(
        compatibility, "compatibility", "obcx", errors
    )
    if obcx_range and not VERSION_RANGE_RE.fullmatch(obcx_range):
        errors.append("[compatibility].obcx must be a semantic-version range")
    abi_min = _required_integer(
        compatibility, "compatibility", "actor_abi_min", errors
    )
    abi_max = _required_integer(
        compatibility, "compatibility", "actor_abi_max", errors
    )
    if abi_min is not None and abi_max is not None:
        if abi_min > abi_max:
            errors.append(
                "[compatibility].actor_abi_min must not exceed actor_abi_max"
            )
        if abi is not None and not abi_min <= abi <= abi_max:
            errors.append(
                "[compatibility] ABI range must include [actor].abi"
            )
        if abi_min != SUPPORTED_ABI or abi_max != SUPPORTED_ABI:
            errors.append(
                f"[compatibility] ABI range must be {SUPPORTED_ABI}..{SUPPORTED_ABI}"
            )
    cpp_standard = _required_integer(
        compatibility, "compatibility", "cpp_standard", errors
    )
    if cpp_standard is not None and cpp_standard != 26:
        errors.append("[compatibility].cpp_standard must equal 26")
    compiler = _required_string(
        compatibility, "compatibility", "compiler", errors
    )
    if compiler and compiler != "gcc>=16.1":
        errors.append("[compatibility].compiler must equal 'gcc>=16.1'")
    reflection_macro = _required_integer(
        compatibility, "compatibility", "reflection_macro", errors
    )
    if reflection_macro is not None and reflection_macro != 202506:
        errors.append(
            "[compatibility].reflection_macro must equal 202506"
        )
    input_contract_schema = _required_integer(
        compatibility, "compatibility", "input_contract_schema", errors
    )
    if input_contract_schema is not None and input_contract_schema != 2:
        errors.append(
            "[compatibility].input_contract_schema must equal 2"
        )

    repository = _required_string(
        publication, "publication", "repository", errors
    )
    if repository and not _valid_url(repository):
        errors.append("[publication].repository must be an https URL")
    license_name = _required_string(
        publication, "publication", "license", errors
    )
    if license_name and not LICENSE_RE.fullmatch(license_name):
        errors.append("[publication].license must be an SPDX-style expression")
    _required_string(publication, "publication", "description", errors)
    homepage = publication.get("homepage")
    if homepage is not None and (
        not isinstance(homepage, str) or not _valid_url(homepage)
    ):
        errors.append("[publication].homepage must be an https URL")

    return errors


def require_metadata(path: Path | str) -> dict[str, Any]:
    metadata_path = Path(path)
    document = load_metadata(metadata_path)
    errors = validate_metadata(document)
    if errors:
        raise ActorMetadataError(metadata_path, errors)
    return document


def cmake_record(document: dict[str, Any]) -> str:
    actor = document["actor"]
    artifact = document["artifact"]
    return "|".join(
        (
            actor["id"],
            actor["name"],
            actor["version"],
            str(actor["abi"]),
            artifact["name"],
            artifact["target"],
        )
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("metadata", type=Path)
    validate_parser.add_argument("--json", action="store_true")

    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("metadata", type=Path)
    inspect_parser.add_argument(
        "--format", choices=("json", "cmake"), default="json"
    )

    dependencies_parser = subparsers.add_parser("dependencies")
    dependencies_parser.add_argument("metadata", type=Path)

    args = parser.parse_args(argv)
    try:
        document = require_metadata(args.metadata)
    except ActorMetadataError as error:
        if getattr(args, "json", False):
            print(
                json.dumps(
                    {
                        "valid": False,
                        "path": str(error.path),
                        "errors": error.errors,
                    },
                    indent=2,
                )
            )
        else:
            print(error, file=sys.stderr)
        return 1

    if args.command == "validate":
        if args.json:
            print(
                json.dumps(
                    {"valid": True, "path": str(args.metadata), "errors": []},
                    indent=2,
                )
            )
        else:
            print(f"valid actor metadata: {args.metadata}")
    elif args.command == "inspect":
        if args.format == "cmake":
            print(cmake_record(document))
        else:
            print(json.dumps(document, indent=2, sort_keys=True))
    elif args.command == "dependencies":
        for package in document["dependencies"]["packages"]:
            print(package)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
