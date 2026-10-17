#!/usr/bin/env python3
"""Package a selected, stripped OBCX install into a scratch Docker archive.

Every CLI option and every runtime-spec list is explicit. This tool neither
selects application configuration nor contacts a deployment host.
"""
import argparse
import gzip
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def command(args, *, env=None):
    result = subprocess.run(args, capture_output=True, text=True, env=env, timeout=180)
    require(result.returncode == 0, f"Command failed: {args[0]}\n{result.stderr[-4000:]}")
    return result.stdout


def elf(path):
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


class Runtime:
    def __init__(self, prefix, output, patchelf):
        self.prefix = prefix
        self.root = output / "context/rootfs"
        self.root.mkdir(parents=True)
        self.patchelf = patchelf
        self.seen = set()
        self.objects = set()
        self.inventory = {}
        self.env = dict(os.environ)
        self.env.pop("LD_PRELOAD", None)
        self.env["LC_ALL"] = "C"
        self.env["LD_LIBRARY_PATH"] = os.pathsep.join(str(prefix / d) for d in ("lib", "lib64"))

    def mapped(self, source):
        if source.is_relative_to(self.prefix):
            return PurePosixPath("/opt/obcx") / source.relative_to(self.prefix).as_posix()
        require(source.is_relative_to(Path("/nix/store")),
                f"Undeclared dependency outside install prefix/Nix store: {source}")
        return PurePosixPath(source.as_posix())

    def destination(self, path):
        path = PurePosixPath(path)
        require(path.is_absolute() and ".." not in path.parts and len(path.parts) > 1,
                f"Invalid image path: {path}")
        require(path.parts[1] not in ("proc", "sys", "dev"), f"Reserved image path: {path}")
        dest = self.root.joinpath(*path.parts[1:])
        parent = self.root
        for part in path.parts[1:-1]:
            parent /= part
            require(not parent.is_symlink(), f"Image parent is a symlink: {parent}")
            parent.mkdir(exist_ok=True)
        return dest

    def alias(self, target, destination):
        dest = self.destination(destination)
        if dest.exists() or dest.is_symlink():
            require(dest.is_symlink() and os.readlink(dest) == str(target),
                    f"Conflicting image path: {destination}")
        else:
            dest.symlink_to(str(target))

    def include(self, source):
        source = Path(os.path.abspath(source))
        actual = source.resolve(strict=True)
        destination = self.mapped(actual)
        if actual not in self.seen:
            require(actual.is_file(), f"Expected a runtime file: {actual}")
            self.seen.add(actual)
            dest = self.destination(destination)
            require(not dest.exists() and not dest.is_symlink(), f"Duplicate path: {destination}")
            shutil.copy2(actual, dest)
            dest.chmod(0o755 if os.access(actual, os.X_OK) else 0o644)
            self.inventory[str(destination)] = str(actual)
            if elf(actual):
                self.objects.add(str(destination))
                self.dependencies(actual)
                if actual.is_relative_to(self.prefix):
                    old = command([str(self.patchelf), "--print-rpath", str(actual)]).strip()
                    paths = ["/opt/obcx/lib", "/opt/obcx/lib64"]
                    for item in old.split(":"):
                        if item.startswith(str(self.prefix) + "/"):
                            paths.append("/opt/obcx/" + Path(item).relative_to(self.prefix).as_posix())
                        elif item.startswith(("/nix/store/", "$ORIGIN")):
                            paths.append(item)
                    command([str(self.patchelf), "--set-rpath", ":".join(dict.fromkeys(paths)), str(dest)])
        lexical = self.mapped(source)
        if lexical != destination:
            self.alias(destination, lexical)
        return lexical

    def dependencies(self, source):
        dynamic = command(["readelf", "-l", "-d", str(source)], env=self.env)
        interp = re.search(r"Requesting program interpreter: ([^\]]+)\]", dynamic)
        if interp:
            self.include(Path(interp[1]))
        if "(NEEDED)" not in dynamic:
            return
        listing = command(["ldd", str(source)], env=self.env)
        require("not found" not in listing, f"Unresolved ELF dependencies in {source}:\n{listing}")
        for line in listing.splitlines():
            match = re.match(r"\s*(?:\S+\s+=>\s+)?(/.*?)\s+\(0x[0-9a-f]+\)", line)
            if match:
                self.include(Path(match[1]))

    def asset(self, source, destination):
        source = source.resolve(strict=True)
        require(source.is_file(), f"Expected an asset file: {source}")
        require(not elf(source), f"ELF assets must be declared as executables or plugin directories: {source}")
        require(source.suffix.lower() not in (".db", ".sqlite", ".sqlite3") and source.name != ".env",
                f"Refusing database/secret-file asset: {source}")
        with source.open("rb") as stream:
            start = stream.read(4096)
        require(not re.search(rb"-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----", start),
                f"Refusing private key asset: {source}")
        dest = self.destination(destination)
        require(not dest.exists() and not dest.is_symlink(), f"Conflicting asset: {destination}")
        shutil.copy2(source, dest)
        dest.chmod(0o755 if os.access(source, os.X_OK) else 0o644)
        self.inventory[str(destination)] = str(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--actors", nargs="+", required=True, help="Artifact names including transitive actors")
    parser.add_argument("--runtime-spec", type=Path, required=True)
    parser.add_argument("--patchelf", type=Path, required=True, help="Exact OBCX_PATCHELF_EXECUTABLE used in the build")
    parser.add_argument("--output", type=Path, required=True, help="New, nonexistent output directory")
    parser.add_argument("--tag", required=True, help="Explicit qualified name:tag, e.g. localhost/obcx:build-id")
    parser.add_argument("--engine", choices=("podman", "docker"), required=True)
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    require(not args.output.is_symlink(), "Output must not be a symlink")
    output = args.output.resolve()
    require(not output.exists(), f"Output already exists: {output}")
    require(not output.is_relative_to(prefix), "Output must be outside the install prefix")
    require(re.fullmatch(r"[a-z0-9][a-z0-9./_:-]*:[A-Za-z0-9_][A-Za-z0-9_.-]*", args.tag) is not None
            and "/" in args.tag, "Supply an explicit qualified image name and tag")
    for name in args.actors:
        require(re.fullmatch(r"[A-Za-z0-9_-]+", name) is not None, f"Invalid actor artifact name: {name}")
    spec = json.loads(args.runtime_spec.read_text())
    require(set(spec) == {"executables", "files", "directories"}, "Runtime spec requires executables, files and directories")
    for entries in spec.values():
        require(isinstance(entries, list), "Runtime-spec sections must be explicit arrays")
        for entry in entries:
            require(isinstance(entry, dict) and set(entry) == {"source", "destination"}, "Invalid runtime-spec entry")
            require(Path(entry["source"]).is_absolute(), "Runtime sources must be absolute")
    command([args.engine, "info"])
    existing = subprocess.run([args.engine, "image", "inspect", args.tag], capture_output=True, timeout=30)
    require(existing.returncode != 0, f"Image tag already exists; choose a new tag: {args.tag}")
    output.mkdir(mode=0o700, parents=True)
    runtime = Runtime(prefix, output, args.patchelf.resolve(strict=True))
    app = prefix / "bin/obcx"
    require(app.is_file() and elf(app), "Missing installed OBCX executable")
    runtime.include(app)
    installed_actors = {}
    for lib in (prefix / "lib", prefix / "lib64"):
        for actor in (lib / "obcx/actors").glob("*.so"):
            require(actor.stem not in installed_actors, "Duplicate installed actor artifact")
            installed_actors[actor.stem] = actor
    require(set(installed_actors) == set(args.actors), "Installed actors differ from the resolved actor closure")
    for actor in installed_actors.values():
        runtime.include(actor)
    tool = runtime.include(args.patchelf.resolve(strict=True))
    declared_tool = Path(os.path.abspath(args.patchelf))
    if str(tool) != str(declared_tool):
        runtime.alias(tool, str(declared_tool))
    for entry in spec["executables"]:
        source = Path(entry["source"])
        require(source.is_file() and elf(source), "Executable roots must be native ELF; declare scripts as files and their interpreter separately")
        target = runtime.include(source)
        if str(target) != entry["destination"]:
            runtime.alias(target, entry["destination"])
    for entry in spec["files"]:
        runtime.asset(Path(entry["source"]), entry["destination"])
    for entry in spec["directories"]:
        source = Path(entry["source"]).resolve(strict=True)
        require(source.is_dir() and source.is_relative_to(Path("/nix/store")), "Runtime directories must be explicit Nix output subdirectories")
        # Preserve native absolute paths, including Python/DSO search locations.
        require(entry["destination"] == str(source), "Runtime directories must retain their Nix paths")
        for base, dirs, files in os.walk(source, followlinks=False):
            require(not any((Path(base) / d).is_symlink() for d in dirs), "Declare directory-symlink targets explicitly")
            for name in files:
                runtime.include(Path(base) / name)
    for path in (prefix / "share").rglob("*"):
        if path.is_file() and (any(part.lower() in ("license", "licenses", "licence", "licences") for part in path.parts)
                               or path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE"))):
            runtime.asset(path, "/opt/obcx/" + path.relative_to(prefix).as_posix())
    ca = runtime.root / "etc/ssl/certs/ca-bundle.crt"
    require(ca.is_file(), "Declare the CA bundle at /etc/ssl/certs/ca-bundle.crt")
    temporary = runtime.root / "tmp"
    require(not temporary.is_symlink(), "Image /tmp must not be a symlink")
    temporary.mkdir(mode=0o1777, exist_ok=True)
    temporary.chmod(0o1777)
    nsswitch = runtime.destination("/etc/nsswitch.conf")
    require(not nsswitch.exists() and not nsswitch.is_symlink(), "Duplicate nsswitch configuration")
    nsswitch.write_text("hosts: files dns\n")
    dockerfile = output / "context/Dockerfile"
    dockerfile.write_text('FROM scratch\nCOPY rootfs/ /\n'
                         'ENV PATH="/opt/obcx/bin:/usr/local/bin:/bin" '
                         'SSL_CERT_FILE="/etc/ssl/certs/ca-bundle.crt" '
                         'CURL_CA_BUNDLE="/etc/ssl/certs/ca-bundle.crt" LANG="C.UTF-8"\n'
                         'ENTRYPOINT ["/opt/obcx/bin/obcx"]\n')
    pull = ["--pull=never"] if args.engine == "podman" else ["--pull=false"]
    with (output / "image-build.log").open("w") as log:
        result = subprocess.run([args.engine, "build", "--network", "none", *pull, "-t", args.tag, str(dockerfile.parent)], stdout=log, stderr=subprocess.STDOUT)
    require(result.returncode == 0, f"Image build failed; see {output / 'image-build.log'}")
    version = command([args.engine, "run", "--rm", "--network", "none", args.tag, "--version"])
    program_headers = command(["readelf", "-l", str(app)])
    interpreter = re.search(r"Requesting program interpreter: ([^\]]+)\]", program_headers)
    require(interpreter is not None, "Expected a dynamically linked native executable")
    for actor in installed_actors.values():
        command([args.engine, "run", "--rm", "--network", "none", "--entrypoint", interpreter[1], args.tag, "--list", str(runtime.mapped(actor))])
    image = json.loads(command([args.engine, "image", "inspect", args.tag]))[0]
    partial = output / "obcx-image.tar.gz.partial"
    save = [args.engine, "save"] + (["--format", "docker-archive"] if args.engine == "podman" else []) + [args.tag]
    with partial.open("wb") as raw, (output / "image-save.log").open("wb") as log:
        with subprocess.Popen(save, stdout=subprocess.PIPE, stderr=log) as proc:
            with gzip.GzipFile(filename="", mode="wb", compresslevel=9, fileobj=raw, mtime=0) as compressed:
                shutil.copyfileobj(proc.stdout, compressed, 1024 * 1024)
            require(proc.wait() == 0, f"Image export failed; see {output / 'image-save.log'}")
    archive = output / "obcx-image.tar.gz"
    partial.rename(archive)
    archive.chmod(0o644)
    report = {"image": args.tag, "image_id": image["Id"], "actors": sorted(args.actors),
              "archive": str(archive), "archive_bytes": archive.stat().st_size,
              "rootfs_bytes": sum(p.stat().st_size for p in runtime.root.rglob("*") if p.is_file() and not p.is_symlink()),
              "version": version.strip(), "runtime_files": runtime.inventory,
              "elf_objects": sorted(runtime.objects), "runtime_spec": spec,
              "validation": "network-disabled --version and actor dependency loading; not live forwarding"}
    (output / "image-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(archive)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"Image packaging failed: {error}", file=sys.stderr)
        sys.exit(1)
