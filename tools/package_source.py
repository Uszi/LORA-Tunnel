#!/usr/bin/env python3
"""Create a source-only archive from explicit project paths; exclude private SD files."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FOLDERS = ("boards", "docs", "include", "lib", "linker", "schemas", "src", "test", "tests",
           "third_party", "tools", "web", "assets/ui", ".github")
FILES = ("README.md", "LICENSE", "platformio.ini", ".gitignore", ".gitattributes", "sdcard/README.md",
         "assets/IMAGEGEN.md", "assets/lora-tunnel-radio-logo.png", "assets/vts-official-logo.png",
         "sdcard/lora-tunnel/config.example.json", "sdcard/lora-tunnel/detection_rules.json",
         "sdcard/lora-tunnel/detection_rules.example.json",
         "sdcard/lora-tunnel/vendor-prefixes.csv",
         "sdcard/lora-tunnel/vendor-prefixes.README.md",
         "sdcard/lora-tunnel/vendor-prefixes.LICENSE.txt",
         "examples/synthetic-session.jsonl", "examples/synthetic-wigle.csv",
         "examples/synthetic-wdgwars-mesh-candidate.json")
# Historical bench/build transcripts and the original conversation brief stay local.
# Public validation summaries live in docs/VALIDATION.md.
EXCLUDED_FILES = {"docs/PROJECT_BRIEF.txt"}
EXCLUDED_DIRS = {"__pycache__", "node_modules", ".git", ".venv", "dist", "build",
                 "site", "private", ".wrangler", ".cache"}



def collect_paths(root=ROOT):
    """Resolve the public source inventory without creating an archive."""
    root = Path(root).resolve()
    # SD folders are never scanned recursively. Even these explicitly named
    # templates must still be safe if a user edited them in their checkout.
    sample = json.loads((root / "sdcard/lora-tunnel/config.example.json").read_text(encoding="utf-8"))
    credentials = sample.get("credentials")
    if not isinstance(credentials, dict) or any(value != "" for value in credentials.values()):
        raise ValueError("Source example credentials must all be empty; keep real values on the private SD card")
    rules = json.loads((root / "sdcard/lora-tunnel/detection_rules.json").read_text(encoding="utf-8"))
    if rules.get("schema") != 1 or rules.get("rules") != []:
        raise ValueError("The packaged detection_rules.json must retain its empty rules array")
    paths = [root / name for name in FILES]
    for name in FOLDERS:
        paths.extend(p for p in (root / name).rglob("*")
                     if p.is_file() and not EXCLUDED_DIRS.intersection(p.relative_to(root).parts)
                     and p.relative_to(root).as_posix() not in EXCLUDED_FILES
                     and p.suffix not in (".pyc", ".bin", ".elf", ".zip", ".log"))
    for path in paths:
        if not path.is_file():
            raise FileNotFoundError(path)
        # A linked file or directory must not bypass the explicit SD/asset list.
        if path.resolve() != path.absolute():
            raise ValueError("Source inventory contains a linked path; refusing to follow it")
        path.resolve().relative_to(root)
    return sorted(set(paths))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("Refusing to overwrite an existing archive")
    paths = collect_paths()
    output.parent.mkdir(parents=True, exist_ok=True)
    hashes = {}
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as archive:
        for path in paths:
            # Never infer private data from nearby directories or bundle .pio/research.
            if not path.is_file():
                raise FileNotFoundError(path)
            name = path.relative_to(ROOT).as_posix()
            data = path.read_bytes()
            hashes[name] = hashlib.sha256(data).hexdigest()
            archive.writestr("LORA-Tunnel/" + name, data)
        archive.writestr("LORA-Tunnel/SOURCE-SHA256.json", json.dumps(hashes, indent=2) + "\n")
    print(f"Created {output.name}: {len(hashes)} source/assets/example files")
    print("SHA256 " + hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
