#!/usr/bin/env python3
"""Generate vcpkg.json by merging base dependencies with plugin dependencies.

Reads vcpkg-base.json (core OBCX deps) and collects vcpkg_deps from each
enabled plugin's plugin.toml, then writes a merged vcpkg.json.

Usage:
    # Generate vcpkg.json (default: reads plugins.toml in same dir as vcpkg-base.json)
    python3 cmake/gen_vcpkg_manifest.py [plugins.toml]

    # Just list all required packages (for non-vcpkg users)
    python3 cmake/gen_vcpkg_manifest.py [plugins.toml] --list
"""
import json
import os
import re
import sys
from pathlib import Path


def load_toml(path: str) -> dict:
    """Load a TOML file, with fallback to simple regex parsing."""
    try:
        import tomllib
    except ImportError:
        try:
            import tomli as tomllib
        except ImportError:
            return _parse_toml_simple(path)

    with open(path, "rb") as f:
        return tomllib.load(f)


def _parse_toml_simple(path: str) -> dict:
    """Minimal TOML parser — handles only what we need."""
    with open(path) as f:
        content = f.read()

    result: dict = {}

    # Parse [section] and [section.subsection]
    current_section = result
    section_path: list[str] = []

    for line in content.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue

        # Section header
        m = re.match(r"^\[([^\]]+)\]$", line)
        if m:
            section_path = m.group(1).split(".")
            current_section = result
            for key in section_path:
                current_section = current_section.setdefault(key, {})
            continue

        # Key = value
        m = re.match(r'^(\w+)\s*=\s*(.+)$', line)
        if m:
            key = m.group(1)
            val_str = m.group(2).strip()
            current_section[key] = _parse_toml_value(val_str, content, key)

    return result


def _parse_toml_value(val_str: str, full_content: str = "", key: str = ""):
    """Parse a simple TOML value."""
    if val_str.startswith('"') and val_str.endswith('"'):
        return val_str[1:-1]
    if val_str == "true":
        return True
    if val_str == "false":
        return False
    if val_str.isdigit():
        return int(val_str)

    # Simple array of strings: ["a", "b"]
    if val_str.startswith("["):
        # Handle multi-line arrays — find matching ]
        if "]" in val_str:
            arr_str = val_str
        else:
            # Find in full content
            pattern = rf'{re.escape(key)}\s*=\s*\[(.*?)\]'
            m = re.search(pattern, full_content, re.DOTALL)
            arr_str = f"[{m.group(1)}]" if m else val_str

        items = re.findall(r'"([^"]*)"', arr_str)
        return items

    return val_str


def collect_plugin_deps(
    plugins_toml_path: str, project_root: str
) -> tuple[list[str], dict[str, list[str]]]:
    """Collect vcpkg_deps from all enabled plugins.

    Returns:
        (all_deps, per_plugin_deps) where per_plugin_deps maps name -> deps list
    """
    plugins_data = load_toml(plugins_toml_path)
    all_deps: list[str] = []
    per_plugin: dict[str, list[str]] = {}

    # Local plugins
    local_section = plugins_data.get("local", {})
    for plugin in local_section.get("plugins", []):
        if isinstance(plugin, str):
            continue
        name = plugin.get("name", "")
        path = plugin.get("path", "")
        enabled = plugin.get("enabled", True)

        if not enabled or enabled == "false":
            continue

        # Resolve plugin path
        if os.path.isabs(path):
            plugin_dir = path
        else:
            plugin_dir = os.path.join(project_root, path)

        plugin_toml = os.path.join(plugin_dir, "plugin.toml")
        if os.path.exists(plugin_toml):
            deps = _extract_vcpkg_deps(plugin_toml)
            if deps:
                all_deps.extend(deps)
                per_plugin[name] = deps

    # Remote plugins — try to read from previously fetched sources
    remote_section = plugins_data.get("remote", {})
    for repo in remote_section.get("plugins", []):
        if not isinstance(repo, str):
            continue
        cmake_name = repo.replace("/", "_")
        # Check common fetch locations
        for fetch_dir in [
            os.path.join(project_root, "build", "_plugins", cmake_name),
            os.path.join(project_root, "cmake-build-debug", "_plugins", cmake_name),
            os.path.join(project_root, "cmake-build-release", "_plugins", cmake_name),
        ]:
            plugin_toml = os.path.join(fetch_dir, "plugin.toml")
            if os.path.exists(plugin_toml):
                deps = _extract_vcpkg_deps(plugin_toml)
                if deps:
                    all_deps.extend(deps)
                    per_plugin[repo] = deps
                break

    return all_deps, per_plugin


def _extract_vcpkg_deps(plugin_toml_path: str) -> list[str]:
    """Extract vcpkg_deps from a plugin.toml file."""
    try:
        data = load_toml(plugin_toml_path)
        build = data.get("build", {})
        deps = build.get("vcpkg_deps", [])
        return deps if isinstance(deps, list) else []
    except Exception as e:
        print(f"Warning: failed to parse {plugin_toml_path}: {e}", file=sys.stderr)
        return []


def main():
    args = sys.argv[1:]
    list_mode = "--list" in args
    if list_mode:
        args.remove("--list")

    # Determine project root (where vcpkg-base.json lives)
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent

    # Plugins manifest path
    if args:
        plugins_toml = args[0]
        if not os.path.isabs(plugins_toml):
            plugins_toml = os.path.join(project_root, plugins_toml)
    else:
        plugins_toml = os.path.join(project_root, "plugins.toml")

    base_json_path = os.path.join(project_root, "vcpkg-base.json")
    output_path = os.path.join(project_root, "vcpkg.json")

    if not os.path.exists(base_json_path):
        print(f"Error: {base_json_path} not found", file=sys.stderr)
        sys.exit(1)

    # Load base manifest
    with open(base_json_path) as f:
        base = json.load(f)

    base_deps = set(base.get("dependencies", []))

    # Collect plugin deps
    plugin_deps: list[str] = []
    per_plugin: dict[str, list[str]] = {}

    if os.path.exists(plugins_toml):
        plugin_deps, per_plugin = collect_plugin_deps(plugins_toml, str(project_root))

    merged_deps = sorted(base_deps | set(plugin_deps))

    if list_mode:
        print("=== OBCX core dependencies ===")
        for d in sorted(base_deps):
            print(f"  {d}")

        if per_plugin:
            print("\n=== Plugin dependencies ===")
            for name, deps in sorted(per_plugin.items()):
                print(f"  [{name}]")
                for d in deps:
                    print(f"    {d}")

        new_deps = set(plugin_deps) - base_deps
        if new_deps:
            print(f"\n=== Additional packages needed by plugins ===")
            for d in sorted(new_deps):
                print(f"  {d}")

        print(f"\n=== All required packages ({len(merged_deps)}) ===")
        for d in merged_deps:
            print(f"  {d}")
        return

    # Generate vcpkg.json
    base["dependencies"] = merged_deps
    with open(output_path, "w") as f:
        json.dump(base, f, indent=2)
        f.write("\n")

    added = set(plugin_deps) - base_deps
    print(f"Generated {output_path}")
    print(f"  Base deps: {len(base_deps)}, Plugin deps: {len(added)}, Total: {len(merged_deps)}")
    if added:
        print(f"  Added by plugins: {', '.join(sorted(added))}")


if __name__ == "__main__":
    main()
