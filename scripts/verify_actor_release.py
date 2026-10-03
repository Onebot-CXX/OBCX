#!/usr/bin/env python3
"""Release verification is unavailable after package-v2 cutover.

Use the explicit workspace build and actor_sdk_v2_smoke regression instead.
Inventory/closure-based release verification is deferred; neither a local
build nor the existing SDK smoke proves a coordinated release is complete.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import Sequence


def environment_prefix_to_cmake(value: str, separator: str = os.pathsep) -> str:
    """Translate the platform environment list into CMake cache-list syntax."""
    if not value:
        return ""
    return ";".join(entry for entry in value.split(separator) if entry)


def main(argv: Sequence[str] | None = None) -> int:
    # Only --help is supported. Old arguments must never trigger cleanup of a
    # work directory, compilation of retired targets, or execution of actors.
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments in (["--help"], ["-h"]):
        argparse.ArgumentParser(description=__doc__).print_help()
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
