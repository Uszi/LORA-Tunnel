#!/usr/bin/env python3
"""Package clean Cardputer build outputs locally; never read a device or publish."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

TARGET = "cardputer-adv-lora1262"
FLASH_BYTES = 0x800000
APP_LIMIT = 0x330000
PARTITIONS = [
    (1, 2, 0x9000, 0x5000, "nvs"),
    (1, 0, 0xe000, 0x2000, "otadata"),
    (0, 0x10, 0x10000, APP_LIMIT, "app0"),
    (0, 0x11, 0x340000, APP_LIMIT, "app1"),
    (1, 0x82, 0x670000, 0x180000, "spiffs"),
    (1, 3, 0x7f0000, 0x10000, "coredump"),
]
ACCEPTANCE_CHECKS = (
    "factory_boot", "keyboard_display", "lora_receive", "gps_fix",
    "sd_readback", "cold_start_region", "power_wake", "sustained_capture",
    "web_flash", "launcher_install", "launcher_return",
    "upload_idle_resume", "wigle_upload", "wardrift_upload", "wdgwars_upload",
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_image(data):
    """Check ESP32-S3 image segments, XOR checksum and appended SHA-256."""
    require(len(data) >= 24 and data[0] == 0xe9 and data[12:14] == b"\x09\x00",
            "Expected ESP32-S3 image header")
    require(1 <= data[1] <= 16, "Invalid ESP image segment count")
    offset, checksum = 24, 0xef
    for _ in range(data[1]):
        require(offset + 8 <= len(data), "Truncated segment header")
        size = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        require(offset + size <= len(data), "Truncated segment")
        for value in data[offset:offset + size]:
            checksum ^= value
        offset += size
    checksum_offset = offset | 15
    require(checksum_offset < len(data) and data[checksum_offset] == checksum,
            "Invalid ESP image checksum")
    end = checksum_offset + 1
    require(data[23] == 1 and len(data) >= end + 32, "Missing ESP image SHA-256")
    require(hashlib.sha256(data[:end]).digest() == data[end:end + 32],
            "Invalid ESP image SHA-256")
    return end + 32


def validate_app(data, version):
    require(len(data) <= APP_LIMIT, "Application exceeds 0x330000-byte partition")
    end = validate_image(data)
    require(end == len(data), "Unexpected bytes after application image")
    require(TARGET.encode() + b"\0" in data, "Missing Cardputer target identity")
    require(version.encode() + b"\0" in data, "Missing application/version")


def validate_partitions(data):
    found = []
    for offset in range(0, min(len(data), 0xc00), 32):
        entry = data[offset:offset + 32]
        if len(entry) < 32 or entry[:2] in (b"\xff\xff", b"\xeb\xeb"):
            break
        magic, kind, subtype, start, size, label, flags = struct.unpack("<HBBII16sI", entry)
        require(magic == 0x50aa and flags == 0, "Invalid partition entry")
        found.append((kind, subtype, start, size, label.rstrip(b"\0").decode("ascii")))
    require(found == PARTITIONS, "Cardputer requires the exact default_8MB partition table")
    marker = len(found) * 32
    require(data[marker:marker + 16] == b"\xeb\xeb" + b"\xff" * 14,
            "Missing partition-table MD5 marker")
    require(data[marker + 16:marker + 32] == hashlib.md5(data[:marker]).digest(),
            "Invalid partition-table MD5")


def validate_factory(data, version, application=None):
    require(0x10000 < len(data) <= FLASH_BYTES, "Invalid Cardputer factory length")
    require(data[2:4] == b"\x02\x3f", "Expected 8 MB DIO / 80 MHz factory boot header")
    boot_end = validate_image(data)
    require(boot_end <= 0x8000, "Bootloader overlaps partition table")
    require(data[boot_end:0x8000] == b"\xff" * (0x8000 - boot_end), "Non-erased boot gap")
    validate_partitions(data[0x8000:0x8c00])
    require(data[0x8c00:0xe000] == b"\xff" * (0xe000 - 0x8c00), "Factory NVS must be blank")
    app = data[0x10000:]
    validate_app(app, version)
    if application is not None:
        require(app == application, "Factory application differs from Launcher application")
    return app


def validate_acceptance(record, factory_sha, app_sha):
    require(record.get("target") == TARGET and record.get("result") == "passed",
            "Cardputer hardware acceptance is pending")
    require(record.get("factory_sha256") == factory_sha and record.get("app_sha256") == app_sha,
            "Hardware acceptance does not match the packaged firmware")
    checks = record.get("checks", {})
    require(all(checks.get(name) is True for name in ACCEPTANCE_CHECKS),
            "Cardputer hardware acceptance checklist is incomplete")
    require(bool(record.get("tested_at")) and bool(record.get("launcher_version")),
            "Hardware acceptance needs test date and Launcher version")


def package(build, output, version):
    require(bool(re.fullmatch(r"\d+\.\d+\.\d+-beta\.\d+", version)), "Expected beta version")
    app = (build / "firmware.bin").read_bytes()
    factory = (build / "firmware.factory.bin").read_bytes()
    validate_factory(factory, version, app)
    partitions = (build / "partitions.bin").read_bytes()
    validate_partitions(partitions)
    require(factory[0x8000:0x8000 + len(partitions)] == partitions,
            "Factory partition table differs from build")
    bootloader = (build / "bootloader.bin").read_bytes()
    validate_image(bootloader)
    # esptool merge-bin may change the boot flash-mode byte and its digest.
    require(factory[24:len(bootloader) - 32] == bootloader[24:-32],
            "Factory bootloader differs from build")
    require(not output.exists() or not any(output.iterdir()), "Choose an empty output directory")
    output.mkdir(parents=True, exist_ok=True)
    base = f"LORA-Tunnel-{version}-Cardputer-ADV-LoRa1262"
    names = {"factory": base + "-FACTORY-0x0.bin", "app": base + "-LAUNCHER-APP.bin"}
    for kind, data in (("factory", factory), ("app", app)):
        (output / names[kind]).write_bytes(data)
    report = {"schema": 1, "target": TARGET, "version": version,
              "flash_bytes": FLASH_BYTES, "psram_bytes": 0, "app_limit": APP_LIMIT,
              "factory_file": names["factory"], "factory_sha256": sha(factory),
              "app_file": names["app"], "app_sha256": sha(app), "app_bytes": len(app),
              "hardware_validation": "pending", "public_enabled": False,
              "formats": {"factory": "merged image; USB/M5Burner at 0x0",
                          "app": "application only; install using Launcher SD menu"}}
    (output / "PACKAGE.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS.txt").write_text("".join(
        f"{report[k + '_sha256']}  {names[k]}\n" for k in ("factory", "app")), encoding="ascii")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path(".pio/build") / TARGET)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="0.4.0-beta.1")
    args = parser.parse_args()
    package(args.build, args.output, args.version)
