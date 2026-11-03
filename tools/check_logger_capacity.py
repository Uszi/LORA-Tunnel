"""Prove the complete logger record fits its Cardputer arena using actual draw-independent serialization code.

32-bit hosts use the actual 4096-byte arena. On 64-bit CI, larger host JSON
bookkeeping slots are normalized to the ESP32's 16-byte slots; string storage
and complete serialized payload bounds are still measured rather than guessed.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def check() -> str:
    dependencies = list((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
    if not dependencies:
        raise RuntimeError("Install the pinned PlatformIO dependencies first.")
    source = (ROOT / "src/logger.cpp").read_text(encoding="utf-8")
    actual = source[source.index("void hex("):source.index("void task(")]
    fixture = (ROOT / "tests/logger_host/capacity.cpp").read_text(encoding="utf-8")
    if fixture.count("// LOGGER_IMPLEMENTATION") != 1:
        raise RuntimeError("Logger capacity fixture marker is invalid.")
    with tempfile.TemporaryDirectory(prefix="lora-logger-host-") as temporary:
        folder = Path(temporary)
        translation_unit = folder / "logger.cpp"
        translation_unit.write_text(fixture.replace("// LOGGER_IMPLEMENTATION", actual), encoding="utf-8")
        binary = folder / ("logger-test.exe" if os.name == "nt" else "logger-test")
        cxx = compiler()
        args = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "include"),
                "-I", str(ROOT / "tests/upload_host"), "-I", str(ROOT / "lib/recon/src"),
                "-I", str(dependencies[0].parent), str(translation_unit), "-o", str(binary)]
        if os.name == "nt":
            args += ["-static-libgcc", "-static-libstdc++"]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        subprocess.run(args, cwd=ROOT, check=True, env=environment)
        result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True, env=environment)
        return result.stdout.strip()


if __name__ == "__main__":
    print(check())
