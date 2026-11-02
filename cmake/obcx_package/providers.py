"""Check discovered provider targets and consumer ranges, not installation locks."""
from __future__ import annotations

import re

from . import PackageError
from .versions import parse_version, satisfies


def verify_provider(binding: dict, observation: dict, graph: dict) -> dict:
    provider_id = binding["id"]
    if not isinstance(observation, dict) or set(observation) != {"id", "version", "targets"} or observation["id"] != provider_id:
        raise PackageError(f"provider {provider_id}: invalid observation identity/fields")
    if not isinstance(observation["version"], str) or not isinstance(observation["targets"], list):
        raise PackageError(f"provider {provider_id}: invalid observation types")
    observed = {}
    for target in observation["targets"]:
        if not isinstance(target, dict) or set(target) != {"name", "type", "locations", "include_directories"}:
            raise PackageError(f"provider {provider_id}: invalid target observation")
        if not isinstance(target["name"], str) or target["name"] in observed:
            raise PackageError(f"provider {provider_id}: duplicate/invalid target observation")
        if not isinstance(target["type"], str) or target["type"] not in {"INTERFACE_LIBRARY", "STATIC_LIBRARY", "SHARED_LIBRARY", "UNKNOWN_LIBRARY"}:
            raise PackageError(f"provider {provider_id}: unsupported provider target type")
        for field in ("locations", "include_directories"):
            if not isinstance(target[field], list) or any(not isinstance(p, str) or not p for p in target[field]):
                raise PackageError(f"provider {provider_id}: invalid observed {field}")
        observed[target["name"]] = target
    if set(observed) != set(binding["targets"]):
        raise PackageError(f"provider {provider_id}: actual targets differ from authorized targets")

    if binding not in graph["providers"]:
        raise PackageError(f"provider {provider_id}: binding differs from the selected graph")
    version = observation["version"]
    if not version:
        raise PackageError(f"provider {provider_id}: discovery did not report a version")
    parse_version(version, binding["version_scheme"])
    nodes = {node["id"]: node for node in graph["packages"]}
    for edge in graph["edges"]:
        if edge["kind"] != "system" or edge["to"] != provider_id:
            continue
        if not set(edge["targets"]) <= set(observed):
            raise PackageError(f"{edge['from']} -> {provider_id}: missing required targets")
        if not satisfies(version, edge["requirement"], binding["version_scheme"]):
            raise PackageError(f"{edge['from']} -> {provider_id}: actual version {version} "
                               f"does not satisfy {edge['requirement']} ({edge['scope']})")
        owner = nodes[edge["from"]]
        if provider_id == "obcx-sdk" and owner["kind"] == "actor":
            requirement = owner["metadata"]["compatibility"]["obcx"]
            if not satisfies(version, requirement, "semver"):
                raise PackageError(f"{owner['id']}: compatibility.obcx {requirement} excludes SDK {version}")
    return {"id": provider_id, "version": version, "version_scheme": binding["version_scheme"],
            "targets": observation["targets"]}


def merge_vcpkg_dependencies(base: list, additions: list[dict]) -> list[dict]:
    """Merge by port identity, retaining features and explicit feature policy."""
    result = {}
    for value in [*base, *additions]:
        # A string is the existing vcpkg syntax for enabling the port's default
        # features, not an OBCX configuration fallback.
        if not isinstance(value, (str, dict)):
            raise PackageError("vcpkg dependency must be a port name or dependency object")
        item = {"name": value, "features": [], "default-features": True} if isinstance(value, str) else dict(value)
        if set(item) - {"name", "features", "default-features", "version>=", "host", "platform"}:
            raise PackageError("vcpkg base uses unsupported dependency fields")
        if not isinstance(item.get("name"), str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", item["name"]):
            raise PackageError("vcpkg dependency needs an explicit canonical port name")
        if "features" in item and (not isinstance(item["features"], list) or
                any(not isinstance(feature, str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", feature)
                    for feature in item["features"])):
            raise PackageError("vcpkg features must be a list of feature names")
        for field in ("default-features", "host"):
            if field in item and not isinstance(item[field], bool):
                raise PackageError(f"vcpkg {field} must be a boolean")
        for field in ("version>=", "platform"):
            if field in item and (not isinstance(item[field], str) or not item[field].strip()):
                raise PackageError(f"vcpkg {field} must be a non-empty string")
        key = (item["name"], item.get("host", False), item.get("platform", ""))
        if key not in result:
            result[key] = item
            continue
        old = result[key]
        if old.get("default-features", True) != item.get("default-features", True):
            raise PackageError(f"vcpkg port {item['name']}: conflicting default-features")
        if "version>=" in item:
            if "version>=" in old and old["version>="] != item["version>="]:
                raise PackageError(f"vcpkg port {item['name']}: conflicting version>=")
            old["version>="] = item["version>="]
        old["features"] = sorted(set(old.get("features", [])) | set(item.get("features", [])))
    return sorted(result.values(), key=lambda item: (item["name"], str(item.get("host", False)), item.get("platform", "")))


def vcpkg_manifest(graph: dict, base: dict, baseline: str) -> dict:
    from .contracts import COMMIT, violations
    if violations(baseline, COMMIT, "baseline"):
        raise PackageError("vcpkg baseline must be an explicit complete commit")
    if "dependencies" not in base or not isinstance(base["dependencies"], list):
        raise PackageError("vcpkg core baseline must explicitly list dependencies")
    if "builtin-baseline" in base and base["builtin-baseline"] != baseline:
        raise PackageError("vcpkg core baseline conflicts with selected baseline")
    additions = []
    for provider in graph["providers"]:
        if provider["id"] == "obcx-sdk":
            continue  # The supplied core base, not actor metadata, owns SDK deps.
        manager = provider["package_manager"]
        if manager["kind"] != "vcpkg":
            raise PackageError(f"provider {provider['id']}: explicit vcpkg port binding required; names are not guessed")
        additions.append({"name": manager["port"], "features": manager["features"],
                          "default-features": manager["default_features"]})
    return {**base, "builtin-baseline": baseline,
            "dependencies": merge_vcpkg_dependencies(base["dependencies"], additions)}
