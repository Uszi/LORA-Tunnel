"""Run real upload/session code with offline SD, HTTPS and BLE lifecycle adapters.

Checks scheduling, streaming, checkpoints, memory/transition failures and the
actual session-start capture gate. No live service or device is contacted.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def extract_body(source: str, signature: str) -> str:
    if source.count(signature) != 1:
        raise RuntimeError("Expected exactly one actual definition: " + signature)
    start = source.index(signature)
    position = start + len(signature)
    depth = 1
    while position < len(source) and depth:
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
        position += 1
    if depth:
        raise RuntimeError("Unterminated actual definition: " + signature)
    return source[start:position] + "\n"


def request_session_body():
    source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    marker = extract_body(source, "__attribute__((noinline)) bool logSessionMarker(Kind kind,CaptureMode mode,uint32_t id) {")
    start = extract_body(source, "void requestSession() {")
    stop = extract_body(source, "  if(stopping) {")
    return marker + start + "void serviceStopMarker() {\n" + stop + "}\n"


def check() -> str:
    dependencies = list((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
    if not dependencies:
        raise RuntimeError("Install the pinned PlatformIO dependencies before running uploader host checks.")
    with tempfile.TemporaryDirectory(prefix="lora-upload-host-") as temporary:
        directory = Path(temporary)
        (directory / "request_session_actual.inc").write_text(request_session_body(), encoding="utf-8")
        cxx = compiler()
        base = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
                "-DARDUINOJSON_ENABLE_ARDUINO_STRING=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0",
                "-I", str(ROOT / "tests/upload_host"), "-I", str(ROOT / "include"),
                "-I", str(dependencies[0].parent), "-I", str(directory)]
        if os.name == "nt":
            base += ["-static-libgcc", "-static-libstdc++"]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        results = []
        for name, source, flags in [
                ("Cardputer", "uploader.cpp", ["-DLORA_TUNNEL_CARDPUTER_ADV=1"]),
                ("Heltec", "uploader.cpp", []),
                ("Session", "request_session.cpp", [])]:
            binary = directory / (name + (".exe" if os.name == "nt" else ""))
            args = base + flags + [str(ROOT / "tests/upload_host" / source), "-o", str(binary)]
            subprocess.run(args, cwd=ROOT, check=True, env=environment)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True, env=environment)
            results.append(name + ": " + result.stdout.strip())
        return "\n".join(results)


if __name__ == "__main__":
    print(check())
