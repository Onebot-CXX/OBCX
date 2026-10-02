"""Verify observed providers; a binding is a requirement, not build evidence."""
from __future__ import annotations

from pathlib import Path
import re

from . import PackageError
from .io import digest, read_json
from .versions import parse_version, satisfies


def verify_environment(binding: dict, targets: list[dict], workspace: Path, build: Path) -> dict:
    provenance = binding["provenance"]
    path = workspace / provenance["path"]
    environment = read_json(path, "provider-environment")
    if environment["kind"] != provenance["kind"]:
        raise PackageError(f"provider {binding['id']}: environment kind mismatch")
    for item in environment["inputs"]:
        try:
            content = (workspace / item["path"]).read_bytes()
        except OSError:
            raise PackageError(f"provider {binding['id']}: missing environment input") from None
        if digest(content) != item["sha256"]:
            raise PackageError(f"provider {binding['id']}: environment input drift: {item['path']}")
    prefixes = []
    for item in environment["prefixes"]:
        path = Path(item["path"])
        if (item["base"] == "absolute") != path.is_absolute() or ".." in path.parts:
            raise PackageError(f"provider {binding['id']}: invalid explicit environment prefix")
        if item["base"] == "workspace":
            path = workspace / path
        elif item["base"] == "build":
            path = build / path
        path = path.resolve()
        if environment["kind"] == "nix" and not re.fullmatch(r"/nix/store/[0-9a-z]{32}-[^/]+", str(path)):
            raise PackageError(f"provider {binding['id']}: Nix provenance must name concrete store outputs")
        prefixes.append(path)
    for target in targets:
        paths = [(value, True) for value in target["locations"]]
        paths.extend((value, False) for value in target["include_directories"])
        for raw, is_binary in paths:
            # These two usage wrappers have a defined configure-time meaning.
            if raw.startswith("$<INSTALL_INTERFACE:") and raw.endswith(">"):
                continue
            if raw.startswith("$<BUILD_INTERFACE:") and raw.endswith(">"):
                raw = raw[len("$<BUILD_INTERFACE:"):-1]
            for entry in raw.split(";"):
                if not entry:
                    continue
                path = Path(entry)
                if "$<" in entry or not path.is_absolute():
                    raise PackageError(f"provider {binding['id']}: unsupported observed path expression")
                if not any(path.resolve().is_relative_to(prefix) for prefix in prefixes):
                    raise PackageError(f"provider {binding['id']}: target path is outside declared environment prefixes")
                if is_binary and not path.is_file():
                    raise PackageError(f"provider {binding['id']}: declared binary artifact is missing")
                if not is_binary and not path.is_dir():
                    raise PackageError(f"provider {binding['id']}: declared include directory is missing")
    return environment


def verify_provider(binding: dict, observation: dict, workspace: Path, build: Path) -> dict:
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

    provenance = binding["provenance"]
    try:
        anchor = (workspace / provenance["path"]).read_bytes()
    except OSError:
        raise PackageError(f"provider {provider_id}: missing provenance anchor") from None
    if digest(anchor) != provenance["sha256"]:
        raise PackageError(f"provider {provider_id}: provenance anchor drift")

    verify_environment(binding, observation["targets"], workspace, build)
    version = observation["version"]
    probe = binding["version_probe"]
    if probe["kind"] == "receipt":
        path = workspace / probe["path"]
        try:
            content = path.read_bytes()
        except OSError:
            raise PackageError(f"provider {provider_id}: missing version receipt") from None
        if digest(content) != probe["sha256"]:
            raise PackageError(f"provider {provider_id}: version receipt digest mismatch")
        receipt = read_json(path, "provider-receipt")
        if receipt["id"] != provider_id or receipt["version_scheme"] != binding["version_scheme"]:
            raise PackageError(f"provider {provider_id}: version receipt identity/scheme mismatch")
        if len(receipt["targets"]) != len(observed) or {t["name"] for t in receipt["targets"]} != set(observed):
            raise PackageError(f"provider {provider_id}: receipt target set mismatch")
        for target in receipt["targets"]:
            actual = observed[target["name"]]
            locations = {Path(p).resolve() for p in actual["locations"]}
            directories = [Path(p).resolve() for p in actual["include_directories"] if "$<" not in p]
            files = set()
            for record in target["files"]:
                file = Path(record["path"])
                if not file.is_absolute() or file.resolve() in files:
                    raise PackageError(f"provider {provider_id}: receipt file paths must be absolute and unique")
                file = file.resolve()
                if file not in locations and not any(file.is_relative_to(root) for root in directories):
                    raise PackageError(f"provider {provider_id}: version evidence is unrelated to actual target")
                try:
                    actual_hash = digest(file.read_bytes())
                except OSError:
                    raise PackageError(f"provider {provider_id}: missing version evidence file") from None
                if actual_hash != record["sha256"]:
                    raise PackageError(f"provider {provider_id}: version evidence file changed")
                files.add(file)
            if locations - files:
                raise PackageError(f"provider {provider_id}: receipt omits observed binary artifacts")
        if version and parse_version(version, binding["version_scheme"]) != parse_version(receipt["version"], binding["version_scheme"]):
            raise PackageError(f"provider {provider_id}: observed version conflicts with receipt")
        version = receipt["version"]
    if not version:
        raise PackageError(f"provider {provider_id}: no reported version; an explicit verified receipt is required")
    if not satisfies(version, "=" + binding["version"], binding["version_scheme"]):
        raise PackageError(f"provider {provider_id}: actual version {version} differs from locked {binding['version']}")
    return {"id": provider_id, "version": version, "version_scheme": binding["version_scheme"],
            "provenance_sha256": provenance["sha256"], "targets": observation["targets"]}


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
        if manager["baseline"] != baseline:
            raise PackageError(f"provider {provider['id']}: vcpkg baseline conflict")
        additions.append({"name": manager["port"], "features": manager["features"],
                          "default-features": manager["default_features"]})
    return {**base, "builtin-baseline": baseline,
            "dependencies": merge_vcpkg_dependencies(base["dependencies"], additions)}
