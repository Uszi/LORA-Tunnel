"""Exact-policy and fail-closed tests; hardware/build execution is never performed."""
import copy
import json
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cardputer_linker as linker

SDK = ROOT / ".pio-core/packages/framework-arduinoespressif32-libs/esp32s3"
OBJDUMP = ROOT / ".pio-core/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump"
if not OBJDUMP.exists():
    OBJDUMP = OBJDUMP.with_suffix(".exe")

class PolicyTests(unittest.TestCase):
    def test_no_function_prefix_or_isr_capture(self):
        policy = {("queue", "xQueueReceive")}
        self.assertTrue(linker.may_move("queue.c.obj", ".text.xQueueReceive", policy))
        self.assertTrue(linker.may_move("queue.c.obj", ".literal.xQueueReceive$constprop$0", policy))
        for section in [".text.xQueueReceiveFromISR", ".text.xQueueReceiveExtra", ".iram1.xQueueReceive", ".text.xQueueReceive$unknown$0"]:
            self.assertFalse(linker.may_move("queue.c.obj", section, policy))
        self.assertFalse(linker.may_move("other.c.obj", ".text.xQueueReceive", policy))

    def test_condition_parser_is_not_code_execution(self):
        self.assertTrue(linker.condition("(CORE = y) && (COUNT > 0)", {"CORE": "y", "COUNT": "2"}))
        self.assertFalse(linker.condition("CORE = y", {}))
        with self.assertRaises(RuntimeError):
            linker.condition("__import__('os')", {})

    def test_official_sources_are_exact_and_isr_branch_disabled(self):
        manifest = json.loads((linker.POLICY / "provenance.json").read_text())
        for name, meta in manifest["sources"].items():
            data = (linker.POLICY / name).read_bytes()
            self.assertEqual(linker.digest(data), meta["sha256"])
            entries = linker.whitelist(data.decode(), manifest["policy"])
            self.assertTrue(entries)
            self.assertFalse(any("FromISR" in fn or fn in linker.CRITICAL for _, fn in entries))

    def test_explicit_isr_or_cache_off_policy_is_rejected(self):
        for name in ["xQueueReceiveFromISR", "xTaskGetTickCount", "vTaskSwitchContext"]:
            with self.assertRaises(RuntimeError):
                linker.whitelist("queue:" + name + " (default)", {})

    def test_link_flag_replacement_is_exact_and_keeps_other_scripts(self):
        flags = ["-T", "memory.ld", "-T", "sections.ld", "-Wl,--gc-sections"]
        result = linker.replace_link_flags(flags, Path("build space/cardputer/sections.ld"))
        self.assertEqual(result, ["-T", "memory.ld", "-T", "build space/cardputer/sections.ld", "-Wl,--gc-sections"])
        self.assertEqual(flags[3], "sections.ld")
        for invalid in [[], ["sections.ld"], ["-T", "sections.ld", "-T", "sections.ld"]]:
            with self.assertRaises(RuntimeError):
                linker.replace_link_flags(invalid, Path("generated.ld"))

    def test_heltec_does_not_select_hook(self):
        import configparser
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini")
        self.assertFalse(config.has_option("env:heltec-v4-r8-tft", "extra_scripts"))
        self.assertEqual(config["env:cardputer-adv-lora1262"]["extra_scripts"].split(), ["pre:tools/cardputer_build.py", "post:tools/cardputer_linker_pio.py"])

    def test_map_parser_ignores_discarded_and_supports_line_wraps(self):
        text = " .text.keep 0x00000000 0x10 any/libfreertos.a(tasks.c.obj)\n .text.move\n 0x42001000 0x20 any/libfreertos.a(queue.c.obj)\n"
        self.assertEqual(linker.map_sections(text), {("libfreertos.a", "queue.c.obj", ".text.move"): (0x42001000, 32)})

