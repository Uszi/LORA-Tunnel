#!/usr/bin/env python3
"""Package exact library sources/notices for a versioned firmware release.

Never reads a device, SD card, capture, credential file, or private backup.
The matching application source archive is a separate release asset.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ENV = "heltec-v4-r8-tft"
LIBRARIES = ("ArduinoJson", "LovyanGFX", "NimBLE-Arduino", "RadioLib", "TinyGPSPlus")
CARDPUTER_ENV = "cardputer-adv-lora1262"
CARDPUTER_LIBRARIES = ("ArduinoJson", "NimBLE-Arduino", "RadioLib", "TinyGPSPlus", "M5Cardputer", "M5Unified", "M5GFX", "IRremote")
EXCLUDED_PARTS = {".git", ".github", "__pycache__", ".pio", ".pio-core"}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files_below(folder):
    for path in sorted(folder.rglob("*")):
        relative = path.relative_to(folder)
        if not path.is_file() or any(p in EXCLUDED_PARTS for p in relative.parts):
            continue
        # Host executables are not linked firmware/library source. Their Python
        # equivalents are retained; PlatformIO uses gen_esp32part.py directly.
        if path.name == ".piopm" or path.suffix.lower() in {".pyc", ".exe"}:
            continue
        if path.resolve() != path.absolute():
            raise ValueError("Refusing a linked source path")
        yield path, relative.as_posix()


def collect(root, env=ENV):
    framework = root / ".pio-core/packages/framework-arduinoespressif32"
    meta = json.loads((framework / "package.json").read_text(encoding="utf-8"))
    if meta.get("version") != "3.3.12":
        raise ValueError("Expected Arduino-ESP32 3.3.12")
    entries = []
    for path, relative in files_below(framework):
        entries.append(("framework/" + relative, path))
    for name in (CARDPUTER_LIBRARIES if env == CARDPUTER_ENV else LIBRARIES):
        folder = root / ".pio/libdeps" / env / name
        if env == CARDPUTER_ENV and name == "IRremote":
            candidates = [p for p in (root / ".pio/libdeps" / env).glob("IRremote*")
                          if (p / "library.properties").is_file() and
                          "version=4.5.0" in (p / "library.properties").read_text(encoding="utf-8").splitlines()]
            if len(candidates) != 1:
                raise ValueError("Expected exactly one pinned IRremote 4.5.0 source package")
            folder = candidates[0]
        if not folder.is_dir():
            raise FileNotFoundError("Missing installed library: " + name)
        for path, relative in files_below(folder):
            entries.append(("libraries/" + name + "/" + relative, path))
    for path, relative in files_below(root / "third_party/licenses"):
        entries.append(("notices/" + relative, path))
    entries.append(("RELINKING.md", root / "docs/RELINKING.md"))
    sdk = root / ".pio-core/packages/framework-arduinoespressif32-libs"
    for name in ("package.json", "versions.txt", "esp32s3/dependencies.lock", "esp32s3/sdkconfig"):
        entries.append(("sdk-metadata/" + name, sdk / name))
    platforms = list((root / ".pio-core/platforms").glob("espressif32*/platform.json"))
    if len(platforms) != 1:
        raise ValueError("Expected exactly one isolated ESP32 platform")
    entries.append(("build-metadata/platform.json", platforms[0]))
    for package in ("toolchain-xtensa-esp-elf", "tool-esptoolpy", "tool-scons"):
        path = root / ".pio-core/packages" / package / "package.json"
        if path.is_file():
            entries.append(("build-metadata/" + package + ".json", path))
    return sorted(entries)


def application_hashes(root):
    paths = [root / "platformio.ini"]
    for directory in ("src", "include", "lib", "boards"):
        paths.extend(path for path, _ in files_below(root / directory))
    paths.append(root / "assets/lora-tunnel-radio-logo.png")
    paths.append(root / "assets/vts-official-logo.png")
    return {path.relative_to(root).as_posix(): sha(path.read_bytes()) for path in sorted(paths)}


def put(archive, name, data):
    # Stable archive metadata; this is source-content reproducibility, not a
    # promise that compiler timestamps produce byte-identical firmware.
    info = zipfile.ZipInfo("LORA-Tunnel-relink/" + name, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


def package(args):
    root = args.root.resolve()
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError("Refusing to overwrite an existing companion")
    entries = collect(root, args.env)
    build = args.build.resolve() if args.build else root / ".pio/build" / args.env
    firmware = {}
    for name in ("firmware.bin", "firmware.factory.bin", "bootloader.bin", "partitions.bin"):
        data = (build / name).read_bytes()
        firmware[name] = {"bytes": len(data), "sha256": sha(data)}
    manifest = {
        "schema": 1,
        "release": args.version,
        "environment": args.env,
        "arduino_version": "3.3.12",
        "platform": "55.03.312-1",
        "sdk": "5.5.5+sha.b774170ff46",
        "matching_firmware": firmware,
        "application_sources": application_hashes(root),
        "files": {},
        "source_scope": "Installed Arduino core and the selected target's pinned library source packages; no SDK/toolchain binary mirror. Application source is supplied beside this companion.",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in entries:
            data = path.read_bytes()
            manifest["files"][name] = {"bytes": len(data), "sha256": sha(data)}
            put(archive, name, data)
        put(archive, "MANIFEST.json", (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode())
    verify(output)
    print("SHA256 " + sha(output.read_bytes()))


def verify(path):
    with zipfile.ZipFile(path) as archive:
        prefix = "LORA-Tunnel-relink/"
        manifest = json.loads(archive.read(prefix + "MANIFEST.json"))
        expected = {prefix + name for name in manifest["files"]} | {prefix + "MANIFEST.json"}
        names = archive.namelist()
        if len(names) != len(set(names)) or set(names) != expected:
            raise ValueError("Unexpected or duplicate archive paths")
        for name, meta in manifest["files"].items():
            if name.startswith(("/", "\\")) or ".." in Path(name).parts:
                raise ValueError("Unsafe archive path")
            data = archive.read(prefix + name)
            if len(data) != meta["bytes"] or sha(data) != meta["sha256"]:
                raise ValueError("Companion hash mismatch: " + name)
        print("Verified", len(manifest["files"]), "source/notice/metadata files for", manifest["release"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("package")
    pack.add_argument("version")
    pack.add_argument("output", type=Path)
    pack.add_argument("--root", type=Path, default=ROOT)
    pack.add_argument("--env", choices=(ENV, CARDPUTER_ENV), default=ENV)
    pack.add_argument("--build", type=Path, help="Exact matching build-output directory; defaults to .pio/build/ENV")
    check = commands.add_parser("verify")
    check.add_argument("archive", type=Path)
    args = parser.parse_args()
    if args.command == "package":
        package(args)
    else:
        verify(args.archive)


if __name__ == "__main__":
    main()
