"""Run the real upload worker against in-memory SD and scripted HTTPS adapters.

This validates scheduling, streaming, receipts, checkpoints, retries and failure
paths. It does not establish TLS interoperability or live-service acceptance.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def check() -> str:
    dependencies = list((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
    if not dependencies:
        raise RuntimeError("Install the pinned PlatformIO dependencies before running uploader host checks.")
    with tempfile.TemporaryDirectory(prefix="lora-upload-host-") as temporary:
        binary = Path(temporary) / ("upload-test.exe" if os.name == "nt" else "upload-test")
        cxx = compiler()
        args = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
                "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
                "-I", str(ROOT / "tests/upload_host"), "-I", str(ROOT / "include"),
                "-I", str(dependencies[0].parent), str(ROOT / "tests/upload_host/uploader.cpp"), "-o", str(binary)]
        if os.name == "nt":
            args += ["-static-libgcc", "-static-libstdc++"]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        subprocess.run(args, cwd=ROOT, check=True, env=environment)
        result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True, env=environment)
        return result.stdout.strip()


if __name__ == "__main__":
    print(check())
