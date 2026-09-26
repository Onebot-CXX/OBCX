"""Fixed source preparation. No CMake, hooks, implicit LFS or submodule fetches."""
from __future__ import annotations

import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile

from . import PackageError
from .io import digest, encoded, exclusive


def contained(root: Path, path: Path) -> Path:
    try:
        try:
            resolved = path.resolve(strict=True)
        except FileNotFoundError:
            # Archives can contain an internal link before its target is added.
            resolved = path.resolve(strict=False)
    except (OSError, RuntimeError):
        raise PackageError("source contains an unresolvable path or symlink loop") from None
    if not resolved.is_relative_to(root.resolve()):
        raise PackageError("source path or symlink escapes its declared root")
    return resolved


def git(args: list[str], cwd: Path) -> bytes:
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL="/dev/null",
               GIT_TERMINAL_PROMPT="0", GIT_LFS_SKIP_SMUDGE="1")
    try:
        result = subprocess.run(
            ["git", "-c", "protocol.allow=never", "-c", "protocol.https.allow=always",
             "-c", "core.hooksPath=/dev/null", "-c", "core.autocrlf=false",
             "-c", "core.fsmonitor=false", "-c", "fetch.fsckObjects=true",
             "-c", "fetch.recurseSubmodules=false", *args],
            cwd=cwd, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=120)
    except (OSError, subprocess.SubprocessError):
        # Git diagnostics may contain credentials supplied by host configuration.
        raise PackageError("fixed-source Git operation failed (raw transport diagnostics suppressed)") from None
    return result.stdout


def tree_files(root: Path) -> list[tuple[str, str, bytes]]:
    if not root.is_dir():
        raise PackageError("source directory is missing")
    result = []

    def fail_walk(error):
        raise PackageError("cannot enumerate the complete source tree") from error

    for directory, dirs, files in os.walk(root, followlinks=False, onerror=fail_walk):
        base = Path(directory)
        # Only VCS administration is excluded. Untracked/ignored build inputs
        # are not silently omitted from the source identity.
        dirs[:] = sorted(d for d in dirs if d != ".git")
        for name in sorted(set(files) | {d for d in dirs if (base / d).is_symlink()}):
            if name == ".git":
                continue
            path = base / name
            rel = path.relative_to(root).as_posix()
            if path.is_symlink():
                contained(root, path)
                content = os.readlink(path).encode("utf-8")
                mode = "120000"
            elif path.is_file():
                content = path.read_bytes()
                mode = "100755" if path.stat().st_mode & 0o111 else "100644"
                if name == ".gitmodules" or content.startswith(b"version https://git-lfs.github.com/spec/v1"):
                    raise PackageError("implicit Git submodules/LFS are unsupported; declare complete fixed sources")
            else:
                raise PackageError("source contains a non-regular file")
            result.append((rel, mode, content))
    return sorted(result)


def tree_directories(root: Path, allow_vcs: bool) -> list[str]:
    directories = []

    def fail_walk(error):
        raise PackageError("cannot enumerate complete source directories") from error

    for base, dirs, files in os.walk(root, followlinks=False, onerror=fail_walk):
        if not allow_vcs and ".git" in [*dirs, *files]:
            raise PackageError("Git cache must not contain injected VCS administration")
        dirs[:] = sorted(d for d in dirs if d != ".git" and not (Path(base) / d).is_symlink())
        directories.append(Path(base).relative_to(root).as_posix())
    return sorted(directories)


def verify_git_layout(root: Path, allow_vcs: bool) -> None:
    needed = {"."}
    for name, _, _ in tree_files(root):
        needed.update(parent.as_posix() for parent in PurePosixPath(name).parents)
    if set(tree_directories(root, allow_vcs)) != needed:
        raise PackageError("Git source contains unrecorded empty directories")


def tree_digest(root: Path) -> str:
    return digest(encoded({"directories": tree_directories(root, True),
                           "files": [{"path": name, "mode": mode, "sha256": digest(content)}
                                     for name, mode, content in tree_files(root)]}))


def git_object(kind: str, data: bytes) -> bytes:
    return hashlib.sha1(f"{kind} {len(data)}\0".encode() + data).digest()


