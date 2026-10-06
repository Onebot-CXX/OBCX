"""Single source for the v2 JSON contracts and their stdlib-only validator.

JSON Schema files are generated snapshots, not separate handwritten validators.
Only the schema vocabulary used below is supported; unknown schema keywords fail
closed when developing the tooling itself. No configuration defaults are applied.
"""
from __future__ import annotations

import copy
import re
from pathlib import PurePosixPath
from urllib.parse import urlsplit

from . import PackageError, SCHEMA_VERSION, TOOL_VERSION
from .versions import Version, constraints, parse_version


def obj(fields: dict) -> dict:
    return {"type": "object", "required": list(fields), "additionalProperties": False,
            "properties": fields}


def text(**keywords) -> dict:
    return {"type": "string", "minLength": 1, **keywords}


def choice(*values) -> dict:
    return {"type": "string", "enum": list(values)}


def integer(value: int) -> dict:
    return {"type": "integer", "const": value}


def array(item: dict, minimum: int) -> dict:
    return {"type": "array", "items": item, "uniqueItems": True, "minItems": minimum}


ID = text(pattern=r"^[a-z0-9]+(?:[._-][a-z0-9]+)*$")
NAME = text(pattern=r"^[a-z][a-z0-9_-]*$")
TARGET = text(pattern=r"^[A-Za-z_][A-Za-z0-9_.+-]*$")
EXPORT = text(pattern=r"^[A-Za-z_][A-Za-z0-9_.+-]*(?:::[A-Za-z_][A-Za-z0-9_.+-]*)+$")
VERSION = text(format="semver")
RANGE = text(format="semver-range")
PATH = text(format="relative-path")
SUBDIR = text(format="subdir")
HTTPS = text(format="https")
SHA = text(pattern=r"^[0-9a-f]{64}$")
COMMIT = text(pattern=r"^[0-9a-f]{40}$")
PLATFORM = choice("linux-x86_64", "linux-arm64")
VISIBILITY = choice("private", "public", "interface")
LIBRARY_EDGE = obj({"id": ID, "version": RANGE, "target": EXPORT, "visibility": VISIBILITY})
ACTOR_EDGE = obj({"id": ID, "version": RANGE})
SYSTEM_EDGE = obj({"id": ID, "version": text(), "targets": array(EXPORT, 1), "visibility": VISIBILITY})
DEPENDENCIES = obj({"libraries": array(LIBRARY_EDGE, 0), "actors": array(ACTOR_EDGE, 0),
                    "system": array(SYSTEM_EDGE, 0)})
TEST_DEPENDENCIES = obj({"libraries": array(LIBRARY_EDGE, 0), "system": array(SYSTEM_EDGE, 0)})
PUBLICATION = obj({"repository": HTTPS, "homepage": HTTPS,
                   "license": text(pattern=r"^[A-Za-z0-9][A-Za-z0-9.+-]*(?: (?:AND|OR) [A-Za-z0-9][A-Za-z0-9.+-]*)*$"),
                   "description": text()})
TOOLCHAIN = {"cpp_standard": {"type": "integer", "enum": [20, 23, 26]},
             "compiler": obj({"id": choice("GNU"), "version": RANGE}),
             "stdlib": choice("libstdc++"), "cxx11_abi": {"type": "integer", "enum": [0, 1]},
             "pic": {"type": "boolean", "const": True}}


def package_branch(kind: str) -> dict:
    fields = {"schema_version": integer(SCHEMA_VERSION),
              "package": obj({"id": ID, "name": NAME, "version": VERSION, "kind": choice(kind)}),
              "artifact": obj({"kind": choice("shared-library") if kind == "actor" else
                               choice("static-library", "shared-library", "header-only"),
                               "name": NAME, "target": TARGET, "export_target": EXPORT,
                               "platforms": array(PLATFORM, 1)}),
              "dependencies": DEPENDENCIES, "test_dependencies": TEST_DEPENDENCIES,
              "compatibility": obj(TOOLCHAIN if kind == "library" else {
                  **TOOLCHAIN, "cpp_standard": integer(26), "obcx": RANGE,
                  "actor_abi_min": integer(2), "actor_abi_max": integer(2),
                  "reflection_macro": integer(202506)}), "publication": PUBLICATION}
    if kind == "actor":
        fields["actor"] = obj({"name": NAME, "abi": integer(2),
                               "entrypoint": choice("obcx_create_actor_v2"),
                               "input_contract_schema": integer(2)})
    return obj(fields)


