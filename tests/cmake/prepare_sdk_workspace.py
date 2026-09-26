"""Author explicit inputs for the existing installed-SDK consumption smoke."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
for name in ("tool", "sdk", "source", "state", "targets"):
    parser.add_argument("--" + name, type=Path, required=True)
parser.add_argument("--version", required=True)
parser.add_argument("--platform", required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.tool.parent))
from obcx_package.io import digest, encoded, metadata
from obcx_package.target_audit import cmake_list

# Imported SDK dependencies were observed by the trusted core configure, not
# inferred from a package name or granted the whole host/Nix store.
roots = {str(args.sdk.resolve())}
for target in json.loads(args.targets.read_bytes()):
    if target["IMPORTED"] != "TRUE":
        continue
    paths = list(target["locations"])
    for field in ("INCLUDE_DIRECTORIES", "INTERFACE_INCLUDE_DIRECTORIES", "LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"):
        paths.extend(cmake_list(target[field]))
    for item in paths:
        if Path(item).is_absolute():
            path = Path(item).resolve(strict=True)
            roots.add(str(path if path.is_dir() else path.parent))
inputs = [args.targets] + sorted(args.tool.parent.glob("*.cmake"))
environment = dict(schema_version=2, kind="installed-sdk",
    prefixes=[dict(base="absolute", path=p) for p in sorted(roots)],
    inputs=[dict(path=os.path.relpath(p.resolve(), args.state.resolve()), sha256=digest(p.read_bytes())) for p in inputs])
args.state.mkdir(parents=True, exist_ok=True)
anchor = args.state / "sdk-environment.json"
anchor.write_bytes(encoded(environment))
package = metadata(args.source / "package.toml", "package")
provider = dict(id="obcx-sdk", kind="installed-sdk", package="obcx-sdk", components=[],
    targets=["obcx::obcx_core"], version=args.version, version_scheme="semver",
    version_probe=dict(kind="target-property", target="obcx::obcx_core", property="OBCX_SDK_VERSION"),
    package_manager=dict(kind="none"),
    provenance=dict(kind="installed-sdk", path=anchor.name, sha256=digest(anchor.read_bytes())))
workspace = dict(schema_version=2, workspace=dict(roots=[package["package"]["id"]],
    platform=args.platform, profile="tests"),
    sources=[dict(id=package["package"]["id"], kind="path", path=os.path.relpath(args.source.resolve(), args.state.resolve()),
                  provenance=dict(kind="working-tree"))], providers=[provider])

def toml(value):
    if isinstance(value, dict):
        return "{ " + ", ".join(key + " = " + toml(item) for key, item in value.items()) + " }"
    if isinstance(value, list):
        return "[" + ", ".join(map(toml, value)) + "]"
    return json.dumps(value)

manifest = args.state / "packages.toml"
manifest.write_text("\n".join(key + " = " + toml(value) for key, value in workspace.items()) + "\n")
subprocess.run([sys.executable, str(args.tool), "lock", "--workspace", str(manifest),
    "--lock", str(args.state / "packages.lock"), "--graph", str(args.state / "graph.json"),
    "--cache", str(args.state / "sources"), "--mode", "development", "--network", "deny"], check=True)
