"""Exercise actual configuration operations with bounded scratch-allocation faults."""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def check() -> str:
    dependencies = list((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
    if not dependencies:
        raise RuntimeError("Install the pinned PlatformIO dependencies first.")
    with tempfile.TemporaryDirectory(prefix="lora-config-host-") as temporary:
        binary = Path(temporary) / ("config-test.exe" if os.name == "nt" else "config-test")
        cxx = compiler()
        args = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
                "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
                "-I", str(ROOT / "tests/config_host"), "-I", str(ROOT / "include"),
                "-I", str(ROOT / "tests/upload_host"), "-I", str(ROOT / "lib/recon/src"),
                "-I", str(dependencies[0].parent), str(ROOT / "tests/config_host/config_memory.cpp"), "-o", str(binary)]
        if os.name == "nt":
            args += ["-static-libgcc", "-static-libstdc++"]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        results = []
        for target, flags in [("Cardputer", ["-DLORA_TUNNEL_CARDPUTER_ADV=1"]), ("Heltec", [])]:
            subprocess.run(args + flags, cwd=ROOT, check=True, env=environment)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True, env=environment)
            results.append(target + ": " + result.stdout.strip())
        return "\n".join(results)


if __name__ == "__main__":
    print(check())