GIT_PROVENANCE = obj({"kind": choice("git"), "repository": HTTPS, "commit": COMMIT, "subdir": SUBDIR})
ARCHIVE_PROVENANCE = obj({"kind": choice("archive"), "path": PATH, "sha256": SHA})
PATH_SOURCE = obj({"id": ID, "kind": choice("path"), "path": PATH,
                   "provenance": {"oneOf": [obj({"kind": choice("working-tree")}),
                                             GIT_PROVENANCE, ARCHIVE_PROVENANCE]}})
GIT_SOURCE = obj({"id": ID, "kind": choice("git"), "repository": HTTPS, "commit": COMMIT, "subdir": SUBDIR})
VERSION_PROBE = {"oneOf": [
    obj({"kind": choice("cmake-variable"), "name": text(pattern=r"^[A-Za-z_][A-Za-z0-9_-]*$")}),
    obj({"kind": choice("target-property"), "target": EXPORT, "property": text(pattern=r"^[A-Z_][A-Z0-9_]*$")}),
    obj({"kind": choice("pkg-config")}),
    obj({"kind": choice("receipt"), "path": PATH, "sha256": SHA}),
]}
PACKAGE_MANAGER = {"oneOf": [
    obj({"kind": choice("none")}),
    obj({"kind": choice("vcpkg"), "port": text(pattern=r"^[a-z0-9]+(?:-[a-z0-9]+)*$"),
         "features": array(text(pattern=r"^[a-z0-9]+(?:-[a-z0-9]+)*$"), 0),
         "default_features": {"type": "boolean"}, "baseline": COMMIT}),
]}
PROVIDER = obj({"id": ID,
                "kind": choice("cmake-config", "cmake-module", "pkg-config", "workspace-sdk", "installed-sdk"),
                "package": text(pattern=r"^[A-Za-z0-9_.+-]+$"),
                "components": array(text(pattern=r"^[A-Za-z0-9_.+-]+$"), 0),
                "targets": array(EXPORT, 1), "version": text(),
                "version_scheme": choice("semver", "numeric"),
                "version_probe": VERSION_PROBE, "package_manager": PACKAGE_MANAGER,
                "provenance": obj({"kind": choice("nix", "vcpkg", "environment", "workspace-sdk", "installed-sdk"),
                                   "path": PATH})})
PROVIDER_ENVIRONMENT = obj({"schema_version": integer(SCHEMA_VERSION),
                            "kind": choice("nix", "vcpkg", "environment", "workspace-sdk", "installed-sdk"),
                            "prefixes": array(obj({"base": choice("workspace", "build", "absolute"), "path": text()}), 1)})
PROVIDER_RECEIPT = obj({"schema_version": integer(SCHEMA_VERSION), "id": ID,
                        "version": text(), "version_scheme": choice("semver", "numeric"),
                        "targets": array(obj({"name": EXPORT, "files": array(obj({"path": text(), "sha256": SHA}), 1)}), 1)})
EDGE = obj({"from": ID, "to": ID, "kind": choice("libraries", "actors", "system"),
            "scope": choice("production", "test"), "requirement": text(),
            "targets": array(EXPORT, 0), "visibility": choice("private", "public", "interface", "logical")})
IDENTITY = obj({"kind": choice("working-tree", "git", "archive"),
                "digest": {"type": "string", "pattern": r"^(?:[0-9a-f]{40}|[0-9a-f]{64}|metadata-only)$"}})
SOURCE_RECEIPT = obj({"sha256": SHA, "dirty": {"type": "boolean"}})
RESOLVED_NODE = obj({"id": ID, "kind": choice("actor", "library"), "version": VERSION,
                     "source": {"oneOf": [PATH_SOURCE, GIT_SOURCE]}, "source_identity": IDENTITY,
                     "metadata_sha256": SHA, "source_dir": text(),
                     "metadata": {"oneOf": [package_branch("actor"), package_branch("library")]},
                     "source_receipt": SOURCE_RECEIPT})
FILE = obj({"path": SUBDIR, "owner": ID, "sha256": SHA, "role": choice(
    "artifact", "header", "cmake", "metadata", "receipt", "private-library")})
BUILD_RECEIPT = obj({"schema_version": integer(SCHEMA_VERSION), "tool_version": choice(TOOL_VERSION),
                     "package_id": ID, "graph_sha256": SHA,
                     "platform": PLATFORM, "configuration": text(),
                     "sources": array(obj({"id": ID, **SOURCE_RECEIPT["properties"]}), 1),
                     "toolchain": obj({"compiler_id": text(), "compiler_version": text(), "compiler_sha256": SHA,
                                       "stdlib": text(), "stdlib_version": text(), "cxx11_abi": {"type": "integer", "enum": [0, 1]},
                                       "cpp_standard": {"type": "integer", "enum": [20, 23, 26]},
                                       "flags_sha256": SHA}),
                     "providers": array(PROVIDER, 0), "edges": array(EDGE, 0), "files": array(FILE, 1)})


