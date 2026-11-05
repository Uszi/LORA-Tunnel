"""Offline release-assembly checks. No device or publishing calls."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import zipfile

try:
    import markdown
except ImportError:
    raise unittest.SkipTest("Install tools/site-requirements.txt for web release checks")

SPEC = importlib.util.spec_from_file_location("build_web", Path(__file__).parents[1] / "tools/build_web.py")
web = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(web)


class WebReleaseChecks(unittest.TestCase):
    def archive(self, path, entries):
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in entries.items():
                archive.writestr("LORA-Tunnel/" + name, data)
            archive.writestr("LORA-Tunnel/SOURCE-SHA256.json", json.dumps({name:hashlib.sha256(data.encode()).hexdigest() for name,data in entries.items()}))

    def image(self):
        data = bytearray(0x10000 + 512)
        data[0], data[2], data[3], data[12] = 0xE9, 2, 0x4F, 9
        data[0x10000] = 0xE9
        data[0x10080:0x10080 + 13] = b"0.2.2-beta.1\0"
        return bytes(data)

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
