"""Audit registered CMake targets, then native generator output and File API.

CMake evaluates generator expressions; this module does not invent a second
expression language. Unresolved values and unowned edges fail closed. This is a
build-consistency check, not a sandbox for package scripts or C++ code.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import shlex

from . import PackageError
from .io import digest, read_json
from .providers import verify_environment, verify_provider
from .versions import satisfies

KINDS = {"static-library": "STATIC_LIBRARY", "shared-library": "SHARED_LIBRARY", "header-only": "INTERFACE_LIBRARY"}
FALSE = {"", "0", "OFF", "FALSE", "NO", "N", "IGNORE", "NOTFOUND"}


def cmake_list(value: str) -> list[str]:
    # Keep nested expressions intact in the deferred (pre-generation) pass.
    items, start, depth, index = [], 0, 0, 0
    while index < len(value):
        if value.startswith("$<", index):
            depth += 1
            index += 2
            continue
        if value[index] == ">" and depth:
            depth -= 1
        if value[index] == ";" and not depth and (not index or value[index - 1] != "\\"):
            items.append(value[start:index].replace("\\;", ";"))
            start = index + 1
        index += 1
    items.append(value[start:].replace("\\;", ";"))
    return [item for item in items if item and not re.fullmatch(r"::@(?:\(0x[0-9a-fA-F]+\))?", item)]


class Audit:
    def __init__(self, graph: dict, snapshot: dict, evaluated: Path | None):
        self.graph = graph
        self.snapshot = snapshot
        self.nodes = {n["id"]: n for n in graph["packages"]}
        self.targets = snapshot["targets"]
        self.aliases = snapshot["aliases"]
        self.binary_roots = {key: Path(value).resolve() for key, value in snapshot["binary_roots"].items()}
        self.evaluated = evaluated
        self.objects = {}
        if evaluated is not None:
            for name, record in self.targets.items():
                if record["TYPE"] == "OBJECT_LIBRARY":
                    for file in cmake_list((evaluated / (record["key"] + ".objects")).read_text()):
                        self.objects[str(Path(file).resolve())] = name

    def canonical(self, value: str) -> str:
        return self.aliases.get(value, value)

    def values(self, name: str, field: str) -> list[str]:
        record = self.targets[name]
        if self.evaluated is None:
            raw = record["INTERFACE_LINK_LIBRARIES" if field == "INTERFACE_USAGE" else field]
            return [value for value in cmake_list(raw) if "$<" not in value]
        values = cmake_list((self.evaluated / (record["key"] + "." + field)).read_text())
        if any("$<" in value for value in values):
            raise PackageError(f"{name}.{field}: unsupported unresolved generator expression")
        return values

    def allowed(self, name: str) -> dict[str, str]:
        record = self.targets[name]
        result = {}
        for edge in self.graph["edges"]:
            if edge["from"] != record["owner"] or edge["kind"] == "actors":
                continue
            if edge["scope"] == "test" and record["role"] != "test":
                continue
            for target in edge["targets"]:
                result[self.canonical(target)] = edge["visibility"]
        return result

    def chain(self, parent: str, child: str) -> str:
        owner = self.targets[parent]["owner"]
        start = self.nodes[owner]["metadata"]["artifact"]["target"]
        visited = set()
        def walk(name, path):
            if name == parent:
                return [*path, name, child]
            if name in visited or name not in self.targets or self.targets[name]["owner"] != owner:
                return None
            visited.add(name)
            children = [self.canonical(value) for field in ("LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES")
                        for value in self.values(name, field)]
            children += [self.objects[str(self.path(name, value))] for value in self.values(name, "SOURCES")
                         if str(self.path(name, value)) in self.objects]
            for target in children:
                result = walk(target, [*path, name])
                if result:
                    return result
            return None
        return " -> ".join(walk(start, []) or [parent, child])

    def edge(self, parent: str, child: str) -> None:
        child = self.canonical(child)
        source = self.targets[parent]
        if child not in self.targets:
            raise PackageError(f"{parent} -> {child}: unowned target or bare/path linkage")
        target = self.targets[child]
        if source["role"] != "test" and target["role"] == "test":
            raise PackageError(f"{parent} -> {child}: test dependency leaks into production")
        if source["owner"] == target["owner"]:
            return
        if child not in self.allowed(parent):
            raise PackageError(f"{self.chain(parent, child)}: undeclared direct dependency ({source['owner']} -> {target['owner']})")

    def usage(self, name: str, seen: set | None = None) -> tuple[set[Path], set[Path]]:
        seen = set() if seen is None else seen
        name = self.canonical(name)
        if name in seen or name not in self.targets:
            return set(), set()
        seen.add(name)
        record = self.targets[name]
        includes = {self.path(name, p) for p in self.values(name, "INTERFACE_INCLUDE_DIRECTORIES")}
        sources = {self.path(name, p) for p in self.values(name, "INTERFACE_SOURCES") if p not in self.objects}
        for child in self.values(name, "INTERFACE_USAGE"):
            child_includes, child_sources = self.usage(child, seen)
            includes |= child_includes
            sources |= child_sources
        return includes, sources

    def path(self, name: str, value: str) -> Path:
        path = Path(value)
        if not path.is_absolute():
            path = Path(self.targets[name]["SOURCE_DIR"]) / path
        return path.resolve()

    def owns(self, name: str, path: Path) -> bool:
        owner = self.targets[name]["owner"]
        if path.is_relative_to(self.binary_roots[owner]):
            return True
        candidates = [(len(Path(node["source_dir"]).parts), node["id"]) for node in self.nodes.values()
                      if path.is_relative_to(Path(node["source_dir"]).resolve())]
        return bool(candidates) and max(candidates)[1] == owner

    def includes(self, name: str, path: Path, roots: set[Path]) -> bool:
        if any(path != Path(node["source_dir"]).resolve() and Path(node["source_dir"]).resolve().is_relative_to(path)
               for node in self.nodes.values() if node["id"] != self.targets[name]["owner"]):
            raise PackageError(f"{name}: include directory contains another package source root; narrow its scope")
        source_root = Path(self.nodes[self.targets[name]["owner"]]["source_dir"]).resolve()
        return self.owns(name, path) or any(path.is_relative_to(root) for root in roots if root != source_root)

    def roots(self, name: str) -> tuple[set[Path], set[Path]]:
        record = self.targets[name]
        node = self.nodes[record["owner"]]
        roots = {Path(node["source_dir"]).resolve(), self.binary_roots[record["owner"]]}
        sources = set()
        for target in self.allowed(name):
            included, exported_sources = self.usage(target)
            roots |= included
            sources |= exported_sources
        return roots, sources

    def identities(self) -> None:
        for node in self.nodes.values():
            artifact = node["metadata"]["artifact"]
            name = artifact["target"]
            record = self.targets.get(name)
            if record is None or record["owner"] != node["id"] or record["role"] != "artifact":
                raise PackageError(f"{node['id']}: missing or misowned main artifact {name}")
            if self.aliases.get(artifact["export_target"]) != name:
                raise PackageError(f"{node['id']}: export alias does not identify main artifact")
            if record["TYPE"] != KINDS[artifact["kind"]]:
                raise PackageError(f"{name}: artifact type disagrees with package.toml")
            if record["TYPE"] != "INTERFACE_LIBRARY" and record["OUTPUT_NAME"] != artifact["name"]:
                raise PackageError(f"{name}: artifact output name disagrees with package.toml")
            if node["kind"] == "actor" and record["PREFIX"]:
                raise PackageError(f"{name}: actor artifact must retain its runtime filename")
        for name, record in self.targets.items():
            if record["owner"] == "@provider":
                continue
            if record["owner"] not in self.nodes or record["role"] not in {"artifact", "implementation", "test"}:
                raise PackageError(f"{name}: unregistered target owner/role")
            if record["role"] == "test" and self.graph["profile"] != "tests":
                raise PackageError(f"{name}: test target in production profile")
            if record["TYPE"] not in {"INTERFACE_LIBRARY", "UTILITY"}:
                if record["POSITION_INDEPENDENT_CODE"].upper() in FALSE:
                    raise PackageError(f"{name}: PIC is required")
                expected = self.nodes[record["owner"]]["metadata"]["compatibility"]["cpp_standard"]
                if not record["CXX_STANDARD"].isdigit() or int(record["CXX_STANDARD"]) < expected:
                    raise PackageError(f"{name}: C++ standard is below the package contract")

    def check_providers(self) -> None:
        for provider in self.snapshot["providers"].values():
            binding = provider["binding"]
            workspace = Path(self.snapshot["workspace"])
            anchor = workspace / binding["provenance"]["path"]
            if digest(anchor.read_bytes()) != binding["provenance"]["sha256"]:
                raise PackageError(f"{binding['id']}: provider anchor changed after configure")
            verify_provider(binding, json.loads(Path(provider["observed"]).read_bytes()), workspace, Path(self.snapshot["build"]))
            observed = []
            for name in provider["members"]:
                record = self.targets[name]
                locations = list(record["locations"])
                for field in ("LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"):
                    locations.extend(value for value in self.values(name, field) if Path(value).is_absolute())
                included = [value for field in ("INCLUDE_DIRECTORIES", "INTERFACE_INCLUDE_DIRECTORIES")
                            for value in self.values(name, field)]
                observed.append({"name": name, "locations": locations, "include_directories": included})
            verify_environment(binding, observed, workspace, Path(self.snapshot["build"]))

    def check(self) -> None:
        self.identities()
        self.check_providers()
        for name, record in self.targets.items():
            if record["owner"] == "@provider":
                continue
            for child in cmake_list(record["MANUALLY_ADDED_DEPENDENCIES"]):
                child = self.canonical(child)
                logical = any(edge["from"] == record["owner"] and edge["kind"] == "actors" and
                              self.nodes[edge["to"]]["metadata"]["artifact"]["target"] == child
                              for edge in self.graph["edges"])
                if not logical:
                    self.edge(name, child)
            for field in ("LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"):
                for child in self.values(name, field):
                    self.edge(name, child)
                    canonical = self.canonical(child)
                    if field == "INTERFACE_LINK_LIBRARIES" and self.allowed(name).get(canonical) == "private":
                        public = {self.canonical(item) for item in self.values(name, "INTERFACE_USAGE")}
                        if canonical in public:
                            raise PackageError(f"{name} -> {child}: private dependency exposed as public usage")
            # Paths and flags may depend on native usage evaluation. Their full
            # check runs after generation, before any owned target compiles.
            if self.evaluated is None:
                continue
            roots, exported_sources = self.roots(name)
            for field in ("INCLUDE_DIRECTORIES", "INTERFACE_INCLUDE_DIRECTORIES"):
                for item in self.values(name, field):
                    path = self.path(name, item)
                    if not self.includes(name, path, roots):
                        raise PackageError(f"{name}.{field}: include path outside declared usage closure")
            for field in ("LINK_DIRECTORIES", "INTERFACE_LINK_DIRECTORIES"):
                if self.values(name, field):
                    raise PackageError(f"{name}.{field}: raw link directories are unsupported; use declared targets")
            for field in ("LINK_OPTIONS", "INTERFACE_LINK_OPTIONS", "COMPILE_OPTIONS", "INTERFACE_COMPILE_OPTIONS"):
                for option in self.values(name, field):
                    if re.search(r"(^|[ :])(-[ILlF]|-isystem|-iquote|-idirafter|-include|-imacros|--sysroot|-Wl,|-Xlinker|@)", option) or re.search(r"\.(a|so|o)(\.|$)", option):
                        raise PackageError(f"{name}.{field}: unsupported raw dependency flag {option}")
            abi = self.nodes[record["owner"]]["metadata"]["compatibility"]["cxx11_abi"]
            for field in ("COMPILE_DEFINITIONS", "INTERFACE_COMPILE_DEFINITIONS"):
                for definition in self.values(name, field):
                    if definition.startswith("_GLIBCXX_USE_CXX11_ABI") and definition != f"_GLIBCXX_USE_CXX11_ABI={abi}":
                        raise PackageError(f"{name}: C++ ABI definition contradicts package contract")
            for field in ("SOURCES", "INTERFACE_SOURCES"):
                for item in self.values(name, field):
                    path = self.path(name, item)
                    if str(path) in self.objects:
                        self.edge(name, self.objects[str(path)])
                    elif path not in exported_sources and not self.owns(name, path):
                        raise PackageError(f"{name}.{field}: source outside owned/declared interface sources")

    def closure(self, name: str) -> set[str]:
        visited = set()
        def visit(target):
            target = self.canonical(target)
            if target in visited or target not in self.targets:
                return
            visited.add(target)
            for field in ("LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"):
                for child in self.values(target, field):
                    visit(child)
            for field in ("SOURCES", "INTERFACE_SOURCES"):
                for value in self.values(target, field):
                    child = self.objects.get(str(self.path(target, value)))
                    if child:
                        visit(child)
        visit(name)
        return visited

    def file_api(self, build: Path, configuration: str) -> None:
        reply = build / ".cmake/api/v1/reply"
        indices = sorted(reply.glob("index-*.json"), key=lambda path: path.stat().st_mtime_ns)
        if not indices:
            raise PackageError("missing CMake File API reply")
        index = json.loads(indices[-1].read_bytes())
        reference = next((item for item in index["objects"] if item["kind"] == "codemodel"), None)
        if reference is None:
            raise PackageError("missing File API codemodel")
        model = json.loads((reply / reference["jsonFile"]).read_bytes())
        toolchain_ref = next((item for item in index["objects"] if item["kind"] == "toolchains"), None)
        if toolchain_ref is None:
            raise PackageError("missing File API toolchain evidence")
        toolchains = json.loads((reply / toolchain_ref["jsonFile"]).read_bytes())["toolchains"]
        compiler = next((item["compiler"] for item in toolchains if item["language"] == "CXX"), None)
        if compiler is None:
            raise PackageError("missing actual C++ compiler evidence")
        for node in self.nodes.values():
            expected = node["metadata"]["compatibility"]["compiler"]
            if compiler["id"] != expected["id"] or not satisfies(compiler["version"], expected["version"], "semver"):
                raise PackageError(f"{node['id']}: File API compiler differs from compatibility contract")
        implicit_libraries = set(compiler["implicit"]["linkLibraries"])
        implicit_directories = {Path(p).resolve() for p in compiler["implicit"]["linkDirectories"]}
        if Path(model["paths"]["build"]).resolve() != build.resolve():
            raise PackageError("File API belongs to a different build directory")
        config = next((item for item in model["configurations"] if item["name"] == configuration), None)
        if config is None:
            raise PackageError("File API lacks selected configuration")
        compiled = {item["name"]: json.loads((reply / item["jsonFile"]).read_bytes()) for item in config["targets"]}
        names = {item["id"]: item["name"] for item in config["targets"]}
        for name, target in compiled.items():
            directory = (build / target["paths"]["build"]).resolve()
            for owner, root in self.snapshot["binary_roots"].items():
                if directory.is_relative_to(Path(root).resolve()):
                    if name not in self.targets or self.targets[name]["owner"] != owner:
                        raise PackageError(f"{owner}: generated unregistered target {name}")
        def artifact_path(value):
            path = Path(value)
            return (path if path.is_absolute() else build / path).resolve()
        artifacts = {name: {artifact_path(item["path"]) for item in target.get("artifacts", [])}
                     for name, target in compiled.items()}
        for name, record in self.targets.items():
            artifacts.setdefault(name, set()).update(Path(path).resolve() for path in record["locations"])
        for name, record in self.targets.items():
            if record["owner"] == "@provider" or record["TYPE"] in {"INTERFACE_LIBRARY", "UTILITY"}:
                continue
            target = compiled.get(name)
            if target is None or target["type"] != record["TYPE"]:
                raise PackageError(f"{name}: generated target missing/type drift in File API")
            roots, exported_sources = self.roots(name)
            # File API artifact paths are build-root relative, but Make's link
            # command fragments run from the target's binary directory. Ninja
            # runs link commands from the top-level build directory.
            link_directory = (build / target["paths"]["build"]
                              if index["cmake"]["generator"]["name"] == "Unix Makefiles"
                              else build)

            def link_path(value):
                path = Path(value)
                return (path if path.is_absolute() else link_directory / path).resolve()

            closure = self.closure(name)
            for dependency in target.get("dependencies", []):
                child = names[dependency["id"]]
                if child == "obcx_package_audit":
                    continue
                ordered = {self.canonical(value) for value in cmake_list(record["MANUALLY_ADDED_DEPENDENCIES"])}
                if child not in closure and child not in ordered:
                    raise PackageError(f"{name} -> {child}: generated dependency is outside registered target closure")
            for source in target.get("sources", []):
                path = Path(source["path"])
                path = (path if path.is_absolute() else Path(model["paths"]["source"]) / path).resolve()
                if str(path) in self.objects:
                    if self.objects[str(path)] not in closure:
                        raise PackageError(f"{name}: generated object outside registered closure")
                elif path not in exported_sources and not self.owns(name, path):
                    raise PackageError(f"{name}: generated source outside owned/declared interface sources")
            allowed_artifacts = set().union(*(artifacts.get(child, set()) for child in closure))
            provider_literals = set()
            for child in closure:
                if self.targets[child]["owner"] == "@provider":
                    for field in ("LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES", "INTERFACE_LINK_OPTIONS"):
                        provider_literals.update(self.values(child, field))
            provider_literals |= {"-l" + value for value in provider_literals
                                  if re.fullmatch(r"[A-Za-z0-9_+.-]+", value) and self.canonical(value) not in self.targets}
            allowed_artifacts.update(artifact_path(value) for value in provider_literals if Path(value).is_absolute())
            allowed_directories = {path.parent for path in allowed_artifacts} | implicit_directories
            def search_flag(token, cmake_padding=False):
                if token.startswith("-Wl,-rpath,") or token.startswith("-Wl,-rpath-link,"):
                    value = token.split(",", 2)[2]
                    # Native CMake pads installable build-tree RPATHs for its
                    # later RPATH_CHANGE. This is not an explicit search path
                    # grant or an acceptance check for the installed ELF.
                    if cmake_padding and token.startswith("-Wl,-rpath,"):
                        value = value.rstrip(":")
                    paths = value.split(":")
                    if not paths or any(not path or link_path(path) not in allowed_directories for path in paths):
                        raise PackageError(f"{name}: generated runtime search path outside declared closure")
                    return True
                if token.startswith("-L"):
                    if link_path(token[2:]) not in allowed_directories:
                        raise PackageError(f"{name}: generated library search path outside declared closure")
                    return True
                return False
            for fragment in target.get("link", {}).get("commandFragments", []):
                value = fragment["fragment"]
                if fragment["role"] == "libraries":
                    for token in shlex.split(value):
                        padding = "install" in target and "backtrace" not in fragment and not record["BUILD_RPATH"]
                        if search_flag(token, padding) or (token.startswith("-l") and token[2:] in implicit_libraries):
                            continue
                        if link_path(token) not in allowed_artifacts and token not in provider_literals:
                            raise PackageError(f"{name}: generated link item outside declared provider/artifact closure: {token}")
                elif fragment["role"] == "libraryPath":
                    if not all(search_flag(token) for token in shlex.split(value)):
                        raise PackageError(f"{name}: unsupported generated library search expression")
                elif fragment["role"] == "flags":
                    for token in shlex.split(value):
                        if search_flag(token):
                            continue
                        if (re.match(r"(-[LlF]|-Wl,|-Xlinker|@)", token) or
                                re.search(r"\.(a|so|o)(\.|$)", token) or Path(token).is_absolute()) and token not in provider_literals:
                            raise PackageError(f"{name}: generated raw dependency flag {token}")
            compatibility = self.nodes[record["owner"]]["metadata"]["compatibility"]
            for group in target.get("compileGroups", []):
                flags = [token for item in group.get("compileCommandFragments", []) for token in shlex.split(item["fragment"])]
                for flag in flags:
                    if re.match(r"(-[IF]|-isystem|-iquote|-idirafter|-include|-imacros|--sysroot|-Wp,|@)", flag):
                        raise PackageError(f"{name}: generated raw include/toolchain flag {flag}")
                    if flag.startswith("-D_GLIBCXX_USE_CXX11_ABI") and flag != f"-D_GLIBCXX_USE_CXX11_ABI={compatibility['cxx11_abi']}":
                        raise PackageError(f"{name}: generated ABI flag contradicts package contract")
                pic_flags = [flag for flag in flags if flag.lower() in {"-fpic", "-fpie", "-fno-pic", "-fno-pie"}]
                if not pic_flags or pic_flags[-1].lower().startswith("-fno-"):
                    raise PackageError(f"{name}: generated compilation lacks required PIC")
                if group["language"] == "CXX":
                    standards = [flag.split("=", 1)[1] for flag in flags if flag.startswith("-std=")]
                    supported = {"c++20": 20, "c++23": 23, "c++26": 26, "c++2a": 20, "c++2b": 23, "c++2c": 26}
                    if standards and supported.get(standards[-1], 0) < compatibility["cpp_standard"]:
                        raise PackageError(f"{name}: generated standard flag contradicts package contract")
                for entry in group.get("includes", []):
                    if not self.includes(name, Path(entry["path"]).resolve(), roots):
                        raise PackageError(f"{name}: generated include outside declared usage closure")
                for definition in group.get("defines", []):
                    value = definition["define"]
                    if value.startswith("_GLIBCXX_USE_CXX11_ABI") and value != f"_GLIBCXX_USE_CXX11_ABI={compatibility['cxx11_abi']}":
                        raise PackageError(f"{name}: generated ABI differs from contract")
                if group["language"] == "CXX" and int(group["languageStandard"]["standard"]) < compatibility["cpp_standard"]:
                    raise PackageError(f"{name}: generated C++ standard differs from contract")


def audit(args) -> None:
    graph = read_json(args.graph, "resolved-packages")
    snapshot = json.loads(args.snapshot.read_bytes())
    generated = args.command == "audit-generated"
    check = Audit(graph, snapshot, args.evaluated if generated else None)
    check.check()
    if generated:
        check.file_api(args.build_dir, args.configuration)
