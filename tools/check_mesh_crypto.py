"""Validate real mesh parsers against independent cryptographic wire fixtures.

Requires the pinned rweather Crypto package installed by a firmware build.
Compiles its host AES and Ed25519 implementations with the real parsers. This
does not replace or claim runtime testing of the ESP32 SDK mbedTLS AES adapter.
Fixtures are embedded public/synthetic bytes; no Python crypto dependency,
network, key generation, user configuration, captured logs or radio is used.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def compiler() -> str:
    candidates = [os.environ.get("CXX"), shutil.which("g++"), shutil.which("clang++")]
    if os.name == "nt":
        candidates.append(str(Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError("A host C++ compiler is required; set CXX to g++ or clang++.")


def crypto_directory(explicit: Path | None) -> Path:
    candidates = [explicit] if explicit else [
        ROOT / ".pio/libdeps/heltec-v4-r8-tft/Crypto",
        ROOT / ".pio/libdeps/cardputer-adv-lora1262/Crypto",
    ]
    for candidate in candidates:
        if candidate and (candidate / "Ed25519.cpp").is_file():
            metadata = candidate / "library.json"
            if not metadata.is_file() or json.loads(metadata.read_text(encoding="utf-8")).get("version") != "0.4.0":
                raise RuntimeError(f"Expected pinned rweather/Crypto 0.4.0 in {candidate}")
            return candidate
    raise RuntimeError("Build a firmware target first to install pinned rweather/Crypto@0.4.0.")


def check(crypto: Path) -> str:
    cxx = compiler()
    environment = os.environ.copy()
    environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
    sources = ["AESCommon.cpp", "AES128.cpp", "AES256.cpp", "BlockCipher.cpp",
               "Crypto.cpp", "BigNumberUtil.cpp", "Curve25519.cpp", "Ed25519.cpp", "Hash.cpp", "SHA512.cpp"]
    limb = crypto / "utility/LimbUtil.cpp"
    if limb.is_file():
        sources.append("utility/LimbUtil.cpp")
    # Keep generated executables in the ignored build tree. Restricted Windows
    # sessions may not permit a child linker to use the account's global TEMP.
    temporary_root = ROOT / ".pio/host-checks"
    temporary_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mesh-crypto-", dir=temporary_root) as temporary:
        binary = Path(temporary) / ("mesh-crypto.exe" if os.name == "nt" else "mesh-crypto")
        command = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra",
                   "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                   "-I", str(ROOT / "include"), "-I", str(ROOT / "lib/meshdecode/src"),
                   "-I", str(crypto), str(ROOT / "tests/mesh_crypto_host/main.cpp"),
                   str(ROOT / "lib/meshdecode/src/MeshCoreDecode.cpp"),
                   str(ROOT / "lib/meshdecode/src/MeshtasticDecode.cpp")]
        command.extend(str(crypto / source) for source in sources)
        command.extend(["-o", str(binary)])
        if os.name == "nt":
            command.extend(["-static-libgcc", "-static-libstdc++"])
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, env=environment, check=False,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Mesh crypto fixture failed ({result.returncode}):\n"
                               f"{result.stdout}{result.stderr}")
        return result.stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crypto-dir", type=Path)
    arguments = parser.parse_args()
    print(check(crypto_directory(arguments.crypto_dir)))


if __name__ == "__main__":
    main()
