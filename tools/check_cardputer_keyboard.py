"""Exercise the production key mapper against the pinned M5Cardputer key map."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("g++")
    if not compiler:
        candidate = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe"
        if candidate.is_file():
            compiler = str(candidate)
    if not compiler:
        parser.error("g++ is required; supply --compiler or add it to PATH")
    sdk = ROOT / ".pio/libdeps/cardputer-adv-lora1262/M5Cardputer/src"
    if not (sdk / "utility/Keyboard/Keyboard.h").is_file():
        parser.error("Install the pinned cardputer-adv-lora1262 PlatformIO dependencies first")
    output = ROOT / "build-output/cardputer-keyboard"
    output.mkdir(parents=True, exist_ok=True)
    binary = output / ("check.exe" if os.name == "nt" else "check")
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + env.get("PATH", "")
    # Xtensa uses unsigned plain char; the SDK stores 0x80..0xff key IDs in char.
    subprocess.run([compiler, "-std=c++17", "-funsigned-char", "-I", str(ROOT / "tests/ui_host"),
                    "-I", str(ROOT / "include"), "-I", str(sdk),
                    str(ROOT / "tests/keyboard_host/check.cpp"), "-o", str(binary)],
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)


if __name__ == "__main__":
    main()
