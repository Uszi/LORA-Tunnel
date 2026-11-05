"""Offline release-assembly checks. No device or publishing calls."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import struct
import zipfile

try:
    import markdown
except ImportError:
    raise unittest.SkipTest("Install tools/site-requirements.txt for web release checks")

SPEC = importlib.util.spec_from_file_location("build_web", Path(__file__).parents[1] / "tools/build_web.py")
web = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(web)


class WebReleaseChecks(unittest.TestCase):
    def test_shared_sd_starter_is_reusable_but_must_agree(self):
        sources = {name: b"synthetic" for name in web.SAFE_SD}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "starter.zip"
            web.write_sd_starter(path, sources)
            original = path.read_bytes()
            web.write_sd_starter(path, sources)
            self.assertEqual(path.read_bytes(), original)
            sources[next(iter(sources))] = b"changed"
            with self.assertRaisesRegex(ValueError, "disagree"):
                web.write_sd_starter(path, sources)
            self.assertEqual(path.read_bytes(), original)

    def archive(self, path, entries):
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in entries.items():
                archive.writestr("LORA-Tunnel/" + name, data)
            archive.writestr("LORA-Tunnel/SOURCE-SHA256.json", json.dumps({name:hashlib.sha256(data.encode()).hexdigest() for name,data in entries.items()}))

    def image(self):
        def segment(payload):
            header = bytearray(24)
            header[0], header[1], header[2], header[3], header[12], header[23] = 0xe9, 1, 2, 0x4f, 9, 1
            data = header + struct.pack("<II", 0x3c000020, len(payload)) + payload
            checksum = 0xef
            for value in payload:
                checksum ^= value
            data += bytes((15 - len(data) % 16) % 16) + bytes([checksum])
            return bytes(data) + hashlib.sha256(data).digest()
        boot = segment(b"synthetic bootloader")
        app = segment(b"heltec-v4-r8-tft\0" + b"0.2.2-beta.1\0" + bytes(256))
        return boot + b"\xff" * (0x10000-len(boot)) + app

    def test_application_corruption_rejected_even_when_outer_hash_matches(self):
        data = bytearray(self.image())
        data[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "image SHA-256"):
            web.validate_factory(data, "0.2.2-beta.1", hashlib.sha256(data).hexdigest())

    def test_stable_source_archive_with_empty_credentials(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "source.zip"
            self.archive(path, {"include/version.h": '#define TUNNEL_VERSION "0.5.0"',
                "sdcard/lora-tunnel/config.example.json": json.dumps({"credentials": {}, "lora": {"meshtastic_keys": []}}),
                "sdcard/lora-tunnel/detection_rules.json": json.dumps({"schema": 1, "rules": []})})
            self.assertIn("include/version.h", web.read_source(path, "0.5.0"))

    def test_expected_factory_and_source_version(self):
        data = self.image()
        web.validate_factory(data, "0.2.2-beta.1", hashlib.sha256(data).hexdigest())

    def test_modified_download_is_rejected(self):
        data = self.image()
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            web.validate_factory(data + b"corrupt", "0.2.2-beta.1", hashlib.sha256(data).hexdigest())

    def test_wrong_chip_flash_size_or_mode_is_rejected(self):
        for offset, value in [(0, 0), (2, 0), (3, 0x3F), (12, 0)]:
            data = bytearray(self.image())
            data[offset] = value
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "boot header"):
                web.validate_factory(data, "0.2.2-beta.1", hashlib.sha256(data).hexdigest())

    def test_different_application_version_is_rejected(self):
        data = self.image()
        with self.assertRaisesRegex(ValueError, "application/version"):
            web.validate_factory(data, "0.2.3-beta.1", hashlib.sha256(data).hexdigest())

    def test_source_mismatch_and_unsafe_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "source.zip"
            self.archive(p, {"include/version.h": '#define TUNNEL_VERSION "0.2.1-beta.1"'})
            with self.assertRaisesRegex(ValueError, "Source version"):
                web.read_source(p, "0.2.2-beta.1")
            with zipfile.ZipFile(p, "a") as archive:
                archive.writestr("../private.json", "{}")
            with self.assertRaisesRegex(ValueError, "Unsafe source"):
                web.read_source(p, "0.2.2-beta.1")

    def test_nonempty_credentials_are_rejected_without_disclosure(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "source.zip"
            self.archive(p, {"include/version.h": '#define TUNNEL_VERSION "0.2.2-beta.1"',
                             "sdcard/lora-tunnel/config.example.json": json.dumps({"credentials":{"token":"never-print-this"}})})
            with self.assertRaisesRegex(ValueError, "credentials must be empty") as result:
                web.read_source(p, "0.2.2-beta.1")
            self.assertNotIn("never-print-this", str(result.exception))

    def test_mesh_keys_are_rejected_without_disclosure(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "source.zip"
            self.archive(p, {"include/version.h": '#define TUNNEL_VERSION "0.2.2-beta.1"',
                             "sdcard/lora-tunnel/config.example.json": json.dumps({
                                 "credentials": {}, "lora": {"meshtastic_keys": [
                                     {"name": "test", "key_hex": "never-print-this"}]}})})
            with self.assertRaisesRegex(ValueError, "mesh keys must be empty") as result:
                web.read_source(p, "0.2.2-beta.1")
            self.assertNotIn("never-print-this", str(result.exception))

    def test_manifest_hash_and_inventory_must_match(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "source.zip"
            with zipfile.ZipFile(p, "w") as archive:
                archive.writestr("LORA-Tunnel/include/version.h", '#define TUNNEL_VERSION "0.2.2-beta.1"')
                archive.writestr("LORA-Tunnel/SOURCE-SHA256.json", json.dumps({"include/version.h":"0"*64}))
            with self.assertRaisesRegex(ValueError, "manifest hash mismatch"):
                web.read_source(p, "0.2.2-beta.1")
            with zipfile.ZipFile(p, "a") as archive:
                archive.writestr("LORA-Tunnel/unlisted.txt", "unexpected")
            with self.assertRaisesRegex(ValueError, "inventory mismatch"):
                web.read_source(p, "0.2.2-beta.1")

    def test_duplicate_entries_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / "source.zip"
            self.archive(p, {"include/version.h": '#define TUNNEL_VERSION "0.2.2-beta.1"'})
            with zipfile.ZipFile(p, "a") as archive, self.assertWarns(UserWarning):
                archive.writestr("LORA-Tunnel/include/version.h", "replaced")
            with self.assertRaisesRegex(ValueError, "Duplicate source archive"):
                web.read_source(p, "0.2.2-beta.1")

    def test_git_blob_binding_rejects_changed_same_version_content(self):
        # This is Git's published blob hash for the literal bytes 'hello\n'.
        tree = b"100644 blob ce013625030ba8dba906f756967f9e9ca394464a\tREADME.md\0"
        with patch.object(web.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout=tree)):
            web.validate_commit({"README.md":b"hello\n"}, "1"*40)
            with self.assertRaisesRegex(ValueError, "differs from.*Git commit"):
                web.validate_commit({"README.md":b"changed\n"}, "1"*40)

    def test_local_link_traversal_and_missing_targets_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            page = root / "index.html"
            for link in ["../outside.txt", "missing.html"]:
                page.write_text('<a href="' + link + '">link</a>')
                with self.subTest(link=link), self.assertRaisesRegex(ValueError, "Missing local link"):
                    web.check_links(root)


if __name__ == "__main__":
    unittest.main()
