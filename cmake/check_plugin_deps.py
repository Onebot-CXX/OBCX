#!/usr/bin/env python3
"""Extract vcpkg_deps from a plugin.toml file. One package name per line."""
import re
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(1)

    path = sys.argv[1]

    try:
        import tomllib
    except ImportError:
        try:
            import tomli as tomllib
        except ImportError:
            tomllib = None

    if tomllib:
        with open(path, "rb") as f:
            data = tomllib.load(f)
        deps = data.get("build", {}).get("vcpkg_deps", [])
    else:
        with open(path) as f:
            content = f.read()
        m = re.search(r'vcpkg_deps\s*=\s*\[([^\]]*)\]', content)
        deps = re.findall(r'"([^"]*)"', m.group(1)) if m else []

    for d in deps:
        print(d)


if __name__ == "__main__":
    main()
