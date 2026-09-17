from __future__ import annotations

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
INCLUDE = ROOT / "include"
INCLUDES = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
PLATFORMS = ("onebot11", "telegram")


def sources(root: Path) -> list[Path]:
    return sorted(p for p in root.rglob("*") if p.suffix in {".cpp", ".hpp"})


def closure(roots: list[Path]) -> set[Path]:
    visited: set[Path] = set()
    pending = list(roots)
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        for include in INCLUDES.findall(path.read_text()):
            target = INCLUDE / include
            if target.is_file():
                pending.append(target)
    return visited


class BotPlatformModularityTest(unittest.TestCase):
    def test_generic_runtime_has_no_platform_imports_or_closed_contracts(self) -> None:
        generic = sum((sources(ROOT / area) for area in
                       ("include/core", "src/core", "include/common", "src/common")), [])
        banned = re.compile(r'\b(?:BotSurface|BotAction|BotInstallationSurface|BotConnectionConfig|BotOperationClient)\b')
        for path in generic:
            text = path.read_text()
            with self.subTest(path=str(path.relative_to(ROOT))):
                self.assertNotRegex(text, banned)
                self.assertFalse(any(i.startswith(tuple(p + "/" for p in PLATFORMS))
                                     for i in INCLUDES.findall(text)))
                self.assertNotIn('"test.echo"', text)

    def test_platform_implementation_does_not_import_its_peer(self) -> None:
        for platform, peer in (("onebot11", "telegram"), ("telegram", "onebot11")):
            for path in sources(ROOT / "include" / platform) + sources(ROOT / "src" / platform):
                with self.subTest(path=str(path.relative_to(ROOT))):
                    self.assertFalse(any(i.startswith(peer + "/")
                                         for i in INCLUDES.findall(path.read_text())))

    def test_sdk_include_closures_are_allowlisted(self) -> None:
        common = {INCLUDE / "core/bot" / (name + ".hpp") for name in (
            "ids", "json_codec", "validation", "references", "operation_error", "operation_result",
            "operation_traits", "messaging", "gateway_codec", "operation_gateway", "typed_operation", "messaging_client")}
        common |= {INCLUDE / "common" / (name + ".hpp") for name in ("message_type", "json_utils")}
        self.assertLessEqual(closure(list(common)), common)
        for platform in PLATFORMS:
            platform_headers = {INCLUDE / platform / "bot" / (name + ".hpp")
                                for name in ("actions", "types", "operations", "client")}
            self.assertLessEqual(closure(list(platform_headers)), common | platform_headers)
        messaging = (INCLUDE / "common/message_type.hpp").read_text()
        self.assertNotRegex(messaging, r'\b(?:ConnectionConfig|access_token|proxy_password)\b')

    def test_cmake_targets_preserve_runtime_and_sdk_direction(self) -> None:
        cmake = (ROOT / "src/CMakeLists.txt").read_text()
        for name in ("GENERIC", "ONEBOT11", "TELEGRAM"):
            block = re.search(rf'set\(OBCX_{name}_RUNTIME_SOURCES\s+(.*?)\)', cmake, re.DOTALL)
            self.assertIsNotNone(block)
            paths = block.group(1).split()
            self.assertTrue(paths)
            for platform in PLATFORMS:
                if name.lower() != platform:
                    self.assertFalse(any(p.startswith(platform + "/") for p in paths))
        for target in ("bot_common_sdk", "bot_onebot11_sdk", "bot_telegram_sdk"):
            self.assertIn(target, cmake)
        self.assertNotIn("test.echo", cmake)
        self.assertNotIn("echo_module", cmake)
        self.assertNotIn("process_configuration.hpp", cmake[cmake.index("# ===== Install rules ====="):])
        composition = (ROOT / "src/app/builtin_bot_platforms.hpp").read_text()
        self.assertIn("onebot11::bot::register_recipes", composition)
        self.assertIn("telegram::bot::register_recipes", composition)
        self.assertNotIn("test.echo", composition)


if __name__ == "__main__":
    unittest.main()