def schema(name: str) -> dict:
    bodies = {
        "package": {"oneOf": [package_branch("actor"), package_branch("library")]},
        "workspace": obj({"schema_version": integer(SCHEMA_VERSION),
                           "workspace": obj({"roots": array(ID, 0), "platform": PLATFORM,
                                             "profile": choice("production", "tests")}),
                           "sources": array({"oneOf": [PATH_SOURCE, GIT_SOURCE]}, 0),
                           "providers": array(PROVIDER, 0)}),
        "resolved-packages": obj({"schema_version": integer(SCHEMA_VERSION), "tool_version": choice(TOOL_VERSION),
                                   "workspace_sha256": SHA, "platform": PLATFORM, "profile": choice("production", "tests"),
                                   "mode": choice("development", "release"), "roots": array(ID, 0),
                                   "packages": array(RESOLVED_NODE, 0), "edges": array(EDGE, 0),
                                   "providers": array(PROVIDER, 0), "order": array(ID, 0), "unused_sources": array(ID, 0)}),
        "package-build-receipt": BUILD_RECEIPT,
        "provider-receipt": PROVIDER_RECEIPT,
        "provider-environment": PROVIDER_ENVIRONMENT,
    }
    if name not in bodies:
        raise PackageError("unknown schema name")
    return {"$schema": "https://json-schema.org/draft/2020-12/schema",
            "$id": f"https://onebot-cxx.github.io/obcx/schemas/{name}.schema.json",
            "title": f"OBCX v2 {name}", **copy.deepcopy(bodies[name])}


def check_format(value: str, name: str) -> None:
    if name == "semver":
        Version.parse(value)
    elif name == "semver-range":
        constraints(value, "semver")
    elif name == "https":
        try:
            url = urlsplit(value)
            if (url.scheme != "https" or not url.hostname or url.username or url.password or
                    url.query or url.fragment or any(c.isspace() for c in value)):
                raise ValueError
            _ = url.port
        except ValueError:
            raise PackageError("requires HTTPS without embedded credentials, query or fragment") from None
    elif name in {"relative-path", "subdir"}:
        path = PurePosixPath(value)
        if (path.is_absolute() or "\\" in value or ":" in value or
                any(ord(c) < 32 or c == ";" for c in value) or
                (name == "subdir" and ".." in path.parts)):
            raise PackageError("requires a relative POSIX path (subdir may not escape its root)")
    else:
        raise RuntimeError(f"unimplemented schema format: {name}")


def violations(value, contract: dict, path: str) -> list[str]:
    supported = {"$schema", "$id", "title", "oneOf", "type", "required", "properties",
                 "additionalProperties", "enum", "const", "minLength", "pattern", "format",
                 "items", "minItems", "uniqueItems"}
    if set(contract) - supported:
        raise RuntimeError("unimplemented contract vocabulary")
    if "oneOf" in contract:
        results = [violations(value, branch, path) for branch in contract["oneOf"]]
        valid = sum(not errors for errors in results)
        if valid == 1:
            return []
        if valid > 1:
            return [f"{path}: ambiguous contract variant"]
        return min(results, key=len)
    expected = contract.get("type")
    types = {"object": dict, "array": list, "string": str, "integer": int, "boolean": bool}
    if expected and type(value) is not types[expected]:
        return [f"{path}: expected {expected}"]
    errors = []
    if "enum" in contract and value not in contract["enum"]:
        errors.append(f"{path}: expected one of {contract['enum']}")
    if "const" in contract and value != contract["const"]:
        errors.append(f"{path}: must equal {contract['const']}")
    if expected == "object":
        for key in sorted(set(contract["required"]) - value.keys()):
            errors.append(f"{path}.{key}: required explicitly")
        for key in sorted(value):
            if key not in contract["properties"]:
                errors.append(f"{path}.{key}: unknown field")
            else:
                errors.extend(violations(value[key], contract["properties"][key], f"{path}.{key}"))
    elif expected == "array":
        if len(value) < contract["minItems"]:
            errors.append(f"{path}: requires at least {contract['minItems']} entries")
        for index, item in enumerate(value):
            if contract["uniqueItems"] and item in value[:index]:
                errors.append(f"{path}[{index}]: duplicate entry")
            errors.extend(violations(item, contract["items"], f"{path}[{index}]"))
    elif expected == "string":
        if not value.strip() or value != value.strip():
            errors.append(f"{path}: requires a non-empty unpadded string")
        if "pattern" in contract and not re.fullmatch(contract["pattern"], value):
            errors.append(f"{path}: invalid identifier or value syntax")
        if "format" in contract:
            try:
                check_format(value, contract["format"])
            except PackageError as error:
                errors.append(f"{path}: {error}")
    return errors


