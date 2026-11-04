"""Exercise the actual checked-file helper and logger I/O with failing C/VFS calls."""
from pathlib import Path
import os
import subprocess
import tempfile
from check_cardputer_ui import ROOT, compiler


def check():
    logger = (ROOT / "src/logger.cpp").read_text(encoding="utf-8")
    actual = logger[logger.index("void fault()"):logger.index("void hex(")]
    helper = (ROOT / "src/checked_sd_file.cpp").read_text(encoding="utf-8")
    native_open = helper[helper.index("FILE* nativeOpen("):helper.index("int nativeSync(")]
    # Test the actual descriptor-to-stream cleanup branch without touching a
    # device or host /sd directory. Only the four system-call names are mapped.
    for before, after in (("std::fopen", "streamOpenFake"), ("::fdopen", "fdStreamFake"),
                          ("::open", "fdOpenFake"), ("::close", "fdCloseFake")):
        native_open = native_open.replace(before, after)
    fixture = (ROOT / "tests/storage_host/check.cpp").read_text(encoding="utf-8")
    for marker in ("// LOGGER_IO_IMPLEMENTATION", "// NATIVE_OPEN_IMPLEMENTATION"):
        if fixture.count(marker) != 1:
            raise RuntimeError("Invalid checked-SD fixture marker")
    fixture = fixture.replace("// LOGGER_IO_IMPLEMENTATION", actual).replace("// NATIVE_OPEN_IMPLEMENTATION", native_open)
    cxx = compiler()
    environment = os.environ.copy()
    environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
    results = []
    with tempfile.TemporaryDirectory(prefix="lora-checked-sd-") as temporary:
        folder = Path(temporary)
        source = folder / "check.cpp"
        source.write_text(fixture, encoding="utf-8")
        # Also compile the production path: test-only dispatch must not hide a
        # libc signature or include error in the direct-call firmware branch.
        subprocess.run([cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "include"),
                        "-c", str(ROOT / "src/checked_sd_file.cpp"), "-o", str(folder / "production.o")],
                       cwd=ROOT, env=environment, check=True)
        for cardputer in (True, False):
            output = folder / (("cardputer" if cardputer else "heltec") + (".exe" if os.name == "nt" else ""))
            args = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-DLORA_TUNNEL_STORAGE_TEST",
                    "-I", str(ROOT / "include"), str(source), str(ROOT / "src/checked_sd_file.cpp"), "-o", str(output)]
            if cardputer:
                args += ["-DLORA_TUNNEL_CARDPUTER_ADV"]
            if os.name == "nt":
                args += ["-static-libgcc", "-static-libstdc++"]
            subprocess.run(args, cwd=ROOT, env=environment, check=True)
            result = subprocess.run([str(output)], cwd=ROOT, env=environment, check=True, text=True, capture_output=True)
            results.append(result.stdout.strip())
    return "\n".join(results)


if __name__ == "__main__":
    print(check())
