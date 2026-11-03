"""Offline image-integrity and physical-release gate regression checks."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location("cardputer_package", Path(__file__).parents[1] / "tools/package_cardputer.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


def image(payload):
    header = bytearray(24)
    header[0], header[1], header[2], header[3], header[12], header[23] = 0xe9, 1, 2, 0x3f, 9, 1
    data = header + struct.pack("<II", 0x3c000020, len(payload)) + payload
    checksum = 0xef
    for value in payload:
        checksum ^= value
    data += b"\x00" * ((len(data) | 15) - len(data)) + bytes([checksum])
    return bytes(data) + hashlib.sha256(data).digest()


def partition_table():
    data = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, offset, size, label.encode(), 0)
                    for kind, subtype, offset, size, label in package.PARTITIONS)
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data.ljust(0xc00, b"\xff")


def factory(app=None):
    app = app or image(b"cardputer-adv-lora1262\0" + b"0.4.0-beta.1\0" + b"test payload" * 30)
    boot = image(b"synthetic bootloader")
    data = bytearray(b"\xff" * 0x10000)
    data[:len(boot)] = boot
    data[0x8000:0x8c00] = partition_table()
    return bytes(data) + app, app


class CardputerPackageChecks(unittest.TestCase):
    def test_matching_factory_and_relocatable_app(self):
        data, app = factory()
        self.assertEqual(package.validate_factory(data, "0.4.0-beta.1", app), app)

    def test_wrong_board_same_version_is_rejected(self):
        data, _ = factory(image(b"heltec-v4-r8-tft\0" + b"0.4.0-beta.1\0"))
        with self.assertRaisesRegex(ValueError, "target identity"):
            package.validate_factory(data, "0.4.0-beta.1")

    def test_raw_app_is_not_factory(self):
        _, app = factory()
        with self.assertRaisesRegex(ValueError, "factory length"):
            package.validate_factory(app, "0.4.0-beta.1")

    def test_corrupt_app_and_wrong_version_are_rejected(self):
        _, app = factory()
        corrupt = bytearray(app)
        corrupt[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            package.validate_app(corrupt, "0.4.0-beta.1")
        with self.assertRaisesRegex(ValueError, "application/version"):
            package.validate_app(app, "0.4.1-beta.1")

    def test_partition_oversize_wrong_flash_and_private_nvs_fail(self):
        with self.assertRaisesRegex(ValueError, "exceeds"):
            package.validate_app(b"\0" * (package.APP_LIMIT + 1), "0.4.0-beta.1")
        for offset, value, error in ((3, 0x4f, "8 MB"), (0x9000, 0, "NVS"), (0x800c, 0, "partition")):
            data, _ = factory()
            data = bytearray(data)
            data[offset] = value
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, error):
                package.validate_factory(data, "0.4.0-beta.1")

    def test_hardware_gate_binds_all_checks_to_exact_artifacts(self):
        record = {"target": package.TARGET, "result": "passed", "factory_sha256": "a" * 64,
                  "app_sha256": "b" * 64, "tested_at": "2026-11-02", "launcher_version": "test",
                  "checks": {name: True for name in package.ACCEPTANCE_CHECKS}}
        package.validate_acceptance(record, "a" * 64, "b" * 64)
        with self.assertRaisesRegex(ValueError, "match"):
            package.validate_acceptance(record, "c" * 64, "b" * 64)
        record["checks"]["launcher_return"] = False
        with self.assertRaisesRegex(ValueError, "incomplete"):
            package.validate_acceptance(record, "a" * 64, "b" * 64)
        with self.assertRaisesRegex(ValueError, "pending"):
            package.validate_acceptance({}, "a" * 64, "b" * 64)


if __name__ == "__main__":
    unittest.main()
