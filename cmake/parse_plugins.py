#!/usr/bin/env python3
"""Parse plugins.toml and output CMake-readable format."""
import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Usage: parse_plugins.py <plugins.toml>", file=sys.stderr)
        sys.exit(1)

    toml_path = sys.argv[1]
    if not os.path.exists(toml_path):
        print(f"File not found: {toml_path}", file=sys.stderr)
        sys.exit(1)

    # Use tomllib (Python 3.11+) or fallback to simple parsing
    try:
        import tomllib
    except ImportError:
        try:
            import tomli as tomllib
        except ImportError:
            # Fallback: simple manual parsing
            _parse_simple(toml_path)
            return

    with open(toml_path, "rb") as f:
        data = tomllib.load(f)

    # Output local plugins
    local_section = data.get("local", {})
    for plugin in local_section.get("plugins", []):
        name = plugin["name"]
        path = plugin["path"]
        enabled = str(plugin.get("enabled", True)).lower()
        print(f"LOCAL|{name}|{enabled}|{path}")

    # Output remote plugins
    remote_section = data.get("remote", {})
    for repo in remote_section.get("plugins", []):
        print(f"REMOTE|{repo}")

    # Output overrides
    overrides = remote_section.get("overrides", {})
    for repo, settings in overrides.items():
        tag = settings.get("tag", "")
        branch = settings.get("branch", "")
        print(f"OVERRIDE|{repo}|{tag}|{branch}")


def _parse_simple(toml_path):
    """Simple fallback parser for when tomllib is not available."""
    with open(toml_path, "r") as f:
        content = f.read()

    import re

    # Parse local plugins: { name = "...", path = "...", enabled = ... }
    local_pattern = r'\{\s*name\s*=\s*"([^"]+)"\s*,\s*path\s*=\s*"([^"]+)"(?:\s*,\s*enabled\s*=\s*(true|false))?\s*\}'
    for match in re.finditer(local_pattern, content):
        name = match.group(1)
        path = match.group(2)
        enabled = match.group(3) if match.group(3) else "true"
        print(f"LOCAL|{name}|{enabled}|{path}")

    # Parse remote plugins: simple strings in array
    # Find the [remote] section's plugins array
    remote_match = re.search(r'\[remote\]\s*\nplugins\s*=\s*\[(.*?)\]', content, re.DOTALL)
    if remote_match:
        array_content = remote_match.group(1)
        for repo_match in re.finditer(r'"([^"]+)"', array_content):
            print(f"REMOTE;{repo_match.group(1)}")


if __name__ == "__main__":
    main()
