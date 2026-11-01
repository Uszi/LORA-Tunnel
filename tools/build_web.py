#!/usr/bin/env python3
"""Stage the public beta site from a checked factory image and matching source ZIP.

Does not flash devices, contact upload services, or publish the website.
Install the pinned host dependency from tools/site-requirements.txt first.
"""
import argparse
import hashlib
import html
from html.parser import HTMLParser
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess
from urllib.parse import quote, unquote, urlsplit
import zipfile

import markdown

ROOT = Path(__file__).resolve().parents[1]
SITE = "https://valleytech-lora-tunnel.pages.dev/"
TARGET = "heltec-v4-r8-tft-us915"
MAX_FILE = 25 * 1024 * 1024
VERSION_RE = re.compile(r'#define TUNNEL_VERSION "([0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+)"')
GUIDES = ("USER_GUIDE", "UI_GUIDE", "HARDWARE", "WEB_FLASHER", "RECOVERY",
          "EXPORTS", "RECON", "THIRD_PARTY", "VALIDATION", "LOG_FORMAT",
          "ARCHITECTURE", "TEST_CHECKLIST", "GPS_DIAGNOSTICS", "LORA", "RELINKING")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked_repo(value, host):
    parsed = urlsplit(value)
    if (parsed.scheme != "https" or parsed.netloc != host or parsed.query or
            parsed.fragment or not re.fullmatch(r"/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", parsed.path)):
        raise ValueError(f"Expected an HTTPS repository URL on {host}")
    return value.rstrip("/")


def validate_factory(data, version, expected_sha):
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha) or digest(data) != expected_sha:
        raise ValueError("Factory SHA-256 mismatch")
    if len(data) < 0x10000 + 256 or len(data) > 16 * 1024 * 1024:
        raise ValueError("Factory length outside the supported image bounds")
    # ESP image header: magic, SPI mode, flash-size nibble and little-endian chip ID.
    if data[0] != 0xE9 or data[2] != 2 or data[3] >> 4 != 4 or int.from_bytes(data[12:14], "little") != 9:
        raise ValueError("Factory boot header must be ESP32-S3, 16 MB flash, DIO")
    if data[0x10000] != 0xE9 or version.encode("ascii") + b"\0" not in data[0x10000:]:
        raise ValueError("Factory is missing the expected application/version")


def read_source(source_zip, version):
    with zipfile.ZipFile(source_zip) as archive:
        paths = [p for p in archive.namelist() if not p.endswith("/")]
        if len(paths) != len(set(paths)) or len(paths) != len(set(p.casefold() for p in paths)):
            raise ValueError("Duplicate source archive entries")
        if any(PurePosixPath(p).is_absolute() or ".." in PurePosixPath(p).parts or "\\" in p for p in paths):
            raise ValueError("Unsafe source archive path")
        header_paths = [p for p in paths if p.endswith("/include/version.h")]
        if len(header_paths) != 1:
            raise ValueError("Expected exactly one source version header")
        prefix = header_paths[0][:-len("include/version.h")]
        manifest_name = prefix + "SOURCE-SHA256.json"
        if manifest_name not in paths:
            raise ValueError("Source archive has no SOURCE-SHA256 manifest")
        def unique_object(pairs):
            result = {}
            for name, value in pairs:
                if name in result:
                    raise ValueError("Duplicate source manifest entry")
                result[name] = value
            return result
        inventory = json.loads(archive.read(manifest_name), object_pairs_hook=unique_object)
        if not isinstance(inventory, dict) or not inventory:
            raise ValueError("Source manifest must be a nonempty object")
        if set(paths) != {prefix + p for p in inventory} | {manifest_name}:
            raise ValueError("Source manifest/archive inventory mismatch")
        sources = {}
        for name, expected in inventory.items():
            if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
                raise ValueError("Invalid source manifest hash")
            data = archive.read(prefix + name)
            if digest(data) != expected:
                raise ValueError("Source manifest hash mismatch: " + name)
            sources[name] = data
        header = archive.read(header_paths[0]).decode("utf-8")
        match = VERSION_RE.search(header)
        if not match or match.group(1) != version:
            raise ValueError("Source version does not match requested beta")
        config = json.loads(archive.read(prefix + "sdcard/lora-tunnel/config.example.json"))
        if not isinstance(config.get("credentials"), dict) or any(v != "" for v in config["credentials"].values()):
            raise ValueError("Source sample credentials must be empty")
        rules = json.loads(archive.read(prefix + "sdcard/lora-tunnel/detection_rules.json"))
        if rules.get("schema") != 1 or rules.get("rules") != []:
            raise ValueError("Shipped detection rules must be the empty example")
        return sources


