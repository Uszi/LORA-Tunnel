#!/usr/bin/env python3
"""Compose a local M5Burner cover from existing artwork and synthetic UI renders."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/ui/cardputer/m5burner-cover.png"


def font(size, bold=False):
    candidates = (["C:/Windows/Fonts/arialbd.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
                  if bold else ["C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"])
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def main():
    canvas = Image.new("RGB", (1280, 720), "#07120e")
    draw = ImageDraw.Draw(canvas)
    green, white, muted = "#9bf66c", "#f0f5ef", "#b4c4b8"
    draw.rounded_rectangle((28, 28, 1252, 692), radius=24, outline="#355a3c", width=2)
    logo = Image.open(ROOT / "assets/lora-tunnel-radio-logo.png").convert("RGBA")
    logo.thumbnail((328, 328), Image.Resampling.LANCZOS)
    canvas.paste(logo, (62 + (328 - logo.width) // 2, 94), logo)
    draw.text((62, 447), "VALLEYTECH SOLUTIONS", font=font(21, True), fill=green)
    draw.text((62, 484), "LORA TUNNEL", font=font(34, True), fill=white)
    draw.text((62, 538), "Wi-Fi / BLE / LoRa", font=font(24), fill=muted)
    draw.text((442, 68), "CARDPUTER ADV", font=font(42, True), fill=white)
    draw.text((444, 122), "+ CAP LoRa-1262 (U214)", font=font(29), fill=green)
    draw.rounded_rectangle((438, 188, 1190, 629), radius=17, fill="#020705", outline="#426647", width=2)
    preview = Image.open(ROOT / "assets/ui/cardputer/wardriver.png").convert("RGB")
    if preview.size != (240, 135):
        raise ValueError("Expected the actual 240 x 135 source UI render")
    preview = preview.resize((720, 405), Image.Resampling.NEAREST)
    canvas.paste(preview, (454, 205))
    draw.text((444, 650), "SYNTHETIC UI PREVIEW / NOT A DEVICE PHOTO", font=font(18, True), fill=muted)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
