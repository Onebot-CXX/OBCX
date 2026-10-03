from __future__ import annotations

import copy
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import WorkspaceCase, archive
from obcx_package import PackageError
from obcx_package.io import digest, encoded
from obcx_package.sources import Sources, extract_archive, git, git_object, git_tree, tree_digest


class PackageSourcesTest(WorkspaceCase):
    def cache_source(self, subdir="."):
        directory = self.root / "example.mapping"
        self.save()
        root_tree = git_tree(directory)
        commit = f"tree {root_tree}\nauthor Test <test@example.invalid> 0 +0000\ncommitter Test <test@example.invalid> 0 +0000\n\nfixture\n".encode()
        source = {"id": "example.mapping", "kind": "git", "repository": "https://example.invalid/mapping",
                  "commit": git_object("commit", commit).hex(), "subdir": subdir}
        key = digest(encoded({"repository": source["repository"], "commit": source["commit"]}))
        cache = self.cache / key
        cache.mkdir(parents=True)
        shutil.copytree(directory, cache / "source")
        (cache / "commit-object").write_bytes(commit)
        return source, cache

    def store(self, mode="development", network="deny"):
        return Sources(self.root, self.cache, mode, network)

    def test_offline_complete_git_cache(self):
        source, cache = self.cache_source()
        with patch("obcx_package.sources.Sources.fetch_git", side_effect=AssertionError("offline fetch")):
            root, identity, receipt = self.store().prepare(source)
        self.assertEqual(root, cache / "source")
        self.assertEqual(identity["digest"], git_tree(root))
        self.assertFalse(receipt["dirty"])

    def test_git_cache_checks_all_source_not_just_metadata(self):
        source, cache = self.cache_source()
        (cache / "source" / "unrecorded.cpp").write_text("int fake;\n")
        with self.assertRaisesRegex(PackageError, "source tree was modified"):
            self.store().prepare(source)

    def test_cache_rejects_injected_empty_dirs_and_git_admin(self):
        source, cache = self.cache_source()
        (cache / "source" / "unrecorded").mkdir()
        with self.assertRaisesRegex(PackageError, "unrecorded empty directories"):
            self.store().prepare(source)
        (cache / "source" / "unrecorded").rmdir()
        (cache / "source" / ".git").mkdir()
        with self.assertRaisesRegex(PackageError, "injected VCS"):
            self.store().prepare(source)

    def test_cache_commit_identity_cannot_be_relabelled(self):
        source, cache = self.cache_source()
        (cache / "commit-object").write_text("tree " + "a" * 40 + "\n")
        with self.assertRaisesRegex(PackageError, "commit identity mismatch"):
            self.store().prepare(source)

    def test_offline_missing_cache_never_fetches(self):
        source, cache = self.cache_source()
        shutil.rmtree(cache)
        with patch("obcx_package.sources.Sources.fetch_git", side_effect=AssertionError("offline fetch")):
            with self.assertRaisesRegex(PackageError, "offline: missing source example.mapping commit"):
                self.store().prepare(source)

    def test_online_preparation_publishes_only_after_verification(self):
        source, cache = self.cache_source()
        saved = self.root / "saved"
        shutil.move(cache, saved)
        def fake_fetch(_, destination):
            shutil.copytree(saved, destination, dirs_exist_ok=True)
        with patch.object(Sources, "fetch_git", side_effect=fake_fetch):
            root, _, _ = self.store(network="allow").prepare(source)
        self.assertEqual(root, cache / "source")
        self.assertEqual(root.stat().st_mode & 0o222, 0)
        self.assertEqual((root / "package.toml").stat().st_mode & 0o222, 0)
        self.assertEqual(list(self.cache.glob(".prepare-*")), [])
        Sources.discard(cache)
        (saved / "source" / "bad").write_text("tampered")
        with patch.object(Sources, "fetch_git", side_effect=fake_fetch):
            with self.assertRaises(PackageError):
                self.store(network="allow").prepare(source)
        self.assertFalse(cache.exists())
        self.assertEqual(list(self.cache.glob(".prepare-*")), [])

    def test_raw_git_fetch_ignores_archive_filters_without_network(self):
        root = self.root / "example.mapping"
        (root / ".gitattributes").write_text("keep.cpp export-ignore\nsubstitute.txt export-subst\n")
        (root / "keep.cpp").write_text("int must_be_present;\n")
        (root / "substitute.txt").write_text("$Format:%H$\n")
        git(["init", "."], root)
        git(["add", "."], root)
        tree = git(["write-tree"], root).decode().strip()
        commit = f"tree {tree}\nauthor Test <test@example.invalid> 0 +0000\ncommitter Test <test@example.invalid> 0 +0000\n\nfixture\n".encode()
        result = subprocess.run(["git", "hash-object", "-t", "commit", "-w", "--stdin"], cwd=root,
                                input=commit, check=True, capture_output=True)
        source = {"id": "example.mapping", "kind": "git", "repository": "https://example.invalid/mapping",
                  "commit": result.stdout.decode().strip(), "subdir": "."}
        def local_transport(args, cwd):
            if args[0] == "fetch":
                shutil.copytree(root / ".git/objects", cwd / "objects", dirs_exist_ok=True)
                return b""
            return git(args, cwd)
        with patch("obcx_package.sources.git", side_effect=local_transport):
            prepared, identity, _ = self.store(network="allow").prepare(source)
        self.assertEqual(identity["digest"], tree)
        self.assertTrue((prepared / "keep.cpp").exists())
        self.assertEqual((prepared / "substitute.txt").read_text(), "$Format:%H$\n")

    def test_symlink_loop_is_a_structured_error(self):
        root = self.root / "example.mapping"
        (root / "one").symlink_to("two")
        (root / "two").symlink_to("one")
        with self.assertRaisesRegex(PackageError, "symlink loop"):
            self.store().prepare(self.workspace["sources"][1])

    def test_generated_cache_must_be_outside_source_roots(self):
        source = self.workspace["sources"][1]
        store = Sources(self.root, self.root / "example.mapping/cache", "development", "deny")
        with self.assertRaisesRegex(PackageError, "cache must be outside"):
            store.prepare(source)

    def test_remote_subdir_must_exist(self):
        source, _ = self.cache_source(subdir="missing")
        with self.assertRaisesRegex(PackageError, "subdir does not exist"):
            self.store().prepare(source)

    def test_source_symlink_cannot_escape_root(self):
        (self.root / "example.mapping" / "escape").symlink_to(self.root / "sdk-receipt.json")
        with self.assertRaisesRegex(PackageError, "symlink escapes"):
            self.store().prepare(self.workspace["sources"][1])

    def test_lfs_and_submodules_rejected(self):
        root = self.root / "example.mapping"
        for filename, content in ((".gitmodules", b""), ("image", b"version https://git-lfs.github.com/spec/v1\n")):
            (root / filename).write_bytes(content)
            with self.subTest(filename=filename), self.assertRaisesRegex(PackageError, "submodules/LFS"):
                self.store().prepare(self.workspace["sources"][1])
            (root / filename).unlink()

    def test_git_tree_identity_matches_git_for_modes_links_and_sorting(self):
        root = self.root / "example.mapping"
        (root / "foo").mkdir()
        (root / "foo" / "nested").write_text("nested")
        (root / "foo.bar").write_text("sort before directory")
        (root / "executable").write_text("#!/bin/sh\nexit 0\n")
        (root / "executable").chmod(0o755)
        (root / "link").symlink_to("foo/nested")
        git(["init", "."], root)
        git(["add", "."], root)
        expected = git(["write-tree"], root).decode().strip()
        self.assertEqual(git_tree(root), expected)

    def test_release_requires_explicit_provenance(self):
        with self.assertRaisesRegex(PackageError, "release needs explicit fixed"):
            self.resolve(mode="release")

    def test_release_archive_matches_whole_source(self):
        source = copy.deepcopy(self.workspace["sources"][1])
        content = archive(self.root / "example.mapping")
        (self.root / "mapping.tar").write_bytes(content)
        source["provenance"] = {"kind": "archive", "path": "mapping.tar", "sha256": digest(content)}
        _, identity, receipt = self.store(mode="release").prepare(source)
        self.assertEqual(identity["digest"], digest(content))
        self.assertFalse(receipt["dirty"])
        (self.root / "example.mapping" / "unrecorded.cpp").write_text("bad")
        with self.assertRaisesRegex(PackageError, "differs from fixed archive"):
            self.store(mode="release").prepare(source)
        (self.root / "mapping.tar").write_bytes(b"tampered")
        with self.assertRaisesRegex(PackageError, "archive digest mismatch"):
            self.store(mode="release").prepare(source)

    def test_release_git_rejects_dirty_untracked_even_ignored(self):
        root = self.root / "example.mapping"
        (root / ".gitignore").write_text("ignored.cpp\n")
        git(["init", "."], root)
        git(["add", "."], root)
        tree = git(["write-tree"], root).decode().strip()
        commit = f"tree {tree}\nauthor Test <test@example.invalid> 0 +0000\ncommitter Test <test@example.invalid> 0 +0000\n\nfixture\n".encode()
        # Create a fixture object, not a project commit or history update.
        process = subprocess.run(["git", "hash-object", "-t", "commit", "-w", "--stdin"], cwd=root,
                                 input=commit, check=True, capture_output=True)
        git(["update-ref", "HEAD", process.stdout.decode().strip()], root)
        source = copy.deepcopy(self.workspace["sources"][1])
        source["provenance"] = {"kind": "git", "repository": "https://example.invalid/mapping",
                                "commit": process.stdout.decode().strip(), "subdir": "."}
        self.store(mode="release").prepare(source)
        for name in ("ignored.cpp", "not-tracked.cpp"):
            (root / name).write_text("bad")
            with self.subTest(name=name), self.assertRaisesRegex(PackageError, "dirty/unrecorded"):
                self.store(mode="release").prepare(source)
            (root / name).unlink()
        (root / "package.toml").write_text("modified")
        with self.assertRaisesRegex(PackageError, "dirty/unrecorded"):
            self.store(mode="release").prepare(source)

    def test_archive_rejects_traversal_absolute_paths_and_special_entries(self):
        for name, kind in (("../escape", tarfile.REGTYPE), ("/escape", tarfile.REGTYPE),
                           (".git/config", tarfile.REGTYPE), ("fifo", tarfile.FIFOTYPE), ("hard", tarfile.LNKTYPE)):
            stream = io.BytesIO()
            with tarfile.open(fileobj=stream, mode="w") as tar:
                entry = tarfile.TarInfo(name)
                entry.type = kind
                tar.addfile(entry)
            with self.subTest(name=name), self.assertRaises(PackageError):
                extract_archive(stream.getvalue(), self.root / "extracted")

    def test_archive_rejects_symlink_parent_and_duplicate_members(self):
        for attack in ("symlink", "duplicate"):
            stream = io.BytesIO()
            with tarfile.open(fileobj=stream, mode="w") as tar:
                if attack == "symlink":
                    directory = tarfile.TarInfo("real")
                    directory.type = tarfile.DIRTYPE
                    tar.addfile(directory)
                    link = tarfile.TarInfo("link")
                    link.type = tarfile.SYMTYPE
                    link.linkname = "real"
                    tar.addfile(link)
                    tar.addfile(tarfile.TarInfo("link/payload"))
                else:
                    tar.addfile(tarfile.TarInfo("same"))
                    tar.addfile(tarfile.TarInfo("same"))
            with self.subTest(attack=attack), self.assertRaises(PackageError):
                extract_archive(stream.getvalue(), self.root / attack)

    def test_safe_archive_round_trip(self):
        root = self.root / "example.mapping"
        (root / "space # %.txt").write_text("unicode 文件")
        (root / "internal").symlink_to("space # %.txt")
        destination = self.root / "round-trip"
        extract_archive(archive(root), destination)
        self.assertEqual(tree_digest(root), tree_digest(destination))

    def test_development_dirty_probe_does_not_run_clean_filters(self):
        root = self.root / "example.mapping"
        (root / ".gitattributes").write_text("*.cpp filter=probe\n")
        (root / "sample.cpp").write_text("original")
        git(["init", "."], root)
        git(["add", "."], root)
        marker = self.root / "filter-executed"
        git(["config", "filter.probe.clean", f"touch '{marker}'"], root)
        (root / "sample.cpp").write_text("modified")
        _, _, receipt = self.store().prepare(self.workspace["sources"][1])
        self.assertTrue(receipt["dirty"])
        self.assertFalse(marker.exists())

    def test_git_errors_do_not_leak_transport_output(self):
        error = subprocess.CalledProcessError(1, ["git"], stderr=b"password and token")
        with patch("obcx_package.sources.subprocess.run", side_effect=error):
            with self.assertRaises(PackageError) as caught:
                git(["fetch", "https://example.invalid/repo"], self.root)
        self.assertNotIn("password", str(caught.exception))
        self.assertNotIn("token", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