def validate_commit(sources, commit, root=ROOT):
    result = subprocess.run(["git", "ls-tree", "-rz", "--full-tree", commit], cwd=root,
                            capture_output=True, check=True)
    tree = {}
    for record in result.stdout.split(b"\0"):
        if not record:
            continue
        metadata, path = record.split(b"\t", 1)
        mode, kind, sha = metadata.decode("ascii").split()
        if kind == "blob" and mode in ("100644", "100755"):
            tree[path.decode("utf-8")] = sha
    for name, data in sources.items():
        blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        if tree.get(name) != blob:
            raise ValueError("Source ZIP differs from the supplied Git commit: " + name)


class LocalLinks(HTMLParser):
    def __init__(self, root, source):
        super().__init__()
        self.root, self.source = root.resolve(), source

    def handle_starttag(self, tag, attrs):
        for name, value in attrs:
            if name not in ("href", "src", "manifest") or not value:
                continue
            url = urlsplit(value)
            if url.scheme or url.netloc or not url.path:
                continue
            target = (self.root / unquote(url.path.lstrip("/")) if url.path.startswith("/")
                      else self.source.parent / unquote(url.path)).resolve()
            if not target.is_relative_to(self.root) or not target.is_file():
                raise ValueError(f"Missing local link in {self.source.name}: {value}")


def check_links(output):
    for page in output.rglob("*.html"):
        LocalLinks(output, page).feed(page.read_text(encoding="utf-8"))


