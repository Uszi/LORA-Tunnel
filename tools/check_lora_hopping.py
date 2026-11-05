"""Exercise actual receive/retune functions against a bounded synthetic SX1262.

Validates software sequencing, captured profile attribution, IRQ races and bus
contention. Does not substitute for hardware packet/airtime/drive testing.
"""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def extract(source, signature):
    start = source.index(signature)
    position = source.index("{", start) + 1
    depth = 1
    while depth and position < len(source):
        depth += (source[position] == "{") - (source[position] == "}")
        position += 1
    if depth:
        raise RuntimeError("Unterminated scanner function: " + signature)
    return source[start:position]


def check():
    source = (ROOT / "src/scanners.cpp").read_text(encoding="utf-8")
    actual = extract(source, "struct RadioBusLock {") + ";\n"
    for signature in ["void accountRadioListen()", "void stopRadioListen()",
                      "int armRadioReceive()", "bool applyRadioProfile(",
                      "void serviceRadioTransition()", "void readLoRaPacketLocked(",
                      "void serviceLoRa()", "void serviceRadioHop()"]:
        actual += extract(source, signature) + "\n"
    fixture = (ROOT / "tests/scanner_host/hopping.cpp").read_text(encoding="utf-8")
    if fixture.count("// SCANNER_IMPLEMENTATION") != 1:
        raise RuntimeError("Scanner fixture marker is invalid")
    with tempfile.TemporaryDirectory(prefix="lora-radio-host-") as tmp:
        work = Path(tmp)
        unit = work / "radio.cpp"
        unit.write_text(fixture.replace("// SCANNER_IMPLEMENTATION", actual), encoding="utf-8")
        binary = work / ("radio.exe" if os.name == "nt" else "radio")
        cxx = compiler()
        args = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "tests/capture_host"),
                "-I", str(ROOT / "include"), "-I", str(ROOT / "lib/recon/src"), str(unit), "-o", str(binary)]
        if os.name == "nt":
            args += ["-static-libgcc", "-static-libstdc++"]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        subprocess.run(args, cwd=ROOT, env=environment, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, env=environment, check=True, capture_output=True, text=True)
        return result.stdout.strip()


if __name__ == "__main__":
    print(check())
