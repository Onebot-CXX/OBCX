from __future__ import annotations

from contextlib import redirect_stderr
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def load_script(name: str):
    path = ROOT / "scripts" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load_script("package_actor_release")
rollback = load_script("rehearse_actor_release_rollback")
verify_release = load_script("verify_actor_release")


class ActorReleaseToolsTest(unittest.TestCase):
    def test_environment_prefix_is_normalized_for_cmake_cache(self) -> None:
        self.assertEqual(
            verify_release.environment_prefix_to_cmake(
                "/nix/first:/nix/second", ":"
            ),
            "/nix/first;/nix/second",
        )
        self.assertEqual(
            verify_release.environment_prefix_to_cmake(
                r"C:\first;D:\second", ";"
            ),
            r"C:\first;D:\second",
        )
        self.assertEqual(
            verify_release.environment_prefix_to_cmake("", ":"), ""
        )

    def test_release_platform_names_are_canonical(self) -> None:
        self.assertEqual(
            package.release_platform_name("Linux", "AMD64"),
            "linux-x86_64",
        )
        self.assertEqual(
            package.release_platform_name("Linux", "aarch64"),
            "linux-arm64",
        )
        with self.assertRaisesRegex(
            package.PackagingFailure, "unsupported release operating system"
        ):
            package.release_platform_name("Darwin", "aarch64")
        with self.assertRaisesRegex(
            package.PackagingFailure, "unsupported release architecture"
        ):
            package.release_platform_name("Linux", "riscv64")

    def test_packaging_reports_invalid_date_without_traceback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            deployment = root / "deployment"
            deployment.mkdir()
            stderr = io.StringIO()

            with redirect_stderr(stderr):
                result = package.main(
                    [
                        "--deployment",
                        str(deployment),
                        "--output-dir",
                        str(root / "output"),
                        "--recorded-date",
                        "not-a-date",
                    ]
                )

            self.assertEqual(result, 1)
            self.assertIn("release packaging failed:", stderr.getvalue())
            self.assertIn("YYYY-MM-DD", stderr.getvalue())
            self.assertNotIn("Traceback", stderr.getvalue())

    def test_retired_release_entrypoints_refuse_before_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "output"
            output.mkdir()
            sentinel = output / "keep.txt"
            sentinel.write_text("keep\n", encoding="utf-8")
            commands = [
                [sys.executable, str(ROOT / "scripts/package_actor_release.py"),
                 "--deployment", str(root / "deployment"), "--output-dir", str(output),
                 "--recorded-date", "2026-07-30", "--clean"],
                [sys.executable, str(ROOT / "scripts/verify_actor_release.py"),
                 "--work-dir", str(output), "--jobs", "20"],
                ["sh", str(ROOT / "scripts/build-podman-image.sh"), "--tag", "unused"],
            ]
            for command in commands:
                with self.subTest(command=command[1]):
                    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("unavailable after package-v2 cutover", result.stderr)
                    self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep\n")
                    self.assertEqual(list(root.iterdir()), [output])

    def test_release_archives_are_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first_source = root / "first.txt"
            second_source = root / "second.txt"
            first_source.write_text("first\n", encoding="utf-8")
            second_source.write_text("second\n", encoding="utf-8")
            os.utime(first_source, (100, 100))
            os.utime(second_source, (200, 200))

            members = [
                (second_source, "release/second.txt"),
                (first_source, "release/first.txt"),
            ]
            first_archive = root / "first.tar.gz"
            second_archive = root / "second.tar.gz"
            package.deterministic_tar(first_archive, members)
            os.utime(first_source, (300, 300))
            os.utime(second_source, (400, 400))
            package.deterministic_tar(second_archive, reversed(members))

            self.assertEqual(first_archive.read_bytes(), second_archive.read_bytes())
            with tarfile.open(first_archive, "r:gz") as archive:
                entries = archive.getmembers()
            self.assertEqual(
                [entry.name for entry in entries],
                ["release/first.txt", "release/second.txt"],
            )
            for entry in entries:
                self.assertEqual(entry.mtime, 0)
                self.assertEqual(entry.uid, 0)
                self.assertEqual(entry.gid, 0)

    def test_core_archive_excludes_actors_and_verifier(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            deployment = Path(temporary)
            included = deployment / "bin" / "obcx"
            actor = deployment / "lib" / "obcx" / "actors" / "bridge.so"
            metadata = (
                deployment
                / "share"
                / "obcx"
                / "packages"
                / "vollate.bridge"
                / "package.toml"
            )
            verifier = deployment / "libexec" / "obcx" / "smoke"
            for path in (included, actor, metadata, verifier):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(path.name, encoding="utf-8")

            members = package.deployment_members(deployment, "obcx-core")
            names = [name for _, name in members]
            self.assertEqual(set(names), {"obcx-core/bin/obcx"})

    def test_single_dso_packager_rejects_declared_library_closure(self) -> None:
        # Keep the existing archive utilities, but do not advertise a partial
        # archive as a deployable package once ordinary libraries are declared.
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from package_test_support import fixture, write_toml
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "package.toml"
            document = fixture("actor")
            write_toml(path, document)
            with self.assertRaisesRegex(package.PackagingFailure, "inventory/closure"):
                package.actor_metadata(path)
            document["dependencies"]["libraries"] = []
            write_toml(path, document)
            self.assertEqual(package.actor_metadata(path)["id"], document["package"]["id"])

    def test_deployment_switch_is_atomic_and_resolves_release(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate"
            previous = root / "previous"
            candidate.mkdir()
            previous.mkdir()
            releases = root / "deployment" / "releases"
            releases.mkdir(parents=True)
            (releases / "candidate").symlink_to(candidate)
            (releases / "previous").symlink_to(previous)
            steps: list[dict[str, object]] = []

            current = rollback.atomic_switch(
                root / "deployment", "candidate", steps
            )
            self.assertEqual(current.resolve(), candidate)
            current = rollback.atomic_switch(
                root / "deployment", "previous", steps
            )
            self.assertEqual(current.resolve(), previous)
            self.assertEqual(
                [step["name"] for step in steps],
                ["switch-to-candidate", "switch-to-previous"],
            )

if __name__ == "__main__":
    unittest.main()
