"""Deterministic metadata IO and process-safe atomic output."""
from __future__ import annotations

from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import tempfile
import tomllib

from . import PackageError
from .contracts import validate


def encoded(value) -> bytes:
    return (json.dumps(value, sort_keys=True, ensure_ascii=True, indent=2) + "\n").encode()


def digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def normalize(value):
    """Only metadata/workspace arrays are unordered sets, NOT graph order."""
    if isinstance(value, dict):
        return {key: normalize(item) for key, item in sorted(value.items())}
    if isinstance(value, list):
        return sorted((normalize(item) for item in value), key=encoded)
    return value


def metadata(path: Path, kind: str) -> dict:
    expected = {"package": "package.toml", "workspace": "packages.toml"}[kind]
    if path.name != expected:
        raise PackageError(f"{path}: v2 {expected} is required; legacy file names are not accepted")
    try:
        document = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, tomllib.TOMLDecodeError):
        raise PackageError(f"{path}: cannot read valid v2 {expected}") from None
    try:
        validate(document, kind)
    except PackageError as error:
        raise PackageError(f"{path}: {error}") from None
    return normalize(document)


@contextmanager
def exclusive(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Keep the inode: unlinking a flock file introduces a third-writer race.
    with path.open("a+b") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)


def atomic_write(path: Path, value: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(value)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
        directory = os.open(path.parent, os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        Path(name).unlink(missing_ok=True)


def read_json(path: Path, kind: str) -> dict:
    try:
        value = json.loads(path.read_bytes())
    except (OSError, ValueError):
        raise PackageError(f"{path}: missing or invalid {kind}; explicit preparation/relock required") from None
    validate(value, kind)
    return value
