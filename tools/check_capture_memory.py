"""Compile actual drain code against bounded queue copies and real record types."""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def check():
    main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    signature = "void drain(unsigned limit=48) {"
    def function(source, signature):
        begin = source.index(signature)
        opening = source.index("{", begin)
        depth, position = 1, opening + 1
        while depth and position < len(source):
            depth += (source[position] == "{") - (source[position] == "}")
            position += 1
        if depth:
            raise RuntimeError("Unterminated production function: " + signature)
        return source[begin:position] + "\n"
    body = function(main, signature)
    if "sizeof(CaptureObservation)" not in main or "static_cast<CaptureObservation*>(&event)" not in body:
        raise RuntimeError("Actual ingress queue must use the compact base record")
    scanners = (ROOT / "src/scanners.cpp").read_text(encoding="utf-8")
    if "static_cast<Observation*>" in scanners or "sizeof(Observation)" in scanners:
        raise RuntimeError("Callback scratch must use compact capture records")
    ap_start = scanners.index("struct ScanAp {")
    ap_end = scanners.index("\nvoid IRAM_ATTR", ap_start)
    ap_body = scanners[ap_start:ap_end] + "\n" + function(scanners, "const char* auth(") + function(scanners, "void collectWifi()") + function(scanners, "void serviceWifiResults()")
    with tempfile.TemporaryDirectory(prefix="lora-capture-host-") as tmp:
        work = Path(tmp)
        (work / "drain_actual.inc").write_text(body, encoding="utf-8")
        (work / "ap_actual.inc").write_text(ap_body, encoding="utf-8")
        cxx = compiler()
        binary = work / ("capture.exe" if os.name == "nt" else "capture")
        args = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "tests/capture_host"),
                "-I", str(ROOT / "include"), "-I", str(ROOT / "lib/recon/src"), "-I", str(work),
                str(ROOT / "tests/capture_host/queue.cpp"), "-o", str(binary)]
        if os.name == "nt": args += ["-static-libgcc", "-static-libstdc++"]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        results = []
        for source in ["queue.cpp", "ap_inventory.cpp"]:
            invocation = [str(ROOT / "tests/capture_host" / source) if value == str(ROOT / "tests/capture_host/queue.cpp") else value for value in args]
            subprocess.run(invocation, cwd=ROOT, env=environment, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, env=environment, check=True, capture_output=True, text=True)
            results.append(result.stdout.strip())
        return "\n".join(results)

if __name__ == "__main__":
    print(check())