@unittest.skipUnless(SDK.exists() and OBJDUMP.exists(), "pinned SDK/toolchain installed by firmware build")
class SdkIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="lora-linker-test-")
        self.directory = Path(self.tmp.name)
        self.output = self.directory / "generated"
        self.report = linker.generate(SDK, OBJDUMP, self.output)

    def tearDown(self):
        self.tmp.cleanup()

    def test_deterministic_real_sdk_output_and_critical_sections(self):
        before = (self.output / "sections.ld").read_bytes()
        self.assertEqual(linker.generate(SDK, OBJDUMP, self.output), self.report)
        self.assertEqual((self.output / "sections.ld").read_bytes(), before)
        self.assertEqual(len(self.report["moved"]), 320)
        self.assertEqual(self.report["archive_movable_bytes"], 22889)
        moved = {linker.section_function(row["section"]) for row in self.report["moved"]}
        self.assertFalse(moved & linker.CRITICAL)
        retained = {linker.section_function(row["section"]) for row in self.report["retained_iram"]}
        self.assertTrue({"xTaskGetSchedulerState", "xTaskGetTickCount", "vTaskSwitchContext", "xTaskIncrementTick"} <= retained)
        self.assertNotIn("*libesp_ringbuf.a:(.literal .literal.* .text .text.*)", before.decode())
        self.assertIn("*libfreertos.a:queue.c.obj(", before.decode())

    def test_source_or_archive_inventory_drift_fails_closed(self):
        policy = self.directory / "policy"
        shutil.copytree(linker.POLICY, policy)
        with (policy / "freertos.lf").open("ab") as stream:
            stream.write(b"\n# altered\n")
        with self.assertRaisesRegex(RuntimeError, "whitelist hash drift"):
            linker.generate(SDK, OBJDUMP, self.output, policy)
        with patch.object(linker, "archive_sections", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "inventory drift"):
                linker.generate(SDK, OBJDUMP, self.output)

    def test_sdk_hash_drift_fails_before_archives_or_output(self):
        fake = self.directory / "sdk/esp32s3"
        (fake / "qio_qspi").mkdir(parents=True)
        shutil.copyfile(SDK.parent / "package.json", fake.parent / "package.json")
        (fake / "qio_qspi/sections.ld").write_text("changed")
        with self.assertRaisesRegex(RuntimeError, "SDK input hash drift"):
            linker.generate(fake, OBJDUMP, self.output)

    def make_map(self):
        lines = []
        for field, address in [("moved", 0x42001000), ("retained_iram", 0x40375000)]:
            for row in self.report[field]:
                lines.append(" %s\n 0x%x 0x%x sdk/%s(%s)\n" % (row["section"], address, row["bytes"], row["archive"], row["object"]))
                address += (row["bytes"] + 3) & ~3
        path = self.directory / "firmware.map"
        path.write_text("".join(lines))
        elf = self.directory / "firmware.elf"
        elf.write_bytes(b"synthetic placement fixture, never firmware")
        return path, elf

    def test_map_verifier_binds_outputs_and_refuses_wrong_placement(self):
        path, elf = self.make_map()
        result = linker.verify(self.output, path, elf)
        self.assertEqual(result["moved_bytes"], 22889)
        self.assertEqual(result["elf_sha256"], linker.digest(elf.read_bytes()))
        self.assertEqual(result["map_sha256"], linker.digest(path.read_bytes()))
        text = path.read_text().replace("0x42001000", "0x40371000", 1)
        path.write_text(text)
        with self.assertRaisesRegex(RuntimeError, "wrong placement"):
            linker.verify(self.output, path, elf)

    def test_map_verifier_rejects_critical_function_in_flash(self):
        path, elf = self.make_map()
        lines = path.read_text().splitlines()
        for i, line in enumerate(lines):
            if line.strip() == ".text.xTaskGetTickCount":
                parts = lines[i+1].split()
                parts[0] = "0x42070000"
                lines[i+1] = " " + " ".join(parts)
        path.write_text("\n".join(lines))
        with self.assertRaisesRegex(RuntimeError, "wrong placement"):
            linker.verify(self.output, path, elf)

    def test_platformio_hook_replaces_only_link_flag_and_installs_verifier(self):
        class Platform:
            def get_package_dir(self, package):
                return str(SDK.parent if package == "framework-arduinoespressif32-libs" else OBJDUMP.parent.parent)
        class Env(dict):
            def subst(inner, value):
                return {"$PROJECT_DIR": str(ROOT), "$PIOENV": "cardputer-adv-lora1262", "$BUILD_DIR": str(self.directory / "pio"), "$BUILD_DIR/${PROGNAME}.elf": str(self.directory / "pio/firmware.elf")}[value]
            def PioPlatform(inner): return Platform()
            def Replace(inner, **values): inner.update(values)
            def Depends(inner, target, sources): inner["dependencies"] = sources
            def AddPostAction(inner, target, action): inner["verify_action"] = action
        environment = Env(LINKFLAGS=["-T", "memory.ld", "-T", "sections.ld"])
        runpy.run_path(str(ROOT / "tools/cardputer_linker_pio.py"), init_globals={"env": environment, "Import": lambda _: None})
        self.assertEqual(environment["LINKFLAGS"][:2], ["-T", "memory.ld"])
        self.assertTrue(Path(environment["LINKFLAGS"][3]).is_file())
        self.assertTrue(callable(environment["verify_action"]))
        self.assertGreaterEqual(len(environment["dependencies"]), 6)

if __name__ == "__main__":
    unittest.main()
