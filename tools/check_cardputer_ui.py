"""Compile the real Cardputer UI against a bounded host drawing adapter.

The adapter validates every draw rectangle/text cell against the panel bounds.
Optional previews replay those commands with the pinned Font0 and original PNGs.
They are synthetic layout evidence, not physical panel/input acceptance.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def compiler() -> str:
    explicit = os.environ.get("CXX")
    candidates = [explicit, shutil.which("g++"), shutil.which("clang++")]
    if os.name == "nt":
        candidates.append(str(Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError("A host C++ compiler is required (set CXX to g++ or clang++).")


def check(output: Path) -> str:
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lora-cardputer-ui-") as temporary:
        temp = Path(temporary)
        assembly = [".section .data"]
        for asset in ("lora-tunnel-radio-logo", "vts-official-logo"):
            symbol = "_binary_assets_" + asset.replace("-", "_") + "_png"
            assembly.extend((f".global {symbol}_start", f".global {symbol}_end", f"{symbol}_start:",
                             f'.incbin "{(ROOT / "assets" / (asset + ".png")).as_posix()}"', f"{symbol}_end:"))
        assets = temp / "assets.S"
        assets.write_text("\n".join(assembly) + "\n", encoding="utf-8")
        binary = temp / ("ui-test.exe" if os.name == "nt" else "ui-test")
        cxx = compiler()
        command = [cxx, "-std=c++14", "-O1", "-Wall", "-Wextra", "-DLORA_TUNNEL_CARDPUTER_ADV",
                   "-DLORA_TUNNEL_HOST_UI", "-I", str(ROOT / "tests/ui_host"), "-I", str(ROOT / "include"),
                   str(ROOT / "tests/ui_host/cardputer_ui.cpp"), str(assets), "-o", str(binary)]
        if os.name == "nt":
            command.extend(("-static-libgcc", "-static-libstdc++"))
        environment = os.environ.copy()
        environment["PATH"] = str(Path(cxx).parent) + os.pathsep + environment.get("PATH", "")
        subprocess.run(command, cwd=ROOT, check=True, env=environment)
        result = subprocess.run([str(binary), str(output)], cwd=ROOT, check=True, capture_output=True, text=True, env=environment)
        return result.stdout.strip()


def render(output: Path) -> None:
    from PIL import Image, ImageDraw
    font_paths = list((ROOT / ".pio/libdeps").glob("*/LovyanGFX/src/lgfx/Fonts/glcdfont.h"))
    font_paths += list((ROOT / ".pio/libdeps").glob("*/M5GFX/src/lgfx/Fonts/glcdfont.h"))
    if not font_paths:
        raise RuntimeError("Build dependencies must be installed to render using the pinned Font0.")
    source = font_paths[0].read_text(encoding="utf-8")
    table = source.split("font[] PROGMEM = {", 1)[1].split("};", 1)[0]
    table = re.sub(r"//[^\n]*", "", table)
    font = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", table))
    logos = {"radio": Image.open(ROOT / "assets/lora-tunnel-radio-logo.png").convert("RGBA"),
             "vts": Image.open(ROOT / "assets/vts-official-logo.png").convert("RGBA")}
    def rgb(color: int) -> tuple[int, int, int]:
        # The native canvas converts RGB565 draw colors to RGB332.
        red, green, blue = ((color >> 11) & 31) * 255 // 31, ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31
        return (red >> 5) * 255 // 7, (green >> 5) * 255 // 7, (blue >> 6) * 255 // 3
    for file in sorted(output.glob("*.json")):
        image = Image.new("RGB", (240, 135))
        draw = ImageDraw.Draw(image)
        for command in json.loads(file.read_text(encoding="utf-8")):
            x, y, w, h = (command[name] for name in ("x", "y", "w", "h"))
            color = rgb(command["color"])
            if command["kind"] in ("rect", "round"):
                draw.rounded_rectangle((x, y, x + w - 1, y + h - 1), radius=command["r"], fill=color)
            elif command["kind"] == "text":
                scale = command["scale"]
                for index, char in enumerate(command["text"]):
                    for column in range(5):
                        bits = font[ord(char) * 5 + column]
                        for bit in range(8):
                            if bits & (1 << bit):
                                left, top = x + (index * 6 + column) * scale, y + bit * scale
                                draw.rectangle((left, top, left + scale - 1, top + scale - 1), fill=color)
            elif command["kind"] == "png":
                source = logos[command["text"]].copy()
                source.thumbnail((w, h), Image.Resampling.LANCZOS)
                image.paste(source, (x, y), source)
        image.save(file.with_suffix(".png"))
    (output / "README.txt").write_text("Synthetic 240x135 previews rendered from src/ui_cardputer.cpp with original artwork and pinned Font0.\nThey show layout and state only, not measured hardware behavior.\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build-output/cardputer-ui")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    print(check(args.output))
    if args.render:
        render(args.output)