def git_tree(root: Path) -> str:
    tree = {}
    for name, mode, content in tree_files(root):
        node = tree
        parts = name.split("/")
        for part in parts[:-1]:
            node = node.setdefault(part, {})
        node[parts[-1]] = (mode, git_object("blob", content))

    def identity(node: dict) -> bytes:
        entries = []
        for name, child in node.items():
            is_dir = isinstance(child, dict)
            mode, oid = ("40000", identity(child)) if is_dir else child
            entries.append((name.encode() + (b"/" if is_dir else b""),
                            mode.encode() + b" " + name.encode() + b"\0" + oid))
        return git_object("tree", b"".join(data for _, data in sorted(entries)))

    return identity(tree).hex()


def extract_archive(content: bytes, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    seen = set()
    try:
        with tarfile.open(fileobj=io.BytesIO(content), mode="r:*") as archive:
            for entry in archive:
                name = PurePosixPath(entry.name)
                if (name.is_absolute() or ".." in name.parts or ".git" in name.parts or
                        not name.parts or str(name) in seen):
                    raise PackageError("source archive has unsafe or duplicate paths")
                seen.add(str(name))
                target = destination / str(name)
                for parent in target.parents:
                    if parent == destination:
                        break
                    if parent.is_symlink():
                        raise PackageError("source archive traverses a symlink")
                target.parent.mkdir(parents=True, exist_ok=True)
                if entry.isdir():
                    target.mkdir(exist_ok=True)
                elif entry.isfile():
                    stream = archive.extractfile(entry)
                    if stream is None:
                        raise PackageError("source archive file cannot be read")
                    target.write_bytes(stream.read())
                    target.chmod(0o755 if entry.mode & 0o111 else 0o644)
                elif entry.issym():
                    if PurePosixPath(entry.linkname).is_absolute():
                        raise PackageError("source archive contains an absolute symlink")
                    contained(destination, target.parent / entry.linkname)
                    target.symlink_to(entry.linkname)
                else:
                    raise PackageError("source archive contains an unsupported entry")
    except (tarfile.TarError, OSError):
        raise PackageError("cannot extract fixed source archive") from None
    tree_files(destination)


class Sources:
    def __init__(self, workspace: Path, cache: Path, mode: str, network: str):
        if mode not in {"development", "release"} or network not in {"allow", "deny"}:
            raise PackageError("mode and network policy must be explicit supported values")
        self.workspace = workspace
        self.cache = cache
        self.mode = mode
        self.network = network

    def prepare_git(self, source: dict) -> tuple[Path, dict]:
        key = digest(encoded({"repository": source["repository"], "commit": source["commit"]}))
        checkout = self.cache / key
        with exclusive(self.cache / f"{key}.guard"):
            if not checkout.exists():
                if self.network == "deny":
                    raise PackageError(f"offline: missing source {source['id']} commit {source['commit']}")
                self.cache.mkdir(parents=True, exist_ok=True)
                temporary = Path(tempfile.mkdtemp(prefix=".prepare-", dir=self.cache))
                try:
                    self.fetch_git(source, temporary)
                    self.verify_git(source, temporary)
                    self.freeze(temporary)
                    os.replace(temporary, checkout)
                finally:
                    if temporary.exists():
                        self.discard(temporary)
            root = self.verify_git(source, checkout)
        return root, {"kind": "git", "digest": git_tree(root)}

    @staticmethod
    def discard(directory: Path) -> None:
        for base, _, _ in os.walk(directory, followlinks=False):
            Path(base).chmod(0o700)
        shutil.rmtree(directory)

    @staticmethod
    def freeze(directory: Path) -> None:
        # Read-only inputs discourage accidental generated files in cache. Hash
        # verification, not permissions or the cache directory name, is evidence.
        for base, dirs, files in os.walk(directory, topdown=False, followlinks=False):
            for name in files:
                path = Path(base) / name
                if not path.is_symlink():
                    path.chmod(path.stat().st_mode & ~0o222)
            Path(base).chmod(0o555)

    def fetch_git(self, source: dict, destination: Path) -> None:
        objects = destination / "objects"
        objects.mkdir()
        git(["init", "--bare", "."], objects)
        git(["fetch", "--no-tags", "--depth=1", "--no-recurse-submodules",
             source["repository"], source["commit"]], objects)
        commit = git(["cat-file", "commit", source["commit"]], objects)
        (destination / "commit-object").write_bytes(commit)
        # Read raw tree/blob objects, not checkout/archive: .gitattributes may
        # contain filters, export-ignore or export-subst that change source bytes.
        listing = git(["ls-tree", "-rz", "--full-tree", source["commit"]], objects)
        root = destination / "source"
        root.mkdir()
        for record in listing.split(b"\0"):
            if not record:
                continue
            header, raw_name = record.split(b"\t", 1)
            mode, kind, oid = header.decode("ascii").split()
            name = PurePosixPath(raw_name.decode("utf-8"))
            if (kind != "blob" or mode not in {"100644", "100755", "120000"} or
                    name.is_absolute() or ".." in name.parts or ".git" in name.parts):
                raise PackageError("Git tree contains unsupported submodules or unsafe paths")
            target = root / str(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            content = git(["cat-file", "blob", oid], objects)
            if mode == "120000":
                link = content.decode("utf-8")
                if PurePosixPath(link).is_absolute():
                    raise PackageError("Git source contains an absolute symlink")
                contained(root, target.parent / link)
                target.symlink_to(link)
            else:
                target.write_bytes(content)
                target.chmod(0o755 if mode == "100755" else 0o644)
        shutil.rmtree(objects)

    def verify_git(self, source: dict, checkout: Path) -> Path:
        try:
            commit = (checkout / "commit-object").read_bytes()
        except OSError:
            raise PackageError(f"source {source['id']}: incomplete Git cache") from None
        match = re.match(rb"tree ([0-9a-f]{40})\n", commit)
        if not match or git_object("commit", commit).hex() != source["commit"]:
            raise PackageError(f"source {source['id']}: cached commit identity mismatch")
        tree = checkout / "source"
        verify_git_layout(tree, False)
        if git_tree(tree) != match[1].decode():
            raise PackageError(f"source {source['id']}: cached source tree was modified")
        root = contained(tree, tree / source["subdir"])
        if not root.is_dir():
            raise PackageError(f"source {source['id']}: subdir does not exist")
        return root

    def prepare(self, source: dict) -> tuple[Path, dict, dict]:
        if source["kind"] == "git":
            root, identity = self.prepare_git(source)
            return root, identity, {"sha256": tree_digest(root), "dirty": False}
        root = (self.workspace / source["path"]).resolve()
        if self.cache.resolve().is_relative_to(root):
            raise PackageError("cache must be outside path package roots to keep generated files out of source identity")
        provenance = source["provenance"]
        actual = tree_digest(root)
        if self.mode == "development":
            dirty = self.dirty(root)
            return root, {"kind": "working-tree", "digest": "metadata-only"}, {"sha256": actual, "dirty": dirty}
        if provenance["kind"] == "working-tree":
            raise PackageError(f"source {source['id']}: release needs explicit fixed Git/archive provenance")
        if provenance["kind"] == "git":
            verify_git_layout(root, True)
            # Explicit local provenance can be checked without a remote fetch.
            commit = git(["cat-file", "commit", provenance["commit"]], root)
            if git_object("commit", commit).hex() != provenance["commit"]:
                raise PackageError(f"source {source['id']}: release commit identity mismatch")
            spec = provenance["commit"] + ("^{tree}" if provenance["subdir"] == "." else ":" + provenance["subdir"])
            expected = git(["rev-parse", spec], root).decode().strip()
            if git_tree(root) != expected or self.dirty(root):
                raise PackageError(f"source {source['id']}: dirty/unrecorded release source does not match fixed Git tree")
            identity = {"kind": "git", "digest": expected}
        else:
            archive_path = self.workspace / provenance["path"]
            try:
                content = archive_path.read_bytes()
            except OSError:
                raise PackageError(f"source {source['id']}: release archive is missing") from None
            if digest(content) != provenance["sha256"]:
                raise PackageError(f"source {source['id']}: release archive digest mismatch")
            with tempfile.TemporaryDirectory() as temporary:
                extract_archive(content, Path(temporary))
                if tree_digest(Path(temporary)) != actual:
                    raise PackageError(f"source {source['id']}: release source differs from fixed archive")
            identity = {"kind": "archive", "digest": provenance["sha256"]}
        return root, identity, {"sha256": actual, "dirty": False}

    @staticmethod
    def dirty(root: Path) -> bool:
        try:
            verify_git_layout(root, True)
            # Do not run git status/diff: configured clean/textconv/fsmonitor
            # filters can execute package-local code. Compare raw tree objects.
            prefix = git(["rev-parse", "--show-prefix"], root).decode().strip().rstrip("/")
            spec = "HEAD:" + prefix if prefix else "HEAD^{tree}"
            expected = git(["rev-parse", spec], root).decode().strip()
            return git_tree(root) != expected
        except PackageError:
            return True
