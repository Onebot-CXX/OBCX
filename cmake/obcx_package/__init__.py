"""Canonical OBCX package tooling; no package build code is executed here."""

TOOL_VERSION = "2.0.0"
SCHEMA_VERSION = 2


class PackageError(ValueError):
    """An actionable metadata, source or dependency contract violation."""
