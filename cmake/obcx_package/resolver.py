"""Resolve explicitly bound candidates, never search for alternative versions."""
from __future__ import annotations

from pathlib import Path

from . import PackageError, SCHEMA_VERSION, TOOL_VERSION
from .contracts import validate
from .io import digest, encoded, metadata
from .sources import Sources
from .versions import satisfies


class Resolver:
    def __init__(self, manifest: Path, cache: Path, mode: str, network: str):
        self.manifest = manifest.resolve()
        self.workspace = metadata(self.manifest, "workspace")
        self.options = self.workspace["workspace"]
        self.sources = {s["id"]: s for s in self.workspace["sources"]}
        self.bindings = {p["id"]: p for p in self.workspace["providers"]}
        self.store = Sources(self.manifest.parent, cache.resolve(), mode, network)
        self.nodes = {}
        self.providers = {}
        self.edges = []
        self.order = []
        self.active = []
        self.targets = {}

    def resolve(self) -> dict:
        for root in self.options["roots"]:
            self.visit(root)
        self.check_requirements()
        graph = {"schema_version": SCHEMA_VERSION, "tool_version": TOOL_VERSION,
                 "workspace_sha256": digest(encoded(self.workspace)), **self.options,
                 "mode": self.store.mode,
                 "packages": [self.nodes[key] for key in sorted(self.nodes)],
                 "providers": [self.providers[key] for key in sorted(self.providers)],
                 "edges": sorted(self.edges, key=encoded), "order": self.order,
                 "unused_sources": sorted(self.sources.keys() - self.nodes.keys())}
        validate(graph, "resolved-packages")
        return graph

    def claim_target(self, target: str, owner: str) -> None:
        if target in self.targets and self.targets[target] != owner:
            raise PackageError(f"target {target}: conflict between {self.targets[target]} and {owner}")
        self.targets[target] = owner

    def visit(self, package_id: str) -> None:
        if package_id in self.active:
            raise PackageError("dependency cycle: " + " -> ".join([*self.active, package_id]))
        if package_id in self.nodes:
            return
        if package_id not in self.sources:
            raise PackageError("sources: missing binding; chain " + " -> ".join([*self.active, package_id]))
        source = self.sources[package_id]
        chain = " -> ".join([*self.active, package_id])
        try:
            directory, identity, receipt = self.store.prepare(source)
            document = metadata(directory / "package.toml", "package")
        except PackageError as error:
            raise PackageError(f"{error}\nchain: {chain}") from None
        package = document["package"]
        if package["id"] != package_id:
            raise PackageError(f"source {package_id}: package.id is {package['id']}; chain {chain}")
        if self.options["platform"] not in document["artifact"]["platforms"]:
            raise PackageError(f"{package_id}: artifact.platforms does not support selected platform; chain {chain}")
        for target in (document["artifact"]["target"], document["artifact"]["export_target"]):
            self.claim_target(target, package_id)
        self.nodes[package_id] = {"id": package_id, "kind": package["kind"], "version": package["version"],
                                  "source": source, "source_identity": identity,
                                  "metadata_sha256": digest(encoded(document)), "metadata": document,
                                  "source_dir": str(directory), "source_receipt": receipt}
        self.active.append(package_id)
        sections = [("production", document["dependencies"])]
        if self.options["profile"] == "tests":
            sections.append(("test", document["test_dependencies"]))
        for scope, dependencies in sections:
            for kind, entries in sorted(dependencies.items()):
                for requirement in entries:
                    dep = requirement["id"]
                    targets = requirement["targets"] if kind == "system" else (
                        [requirement["target"]] if kind == "libraries" else [])
                    self.edges.append({"from": package_id, "to": dep, "scope": scope,
                                       "kind": kind, "requirement": requirement["version"],
                                       "targets": targets,
                                       "visibility": "logical" if kind == "actors" else requirement["visibility"]})
                    if kind == "system":
                        self.provider(dep)
                    else:
                        self.visit(dep)
                        expected = "library" if kind == "libraries" else "actor"
                        node = self.nodes[dep]
                        if node["kind"] != expected:
                            raise PackageError(f"{package_id}: {kind} edge to {dep} expects {expected}, got {node['kind']}; chain {chain} -> {dep}")
                        if targets and node["metadata"]["artifact"]["export_target"] != targets[0]:
                            raise PackageError(f"{package_id}: libraries.target does not match export of {dep}; chain {chain} -> {dep}")
        self.active.pop()
        self.order.append(package_id)

    def provider(self, provider_id: str) -> None:
        if provider_id in self.providers:
            return
        chain = " -> ".join([*self.active, provider_id])
        if provider_id not in self.bindings:
            raise PackageError(f"providers: missing binding; chain {chain}")
        binding = self.bindings[provider_id]
        provenance = binding["provenance"]
        try:
            content = (self.manifest.parent / provenance["path"]).read_bytes()
        except OSError:
            raise PackageError(f"providers.{provider_id}.provenance: missing receipt/lock; chain {chain}") from None
        if digest(content) != provenance["sha256"]:
            raise PackageError(f"providers.{provider_id}.provenance.sha256: drift; chain {chain}")
        for target in binding["targets"]:
            self.claim_target(target, f"system:{provider_id}")
        self.providers[provider_id] = binding

    def paths_to(self, target: str) -> list[list[str]]:
        return paths_to(self.options["roots"], self.edges, target)

    def check_requirements(self) -> None:
        for target in sorted({edge["to"] for edge in self.edges}):
            incoming = [edge for edge in self.edges if edge["to"] == target]
            binding = self.providers.get(target)
            version = binding["version"] if binding else self.nodes[target]["version"]
            scheme = binding["version_scheme"] if binding else "semver"
            failures = []
            for edge in incoming:
                try:
                    valid = satisfies(version, edge["requirement"], scheme)
                except PackageError as error:
                    raise PackageError(f"{edge['from']} -> {target}: invalid version requirement: {error}") from None
                if not valid:
                    failures.append(edge)
                if binding and not set(edge["targets"]) <= set(binding["targets"]):
                    raise PackageError(f"{edge['from']} -> {target}: unauthorized system targets")
                if target == "obcx-sdk":
                    owner = self.nodes[edge["from"]]["metadata"]
                    if owner["package"]["kind"] == "actor" and not satisfies(version, owner["compatibility"]["obcx"], "semver"):
                        raise PackageError(f"{edge['from']}: compatibility.obcx does not include SDK {version}")
            if failures:
                details = [f"version conflict: {target} selected {version}"]
                for edge in incoming:
                    for chain in self.paths_to(edge["from"]):
                        details.append(f"  {' -> '.join([*chain, target])}: {edge['requirement']} ({edge['scope']})")
                raise PackageError("\n".join(details))


def paths_to(roots: list[str], edges: list[dict], target: str) -> list[list[str]]:
    def visit(node: str, chain: list[str]):
        if node == target:
            yield [*chain, node]
        elif node not in chain:
            for child in sorted({e["to"] for e in edges if e["from"] == node}):
                yield from visit(child, [*chain, node])
    return [path for root in roots for path in visit(root, [])]
