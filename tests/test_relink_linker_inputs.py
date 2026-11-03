"""Release companions must bind the Cardputer linker inputs and exact output."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_relink as relink
import package_source


class CardputerLinkerCompanionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lora-relink-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.output = self.build / "cardputer_linker"
        self.output.mkdir(parents=True)
        (self.root / "tools").mkdir()
        policy = self.root / "linker/cardputer/esp-idf-v5.5.5"
        policy.mkdir(parents=True)
        for name in ("freertos.lf", "esp_ringbuf.lf", "provenance.json"):
            (policy / name).write_text("pinned fixture")
        for name in relink.CARDPUTER_BUILD_SCRIPTS:
            (self.root / name).write_text("fixture input")
        # This adapter isolates collection/binding logic; the actual generator
        # has independent tests against the pinned archives and placement policy.
        (self.root / "tools/cardputer_linker.py").write_text(
            "import hashlib,json\nfrom pathlib import Path\n"
            "ROOT=Path(__file__).resolve().parents[1]\n"
            "def generate(sdk,objdump,output):\n"
            " output.mkdir(parents=True,exist_ok=True)\n"
            " report=json.loads((ROOT/'expected-report.json').read_text())\n"
            " (output/'sections.ld').write_bytes(b'exact portable linker script')\n"
            " (output/'report.json').write_text(json.dumps(report))\n"
            " return report\n"
            "def verify(output,map_path,elf_path):\n"
            " return {'map_sha256':hashlib.sha256(map_path.read_bytes()).hexdigest(),"
            "'elf_sha256':hashlib.sha256(elf_path.read_bytes()).hexdigest()}\n")
        self.report = {"schema": 1, "target": relink.CARDPUTER_ENV, "generator_sha256": "fixture"}
        (self.root / "expected-report.json").write_text(json.dumps(self.report))
        (self.output / "report.json").write_text(json.dumps(self.report))
        (self.output / "sections.ld").write_bytes(b"exact portable linker script")
        (self.build / "firmware.map").write_bytes(b"verified map")
        (self.build / "firmware.elf").write_bytes(b"matching elf")
        self.verification = {
            "map_sha256": hashlib.sha256(b"verified map").hexdigest(),
            "elf_sha256": hashlib.sha256(b"matching elf").hexdigest(),
        }
        (self.output / "verification.json").write_text(json.dumps(self.verification))

    def collect(self):
        return relink.cardputer_linker_entries(self.root, self.build)

    def test_complete_portable_inventory_and_no_evidence_mutation(self):
        before = {p.name: p.read_bytes() for p in self.output.iterdir()}
        entries = self.collect()
        names = {name for name, _ in entries}
        for name in relink.CARDPUTER_BUILD_SCRIPTS:
            self.assertIn("build-inputs/" + name, names)
        for name in relink.CARDPUTER_LINKER_OUTPUTS:
            self.assertIn("build-metadata/cardputer_linker/" + name, names)
        self.assertIn("build-inputs/linker/cardputer/esp-idf-v5.5.5/freertos.lf", names)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.output.iterdir()})
        self.assertFalse(any(str(self.root) in name for name in names))

    def test_generated_script_drift_rejected(self):
        (self.output / "sections.ld").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "no longer matches"):
            self.collect()

    def test_source_report_drift_rejected(self):
        (self.root / "expected-report.json").write_text(json.dumps({**self.report, "generator_sha256": "new"}))
        with self.assertRaisesRegex(ValueError, "no longer matches"):
            self.collect()

    def test_wrong_build_elf_or_map_rejected(self):
        for name in ("firmware.elf", "firmware.map"):
            with self.subTest(name=name):
                path = self.build / name
                before = path.read_bytes()
                path.write_bytes(b"different build")
                with self.assertRaisesRegex(ValueError, "exact ELF/map"):
                    self.collect()
                path.write_bytes(before)

    def test_missing_post_link_verification_rejected(self):
        (self.output / "verification.json").unlink()
        with self.assertRaisesRegex(FileNotFoundError, "verification.json"):
            self.collect()

    def test_source_archive_contains_linker_rebuild_inputs(self):
        names = {p.relative_to(ROOT).as_posix() for p in package_source.collect_paths(ROOT)}
        self.assertTrue(set(relink.CARDPUTER_BUILD_SCRIPTS) <= names)
        for name in ("freertos.lf", "esp_ringbuf.lf", "provenance.json"):
            self.assertIn("linker/cardputer/esp-idf-v5.5.5/" + name, names)

    def test_application_source_hashes_bind_cardputer_build_inputs_only(self):
        card = relink.application_hashes(ROOT, relink.CARDPUTER_ENV)
        heltec = relink.application_hashes(ROOT, relink.ENV)
        for name in relink.CARDPUTER_BUILD_SCRIPTS:
            self.assertIn(name, card)
            self.assertNotIn(name, heltec)
        self.assertIn("linker/cardputer/esp-idf-v5.5.5/provenance.json", card)
        self.assertNotIn("linker/cardputer/esp-idf-v5.5.5/provenance.json", heltec)

    def test_heltec_collector_does_not_require_cardputer_linker(self):
        framework = self.root / ".pio-core/packages/framework-arduinoespressif32"
        framework.mkdir(parents=True)
        (framework / "package.json").write_text(json.dumps({"version": "3.3.12"}))
        for name in relink.LIBRARIES:
            (self.root / ".pio/libdeps" / relink.ENV / name).mkdir(parents=True)
        platform = self.root / ".pio-core/platforms/espressif32/platform.json"
        platform.parent.mkdir(parents=True)
        platform.write_text("{}")
        with patch.object(relink, "cardputer_linker_entries", side_effect=AssertionError("Card hook used")):
            entries = relink.collect(self.root, relink.ENV)
        self.assertFalse(any("cardputer_linker" in name for name, _ in entries))


if __name__ == "__main__":
    unittest.main()