def validate_provider_binding(entry: dict, path: str) -> None:
    errors = violations(entry, PROVIDER, path)
    if errors:
        raise PackageError("\n".join(errors))
    sdk = entry["kind"] in {"workspace-sdk", "installed-sdk"}
    if sdk != (entry["id"] == "obcx-sdk"):
        raise PackageError(f"{path}.kind: SDK kinds must bind exactly obcx-sdk")
    if sdk and (entry["version_scheme"] != "semver" or entry["provenance"]["kind"] != entry["kind"]):
        raise PackageError(f"{path}: SDK requires SemVer and matching provenance kind")
    if not sdk and entry["provenance"]["kind"] in {"workspace-sdk", "installed-sdk"}:
        raise PackageError(f"{path}.provenance: SDK provenance is reserved for obcx-sdk")
    probe = entry["version_probe"]
    if probe["kind"] == "pkg-config" and entry["kind"] != "pkg-config":
        raise PackageError(f"{path}.version_probe: pkg-config probe requires pkg-config provider")
    if entry["kind"] == "pkg-config" and (entry["components"] or len(entry["targets"]) != 1 or
            not re.fullmatch(r"PkgConfig::[A-Za-z_][A-Za-z0-9_]*", entry["targets"][0])):
        raise PackageError(f"{path}: pkg-config needs one explicit PkgConfig::<prefix> target and no components")
    if probe["kind"] == "target-property" and probe["target"] not in entry["targets"]:
        raise PackageError(f"{path}.version_probe: property target must be explicitly authorized")
    manager = entry["package_manager"]
    if manager["kind"] == "vcpkg" and entry["provenance"]["kind"] != "vcpkg":
        raise PackageError(f"{path}.package_manager: vcpkg requires vcpkg provenance")
    parse_version(entry["version"], entry["version_scheme"])


def validate(document: dict, name: str) -> None:
    errors = violations(document, schema(name), name)
    if errors:
        raise PackageError("\n".join(errors))
    if name == "package":
        package = document["package"]
        if package["id"] == "obcx-sdk":
            raise PackageError("package.id: obcx-sdk is reserved for a system provider")
        if package["kind"] == "actor" and document["actor"]["name"] != package["name"]:
            raise PackageError("actor.name: must match package.name")
        if package["kind"] == "library" and document["dependencies"]["actors"]:
            raise PackageError("dependencies.actors: a library must not depend on actors")
        all_ids = set()
        for section in ("dependencies", "test_dependencies"):
            for kind, entries in document[section].items():
                for edge in entries:
                    if edge["id"] in all_ids or edge["id"] == package["id"]:
                        raise PackageError(f"{section}.{kind}: duplicate or self dependency {edge['id']}")
                    all_ids.add(edge["id"])
                    if edge["id"] == "obcx-sdk" and kind != "system":
                        raise PackageError(f"{section}.{kind}: obcx-sdk must be a system dependency")
                    if (document["artifact"]["kind"] == "header-only" and section == "dependencies"
                            and edge.get("visibility") != "interface"):
                        raise PackageError("dependencies: header-only usage must be interface")
        if package["kind"] == "actor":
            sdk = next((e for e in document["dependencies"]["system"] if e["id"] == "obcx-sdk"), None)
            if sdk is None:
                raise PackageError("dependencies.system: actor must explicitly declare obcx-sdk")
    elif name == "workspace":
        seen = set()
        targets = set()
        for section in ("sources", "providers"):
            for index, entry in enumerate(document[section]):
                path = f"{section}[{index}]"
                if entry["id"] in seen:
                    raise PackageError(f"{path}.id: duplicate source/provider ID {entry['id']}")
                seen.add(entry["id"])
                if section == "sources" and entry["id"] == "obcx-sdk":
                    raise PackageError(f"{path}.id: obcx-sdk is reserved for a provider")
                if section == "providers":
                    validate_provider_binding(entry, path)
                    for target in entry["targets"]:
                        if target in targets:
                            raise PackageError(f"{path}.targets: duplicate export target {target}")
                        targets.add(target)
