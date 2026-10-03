"""Resolve explicit package declarations into an offline CMake plan."""
from . import PackageError
from .build_inputs import validate_outputs
from .io import atomic_write, encoded, exclusive
from .providers import verify_environment
from .resolver import Resolver
from .versions import satisfies


def quote(value: str) -> str:
    delimiter = "="
    while f"]{delimiter}]" in value:
        delimiter += "="
    return f"[{delimiter}[{value}]{delimiter}]"


def plan(args) -> None:
    graph = Resolver(args.workspace, args.cache, args.mode, "deny").resolve()
    validate_outputs(args.workspace, graph, [args.output, args.current_graph])
    if graph["platform"] != args.platform:
        raise PackageError("workspace platform differs from actual CMake target platform")
    for node in graph["packages"]:
        compatibility = node["metadata"]["compatibility"]
        if args.compiler_id != compatibility["compiler"]["id"] or not satisfies(
                args.compiler_version, compatibility["compiler"]["version"], "semver"):
            raise PackageError(f"{node['id']}: actual compiler does not satisfy compatibility.compiler")
    for provider in graph["providers"]:
        verify_environment(provider, [], args.workspace.resolve().parent, args.build_dir.resolve())
    nodes = {node["id"]: node for node in graph["packages"]}
    lines = [f"set_property(GLOBAL PROPERTY OBCX_PACKAGE_GRAPH {quote(encoded(graph).decode())})"]
    for node in graph["packages"]:
        lines.append(f"_obcx_record_package({quote(encoded(node).decode())})")
    for provider in graph["providers"]:
        lines.append(f"_obcx_load_provider({quote(encoded(provider).decode())})")
    # Everything is registered before package CMake runs. Only topology controls
    # configuration order; workspace source declaration order has no effect.
    for package_id in graph["order"]:
        node = nodes[package_id]
        lines.append(f"_obcx_configure_package({quote(package_id)} {quote(node['source_dir'])})")
    with exclusive(args.output.with_name(args.output.name + ".guard")):
        atomic_write(args.current_graph, encoded(graph))
        atomic_write(args.output, ("\n".join(lines) + "\n").encode())
