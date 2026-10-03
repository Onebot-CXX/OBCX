"""A small real-build fixture covers the three library kinds and audit gates."""
from __future__ import annotations

import json
import os
import platform
from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_test_support import ROOT, WorkspaceCase, digest, encoded


class PackageCMakeTest(WorkspaceCase):
    def setUp(self):
        super().setUp()
        self.workspace["workspace"].update(roots=["example.facade"], profile="tests")
        tokens = self.library("tokens")
        tokens["artifact"]["kind"] = "header-only"
        self.add_package(tokens)
        self.packages["example.mapping"]["dependencies"]["libraries"] = [{**self.edge("tokens"), "visibility": "public"}]
        facade = self.library("facade")
        facade["artifact"]["kind"] = "shared-library"
        facade["dependencies"]["libraries"] = [self.edge("mapping")]
        self.add_package(facade)
        self.files("example.tokens", {"tokens.hpp": "#pragma once\ninline int token_value() { return 42; }\n",
                   "CMakeLists.txt": 'obcx_add_library()\ntarget_include_directories(example_tokens INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")\n'})
        self.files("example.mapping", {"mapping.hpp": "#pragma once\nint mapping_value();\n",
                   "mapping.cpp": '#include "tokens.hpp"\nint mapping_value() { return token_value(); }\n',
                   "CMakeLists.txt": 'obcx_add_library(SOURCES mapping.cpp)\ntarget_include_directories(example_mapping PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")\n'})
        self.facade_cmake = '''add_library(facade_impl OBJECT impl.cpp)
obcx_package_target(facade_impl ROLE implementation)
obcx_add_library(SOURCES facade.cpp)
target_link_libraries(example_facade PRIVATE facade_impl)
if(OBCX_PACKAGE_PROFILE STREQUAL "tests")
  add_executable(facade_check check.cpp)
  obcx_package_target(facade_check ROLE test)
  target_link_libraries(facade_check PRIVATE example::facade)
endif()
'''
        self.files("example.facade", {"impl.cpp": '#include "mapping.hpp"\nint impl_value() { return mapping_value(); }\n',
                   "facade.cpp": "int impl_value();\nint facade_value() { return impl_value(); }\n",
                   "check.cpp": "int facade_value();\nint main() { return facade_value() == 42 ? 0 : 1; }\n",
                   "CMakeLists.txt": self.facade_cmake})
        self.driver = self.root / "driver"
        self.driver.mkdir()
        self.build = self.root / "build"
        (self.driver / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.30)
project(PackageBuildFixture VERSION 1.1.0 LANGUAGES CXX)
# This is a provider/target fixture, not an actor runtime implementation.
add_library(fixture_sdk INTERFACE)
add_library(obcx::obcx_core ALIAS fixture_sdk)
include("{ROOT / 'cmake/OBCXPackages.cmake'}")
obcx_load_packages(WORKSPACE "{self.manifest}"
 CACHE "{self.cache}" MODE development
 STATE_DIR "${{CMAKE_BINARY_DIR}}/package-state")
''')

    def files(self, package, files):
        for filename, content in files.items():
            (self.root / package / filename).write_text(content)

    def prepare(self):
        # Real-build inputs target the runner explicitly; data-only resolver
        # fixtures keep their fixed platform for mismatch tests.
        selected = {"x86_64": "linux-x86_64", "aarch64": "linux-arm64"}[platform.machine()]
        self.workspace["workspace"]["platform"] = selected
        for document in self.packages.values():
            document["artifact"]["platforms"] = [selected]
        self.save()

    def configure(self, configuration):
        self.prepare()
        return subprocess.run(["cmake", "-S", str(self.driver), "-B", str(self.build),
                               "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={configuration}"], capture_output=True, text=True)

    def compile(self):
        return subprocess.run(["cmake", "--build", str(self.build), "--parallel", str(max(6, os.cpu_count() or 6))],
                              capture_output=True, text=True)

    def test_three_library_kinds_with_internal_object_target_build_and_run(self):
        self.workspace["workspace"]["roots"].append("example.actor")
        # Installed SDK receipts can name an already-included module through
        # ../ segments. CMake must track it once, not emit duplicate Ninja rules.
        module = ROOT / "cmake/OBCXActorIdentity.cmake"
        anchor = self.root / "sdk-receipt.json"
        receipt = json.loads(anchor.read_bytes())
        receipt["inputs"].append({"path": os.path.relpath(module, self.root),
                                  "sha256": digest(module.read_bytes())})
        anchor.write_bytes(encoded(receipt))
        self.workspace["providers"][0]["provenance"]["sha256"] = digest(anchor.read_bytes())
        self.files("example.actor", {"CMakeLists.txt": '''add_library(actor_helper STATIC helper.cpp)
obcx_package_target(actor_helper ROLE implementation)
obcx_add_actor(SOURCES actor.cpp)
target_link_libraries(example_actor PRIVATE actor_helper)
''',
                   "actor.cpp": '''#include OBCX_ACTOR_METADATA_HEADER
