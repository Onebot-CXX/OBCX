"""Explicit version schemes: SemVer packages/SDK and dotted numeric providers."""
from __future__ import annotations

from dataclasses import dataclass
from functools import total_ordering
import re

from . import PackageError

SEMVER = (r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
          r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
          r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?")


@total_ordering
@dataclass(frozen=True, eq=False)
class Version:
    major: int
    minor: int
    patch: int
    prerelease: tuple[str, ...]

    @classmethod
    def parse(cls, value: str) -> Version:
        match = re.fullmatch(SEMVER, value)
        if not match:
            raise PackageError("expected a complete SemVer (major.minor.patch)")
        pre = tuple(match[4].split(".")) if match[4] else ()
        if any(part.isdigit() and len(part) > 1 and part[0] == "0" for part in pre):
            raise PackageError("numeric pre-release identifiers must not have leading zeros")
        return cls(int(match[1]), int(match[2]), int(match[3]), pre)

    def key(self) -> tuple:
        # Releases sort after pre-releases; numeric identifiers before strings.
        return (self.major, self.minor, self.patch, not self.prerelease,
                tuple((0, int(s)) if s.isdigit() else (1, s) for s in self.prerelease))

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Version) and self.key() == other.key()

    def __lt__(self, other: Version) -> bool:
        if not isinstance(other, Version):
            return NotImplemented
        return self.key() < other.key()

    def __hash__(self) -> int:
        return hash(self.key())


def numeric(value: str) -> tuple[int, ...]:
    if not re.fullmatch(r"(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*))*", value):
        raise PackageError("expected a dotted numeric provider version")
    parts = [int(part) for part in value.split(".")]
    while len(parts) > 1 and parts[-1] == 0:
        parts.pop()
    return tuple(parts)


def parse_version(value: str, scheme: str):
    if scheme == "semver":
        return Version.parse(value)
    if scheme == "numeric":
        return numeric(value)
    raise PackageError("unsupported explicit version scheme")


def constraints(value: str, scheme: str) -> list[tuple[str, object]]:
    result = []
    for clause in value.split(","):
        match = re.fullmatch(r"\s*(>=|<=|=|>|<)\s*([^\s,]+)\s*", clause)
        if not match:
            raise PackageError("use explicit =/>=/<=/>/< clauses separated by commas; ^/~/*/|| are unsupported")
        result.append((match[1], parse_version(match[2], scheme)))
    return result


def satisfies(version: str, requirement: str, scheme: str) -> bool:
    candidate = parse_version(version, scheme)
    terms = constraints(requirement, scheme)
    if scheme == "semver" and candidate.prerelease:
        # Only an explicit pre-release comparator for the same core permits it.
        core = (candidate.major, candidate.minor, candidate.patch)
        if not any(v.prerelease and (v.major, v.minor, v.patch) == core for _, v in terms):
            return False
    for op, bound in terms:
        if not {"=": candidate == bound, ">=": candidate >= bound,
                "<=": candidate <= bound, ">": candidate > bound,
                "<": candidate < bound}[op]:
            return False
    return True