def build(args):
    version_header = (ROOT / "include/version.h").read_text(encoding="utf-8")
    version_match = VERSION_RE.search(version_header)
    if not version_match:
        raise ValueError("Source version must identify an explicit beta release")
    version = version_match.group(1)
    github = checked_repo(args.github_url, "github.com")
    gitea = checked_repo(args.gitea_url, "git.churchofmalware.org") if args.gitea_url else None
    relink_url = urlsplit(args.relink_url)
    if (not args.relink_url.startswith(github + "/releases/download/") or
            relink_url.query or relink_url.fragment or
            not re.fullmatch(r"[0-9a-f]{64}", args.relink_sha256)):
        raise ValueError("Use the checked matching GitHub release relinking archive URL and SHA-256")
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        raise ValueError("Use the full release source commit SHA")
    factory_data = args.factory.read_bytes()
    validate_factory(factory_data, version, args.factory_sha256)
    source_data = args.source.read_bytes()
    if digest(source_data) != args.source_sha256:
        raise ValueError("Source ZIP SHA-256 mismatch")
    sources = read_source(args.source, version)
    validate_commit(sources, args.commit)
    output = args.output.resolve()
    if output == ROOT or (output.is_relative_to(ROOT) and output.relative_to(ROOT).parts[0] not in ("build-output", "dist")):
        raise ValueError("Choose an empty build-output/dist folder or a folder outside the source tree")
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output must be empty; existing sites are never deleted or overwritten")
    output.mkdir(parents=True, exist_ok=True)
    factory_name = f"LORA-Tunnel-{version}-Heltec-V4-R8-TFT-US915-FACTORY-0x0.bin"
    source_name = f"LORA-Tunnel-{version}-source.zip"
    sd_name = f"LORA-Tunnel-{version}-SD-setup.zip"
    tokens = {"VERSION": version, "FACTORY": factory_name, "SOURCE": source_name,
              "SD": sd_name, "FACTORY_SHA": args.factory_sha256, "GITHUB": github,
              "GITEA": gitea or "https://git.churchofmalware.org/Valleytech",
              "RELINK": args.relink_url, "RELINK_SHA": args.relink_sha256}
    for source_path, source_bytes in sources.items():
        path = PurePosixPath(source_path)
        if not source_path.startswith("web/") or any(p.startswith(".") for p in path.parts[1:]):
            continue
        destination = output / path.relative_to("web")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix == ".html":
            text = source_bytes.decode("utf-8")
            for key, value in tokens.items():
                text = text.replace("@@" + key + "@@", html.escape(value, quote=True))
            if not gitea:
                text = text.replace("Source on Gitea ↗", "Gitea publication pending ↗")
            if "@@" in text:
                raise ValueError("Unresolved public HTML template token")
            destination.write_text(text, encoding="utf-8")
        else:
            destination.write_bytes(source_bytes)
    vendor = output / "vendor/esp-web-tools-10.4.0"
    upstream = json.loads((vendor / "UPSTREAM.json").read_text(encoding="utf-8"))
    if upstream["version"] != "10.4.0":
        raise ValueError("Unexpected installer dependency version")
    for name, expected in upstream["files_sha256"].items():
        if digest((vendor / name).read_bytes()) != expected:
            raise ValueError("Installer dependency hash mismatch: " + name)
    (output / "assets/ui").mkdir(parents=True, exist_ok=True)
    (output / "assets/logo.png").write_bytes(sources["assets/lora-tunnel-radio-logo.png"])
    for name in ("home.png", "wardriver.png", "recon.png", "detail.png", "settings.png", "keyboard.png", "overview.png"):
        (output / "assets/ui" / name).write_bytes(sources["assets/ui/" + name])
    downloads = output / "downloads"
    downloads.mkdir()
    (downloads / factory_name).write_bytes(factory_data)
    (downloads / source_name).write_bytes(source_data)
    with zipfile.ZipFile(downloads / sd_name, "x", zipfile.ZIP_DEFLATED) as archive:
        safe_sd = ("sdcard/README.md", "sdcard/lora-tunnel/config.example.json",
                   "sdcard/lora-tunnel/detection_rules.json", "sdcard/lora-tunnel/detection_rules.example.json",
                   "sdcard/lora-tunnel/vendor-prefixes.csv", "sdcard/lora-tunnel/vendor-prefixes.README.md",
                   "sdcard/lora-tunnel/vendor-prefixes.LICENSE.txt")
        for name in safe_sd:
            archive.writestr(name.removeprefix("sdcard/"), sources[name])
    sums = {factory_name: digest(factory_data), source_name: digest(source_data), sd_name: digest((downloads / sd_name).read_bytes())}
    (downloads / "SHA256SUMS.txt").write_text("".join(f"{sha}  {name}\n" for name, sha in sums.items()), encoding="utf-8")
    (output / "LICENSE.txt").write_bytes(sources["LICENSE"])
    (output / "firmware").mkdir()
    manifest = {"name": "LORA Tunnel Beta", "version": version, "new_install_prompt_erase": True,
                "new_install_improv_wait_time": 0,
                "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "../downloads/" + factory_name, "offset": 0}]}]}
    (output / "firmware/manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    release = {"version": version, "channel": "beta", "target": TARGET,
               "flash_bytes": 16777216, "psram_bytes": 8388608, "flash_mode": "dio",
               "factory_file": factory_name, "factory_sha256": args.factory_sha256,
               "source_file": source_name, "source_sha256": args.source_sha256,
               "source_commit": args.commit, "github": github, "gitea": gitea,
               "relink_url": args.relink_url, "relink_sha256": args.relink_sha256,
               "physical_limits": ["GNSS position fix not obtained", "physical buttons pending",
                                   "service uploads not implemented", "complete Meshtastic payload decoding not implemented"]}
    (output / "release.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
    guides = {"README.md": "README.html", "sdcard/README.md": "SD_SETUP.html"}
    guides.update({"docs/" + name + ".md": name + ".html" for name in GUIDES})
    (output / "guides").mkdir()
    for source, dest in guides.items():
        rendered = markdown.markdown(sources[source].decode("utf-8-sig"), extensions=["extra", "toc", "sane_lists"])

        def rewrite(match):
            value = html.unescape(match.group(2))
            parsed = urlsplit(value)
            if parsed.scheme or parsed.netloc or not parsed.path:
                return match.group(0)
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(source), unquote(parsed.path)))
            if resolved in guides:
                link = guides[resolved]
            elif (resolved.startswith("assets/ui/") and resolved.endswith(".png")) or resolved == "assets/lora-tunnel-radio-logo.png":
                link = "../" + ("assets/logo.png" if resolved.endswith("radio-logo.png") else resolved)
            elif resolved == "LICENSE":
                link = "../LICENSE.txt"
            else:
                link = github + "/blob/" + args.commit + "/" + quote(resolved, safe="/")
            if parsed.fragment:
                link += "#" + parsed.fragment
            return match.group(1) + '="' + html.escape(link, quote=True) + '"'

        rendered = re.sub(r'(href|src)="([^"]*)"', rewrite, rendered)
        title = re.search(r"<h1[^>]*>(.*?)</h1>", rendered)
        title = re.sub("<[^>]+>", "", title.group(1)) if title else dest[:-5]
        page = ('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
                '<meta name="theme-color" content="#07120e"><title>' + html.escape(title) + ' · LORA Tunnel</title>'
                '<link rel="stylesheet" href="../style.css"></head><body><main class="doc">'
                '<a href="../index.html">← LORA Tunnel beta installer</a> · <a href="USER_GUIDE.html">User guide</a>'
                '<p class="small-note">Release ' + html.escape(version) + ' · Public beta · ' + html.escape(args.commit[:12]) + '</p>' + rendered + '</main></body></html>')
        (output / "guides" / dest).write_text(page, encoding="utf-8")
    check_links(output)
    inventory = {}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            if path.stat().st_size > MAX_FILE:
                raise ValueError("Cloudflare Pages per-file limit exceeded: " + path.name)
            inventory[path.relative_to(output).as_posix()] = digest(path.read_bytes())
    (output / "SITE-SHA256.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    print(f"Staged {version}: {len(inventory)} public files; local links, source version, image header and hashes checked")
    print(f"Factory SHA256 {args.factory_sha256}")
    print(f"Source SHA256 {args.source_sha256}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--factory", type=Path, required=True)
    parser.add_argument("--factory-sha256", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--github-url", required=True)
    parser.add_argument("--gitea-url", help="Omit while Gitea publication is blocked; account link is labelled pending")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--relink-url", required=True, help="Verified matching release source/relink companion download")
    parser.add_argument("--relink-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    build(parser.parse_args())


if __name__ == "__main__":
    main()