int mapping_value();
extern "C" const char *helper_identity();
extern "C" int fixture_value() { return mapping_value(); }
extern "C" const char *fixture_identity() { return OBCX_ACTOR_NAME ":" OBCX_ACTOR_VERSION; }
extern "C" const char *fixture_helper_identity() { return helper_identity(); }
''',
                   "helper.cpp": '''#include OBCX_ACTOR_METADATA_HEADER
extern "C" const char *helper_identity() { return OBCX_ACTOR_NAME ":" OBCX_ACTOR_VERSION; }
'''})
        result = self.configure("Release")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(self.build / "package-state/build/example.facade/facade_check")])
        self.assertEqual(result.returncode, 0)
        actor_artifact = self.build / "actors/example_actor.so"
        self.assertTrue(actor_artifact.is_file())

        def check_identity():
            # Separate processes avoid the dynamic loader reusing the old image.
            expected = self.packages["example.actor"]["actor"]["name"] + ":" + self.packages["example.actor"]["package"]["version"]
            probe = subprocess.run([sys.executable, "-c", '''import ctypes,sys
actor = ctypes.CDLL(sys.argv[1])
for name in ("fixture_identity", "fixture_helper_identity"):
    function = getattr(actor, name)
    function.restype = ctypes.c_char_p
    assert function().decode() == sys.argv[2]
assert actor.fixture_value() == 42
''', str(actor_artifact), expected], capture_output=True, text=True)
            self.assertEqual(probe.returncode, 0, probe.stdout + probe.stderr)

        check_identity()

        # Reuse the built fixture to verify incrementality and metadata tracking.
        objects = list(self.build.rglob("*.o"))
        self.assertTrue(objects)
        generated = list(self.build.rglob("actor_*.hpp"))
        self.assertEqual(len(generated), 2)
        outputs = {p: p.stat().st_mtime_ns for p in [*objects, *generated,
                   self.build / "actors/example_actor.so",
                   self.build / "package-state/build/example.facade/facade_check"]}
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(outputs, {p: p.stat().st_mtime_ns for p in outputs})
        self.packages["example.mapping"]["package"]["version"] = "0.1.1"
        self.packages["example.actor"]["package"]["version"] = "0.1.1"
        self.save()
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        graph = json.loads((self.build / "package-state/current-graph.json").read_bytes())
        mapping = next(p for p in graph["packages"] if p["id"] == "example.mapping")
        self.assertEqual(mapping["version"], "0.1.1")
        check_identity()

        self.workspace["workspace"]["profile"] = "production"
        self.build = self.root / "build-production"
        result = self.configure("Release")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.build / "package-state/build/example.facade/facade_check").exists())

    def test_internal_target_cannot_hide_undeclared_direct_dependency(self):
        self.files("example.facade", {"CMakeLists.txt": self.facade_cmake + '\ntarget_link_libraries(facade_impl PRIVATE example::tokens)\n'})
        result = self.configure("Release")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("undeclared direct dependency", " ".join((result.stdout + result.stderr).split()))

    def test_active_generator_expression_is_a_build_gate(self):
        self.files("example.facade", {"CMakeLists.txt": self.facade_cmake + '\ntarget_link_libraries(facade_impl PRIVATE "$<$<CONFIG:Debug>:example::tokens>")\n'})
        result = self.configure("Debug")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("undeclared direct dependency", result.stdout + result.stderr)
        self.assertFalse(list((self.build / "package-state/build").rglob("*.o")))

    def test_artifact_identity_and_pic_are_checked_after_package_cmake(self):
        self.files("example.mapping", {"CMakeLists.txt": 'obcx_add_library(SOURCES mapping.cpp)\nset_target_properties(example_mapping PROPERTIES POSITION_INDEPENDENT_CODE OFF)\n'})
        result = self.configure("Release")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PIC is required", result.stdout + result.stderr)

    def test_test_only_dependency_cannot_enter_internal_production_target(self):
        helper = self.library("testing")
        helper["artifact"]["kind"] = "header-only"
        self.add_package(helper)
        self.files("example.testing", {"CMakeLists.txt": "obcx_add_library()\n"})
        self.packages["example.facade"]["test_dependencies"]["libraries"] = [self.edge("testing")]
        self.files("example.facade", {"CMakeLists.txt": self.facade_cmake + '\ntarget_link_libraries(facade_impl PRIVATE example::testing)\n'})
        result = self.configure("Release")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("undeclared direct dependency", " ".join((result.stdout + result.stderr).split()))

    def test_source_specific_flags_cannot_hide_foreign_includes(self):
        foreign = self.root / "undeclared-headers"
        foreign.mkdir()
        self.files("example.facade", {"CMakeLists.txt": self.facade_cmake +
                   f'\nset_source_files_properties(impl.cpp PROPERTIES COMPILE_OPTIONS "-I{foreign}")\n'})
        result = self.configure("Release")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.compile()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("generated raw include/toolchain flag", result.stdout + result.stderr)

    def test_explicit_empty_roots_do_not_configure_packages(self):
        self.workspace["workspace"]["roots"] = []
        result = self.configure("Release")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse((self.build / "package-state/build").exists())


if __name__ == "__main__":
    unittest.main()
