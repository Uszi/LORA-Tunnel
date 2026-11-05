#!/usr/bin/env python3
"""Stage a two-target installer from independently checked firmware/source artifacts.

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
TARGET = "heltec-v4-r8-tft-high-band"
MAX_FILE = 25 * 1024 * 1024
VERSION_RE = re.compile(r'#define TUNNEL_VERSION "([0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+)"')
GUIDES = ("USER_GUIDE", "UI_GUIDE", "HARDWARE", "WEB_FLASHER", "RECOVERY",
          "EXPORTS", "RECON", "THIRD_PARTY", "VALIDATION", "LOG_FORMAT",
          "ARCHITECTURE", "TEST_CHECKLIST", "GPS_DIAGNOSTICS", "LORA", "RELINKING", "REGIONS")


import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_cardputer as cardputer

HELTEC_FACTORY_SHA = "e6905bfe5521e2f9887a9628dcaf55870b8b44cba2c92c1f8e3c671ee1533a77"
HELTEC_SOURCE_SHA = "519d91704d2572c8b705912bb17105e1dc348e9e3a7db3141ef2f876e558dd85"
HELTEC_COMMIT = "50da5b915fb75d5e12b2bfaae6f7479a4fafec07"
TARGETS = {
    TARGET: {"name": "Heltec Expansion V2 / V4-R8", "flash_bytes": 16777216,
             "psram_bytes": 8388608, "filename": "Heltec-V4-R8-TFT-HIGH-BAND"},
    cardputer.TARGET: {"name": "Cardputer ADV + Cap LoRa-1262", "flash_bytes": 8388608,
                      "psram_bytes": 0, "filename": "Cardputer-ADV-LoRa1262"},
}
SAFE_SD = ("sdcard/README.md", "sdcard/lora-tunnel/config.example.json",
           "sdcard/lora-tunnel/detection_rules.json", "sdcard/lora-tunnel/detection_rules.example.json",
           "sdcard/lora-tunnel/vendor-prefixes.csv", "sdcard/lora-tunnel/vendor-prefixes.README.md",
           "sdcard/lora-tunnel/vendor-prefixes.LICENSE.txt")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked_repo(value, host):
    parsed = urlsplit(value)
    if (parsed.scheme != "https" or parsed.netloc != host or parsed.query or
            parsed.fragment or not re.fullmatch(r"/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", parsed.path)):
        raise ValueError(f"Expected an HTTPS repository URL on {host}")
    return value.rstrip("/")


def validate_factory(data, version, expected_sha, target=TARGET):
    if not re.fullmatch(r"[0-9a-f]{64}", expected_sha) or digest(data) != expected_sha:
        raise ValueError("Factory SHA-256 mismatch")
    if target == cardputer.TARGET:
        cardputer.validate_factory(data, version)
        return
    if target != TARGET:
        raise ValueError("Unknown hardware target")
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
        if config.get("lora", {}).get("meshtastic_keys", []) != []:
            raise ValueError("Source sample mesh keys must be empty")
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


def checked_file(value, expected, base):
    path = (base / value).resolve()
    data = path.read_bytes()
    if not re.fullmatch(r"[0-9a-f]{64}", expected) or digest(data) != expected:
        raise ValueError("Artifact SHA-256 mismatch: " + path.name)
    return path, data


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def copy_assets(sources, output):
    (output / "assets/ui").mkdir(parents=True, exist_ok=True)
    for name, data in sources.items():
        if name.startswith("assets/ui/") and name.endswith(".png"):
            dest = output / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
    (output / "assets/logo.png").write_bytes(sources["assets/lora-tunnel-radio-logo.png"])
    (output / "assets/vts-official-logo.png").write_bytes(sources["assets/vts-official-logo.png"])


def render_guides(sources, output, version, commit, github):
    guides = {"README.md": "README.html", "sdcard/README.md": "SD_SETUP.html"}
    guides.update({"docs/" + name + ".md": name + ".html"
                   for name in (*GUIDES, "CARDPUTER_ADV", "CARDPUTER_RELEASE", "CARDPUTER_TEST_PROCEDURE", "CARDPUTER_SOFTWARE_VALIDATION", "UPLOADS", "ALERTS")
                   if "docs/" + name + ".md" in sources})
    (output / "guides").mkdir(parents=True, exist_ok=True)
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
            elif resolved.startswith("assets/ui/") and resolved.endswith(".png"):
                link = "../" + resolved
            elif resolved in ("assets/lora-tunnel-radio-logo.png", "assets/vts-official-logo.png"):
                link = "../assets/" + ("logo.png" if resolved.endswith("radio-logo.png") else "vts-official-logo.png")
            elif resolved == "LICENSE":
                link = "../LICENSE.txt"
            else:
                link = github + "/blob/" + commit + "/" + quote(resolved, safe="/")
            if parsed.fragment:
                link += "#" + parsed.fragment
            return match.group(1) + '="' + html.escape(link, quote=True) + '"'

        rendered = re.sub(r'(href|src)="([^"]*)"', rewrite, rendered)
        title = re.search(r"<h1[^>]*>(.*?)</h1>", rendered)
        title = re.sub("<[^>]+>", "", title.group(1)) if title else dest[:-5]
        page = ('<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
                '<title>' + html.escape(title) + ' · LORA Tunnel</title><link rel="stylesheet" href="../style.css"></head>'
                '<body><main class="doc"><a href="../index.html">LORA Tunnel installer</a>'
                '<p class="small-note">Release ' + html.escape(version) + ' · ' + html.escape(commit[:12]) +
                '</p>' + rendered + '</main></body></html>')
        (output / "guides" / dest).write_text(page, encoding="utf-8")


def prepare_target(entry, base, github, local_preview):
    target = entry["target"]
    if target not in TARGETS:
        raise ValueError("Unknown hardware target")
    meta = TARGETS[target]
    version = entry["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+-beta\.\d+", version):
        raise ValueError("Expected explicit beta version")
    factory_path, factory = checked_file(entry["factory"], entry["factory_sha256"], base)
    source_path, source = checked_file(entry["source"], entry["source_sha256"], base)
    sources = read_source(source_path, version)
    commit = entry["source_commit"]
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected full source commit SHA")
    validate_commit(sources, commit)
    validate_factory(factory, version, entry["factory_sha256"], target)
    relink_path, relink = checked_file(entry["relink"], entry["relink_sha256"], base)
    if source_path.name != f"LORA-Tunnel-{version}-source.zip" or relink_path.name != f"LORA-Tunnel-{version}-relink.zip":
        raise ValueError("Use the versioned source and dependency companion filenames")
    with zipfile.ZipFile(relink_path) as archive:
        companion = json.loads(archive.read("LORA-Tunnel-relink/MANIFEST.json"))
        if companion["release"] != version or companion["matching_firmware"]["firmware.factory.bin"]["sha256"] != entry["factory_sha256"]:
            raise ValueError("Dependency companion does not match this firmware")
    relink_url = github + "/releases/download/v" + version + "/" + relink_path.name
    public_enabled = entry.get("public_enabled") is True
    app, sd_data = None, None
    validation_status = "scoped-existing-release"
    if target == "cardputer-adv-lora1262":
        _, app = checked_file(entry["app"], entry["app_sha256"], base)
        cardputer.validate_factory(factory, version, app)
        if public_enabled:
            if not entry.get("acceptance"):
                raise ValueError("Cardputer public release requires acceptance or scoped beta authorization")
            acceptance = json.loads((base / entry["acceptance"]).read_text(encoding="utf-8"))
            validation_status = cardputer.validate_publication(acceptance, entry["factory_sha256"], entry["app_sha256"])
        elif not local_preview:
            raise ValueError("Cardputer hardware acceptance is pending; use --local-preview for local preparation only")
    else:
        # The original Heltec binary/source pair is preserved, not rebuilt or relabelled.
        if version != "0.3.0-beta.1" or entry["factory_sha256"] != HELTEC_FACTORY_SHA or commit != HELTEC_COMMIT or entry["source_sha256"] != HELTEC_SOURCE_SHA:
            raise ValueError("Heltec catalog entry must preserve the published 0.3.0-beta.1 release")
        if entry["relink_sha256"] != "8d2d82cd3b9c76d54885889f56d1f3773d62ffd6555c1541b120c0502899326f":
            raise ValueError("Preserve the published Heltec dependency companion")
        _, sd_data = checked_file(entry["sd"], "aa23af1f6c096b45d0c43f82f8cbca2047ba6535c9ed2f3ab0606bee5dd83a33", base)
    factory_name = f"LORA-Tunnel-{version}-{meta['filename']}-FACTORY-0x0.bin"
    app_name = f"LORA-Tunnel-{version}-{meta['filename']}-LAUNCHER-APP.bin" if app is not None else None
    release = {"target": target, "name": meta["name"], "version": version, "channel": "beta",
               "flash_bytes": meta["flash_bytes"], "psram_bytes": meta["psram_bytes"], "flash_mode": "dio",
               "factory_file": factory_name, "factory_sha256": entry["factory_sha256"],
               "source_file": source_path.name, "source_sha256": entry["source_sha256"], "source_commit": commit,
               "sd_file": f"LORA-Tunnel-{version}-SD-setup.zip",
               "relink_url": relink_url, "relink_sha256": entry["relink_sha256"],
               "public_enabled": public_enabled, "hardware_validation": validation_status if public_enabled else "pending",
               "guide": f"targets/{target}/guides/" + ("CARDPUTER_ADV.html" if app is not None else "USER_GUIDE.html"),
               "manifest": f"firmware/{target}/manifest.json"}
    if app_name:
        release.update(app_file=app_name, app_sha256=entry["app_sha256"], app_bytes=len(app))
    return release, factory, app, source, sources, sd_data


def build(args):
    base = args.catalog.resolve().parent
    spec = json.loads(args.catalog.read_text(encoding="utf-8"))
    if spec.get("schema") != 2:
        raise ValueError("Expected catalog schema 2")
    github = checked_repo(spec["github"], "github.com")
    gitea = checked_repo(spec["gitea"], "git.churchofmalware.org")
    site = spec["site"]
    site_path, _ = checked_file(site["source"], site["source_sha256"], base)
    site_sources = read_source(site_path, site["version"])
    validate_commit(site_sources, site["source_commit"])
    entries = spec["targets"]
    if len(entries) != 2 or {e["target"] for e in entries} != set(TARGETS):
        raise ValueError("Catalog must contain exactly the preserved Heltec and Cardputer ADV targets")
    prepared = [prepare_target(entry, base, github, args.local_preview) for entry in entries]
    output = args.output.resolve()
    if output == ROOT or (output.is_relative_to(ROOT) and output.relative_to(ROOT).parts[0] not in ("build-output", "dist")):
        raise ValueError("Choose an empty build-output/dist folder or a folder outside the source tree")
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output must be empty; existing sites are never deleted or overwritten")
    output.mkdir(parents=True, exist_ok=True)
    for name, data in site_sources.items():
        if name.startswith("web/") and not any(p.startswith(".") for p in PurePosixPath(name).parts[1:]):
            path = output / name.removeprefix("web/")
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.suffix == ".html":
                content = data.decode("utf-8")
                for key, value in {"VERSION": site["version"], "GITHUB": github, "GITEA": gitea}.items():
                    content = content.replace("@@" + key + "@@", html.escape(value, quote=True))
                if "@@" in content:
                    raise ValueError("Unresolved public HTML template token")
                path.write_text(content, encoding="utf-8")
            else:
                path.write_bytes(data)
    vendor = output / "vendor/esp-web-tools-10.4.0"
    upstream = json.loads((vendor / "UPSTREAM.json").read_text(encoding="utf-8"))
    if upstream["version"] != "10.4.0" or any(digest((vendor / n).read_bytes()) != h for n, h in upstream["files_sha256"].items()):
        raise ValueError("Installer dependency hash mismatch")
    copy_assets(site_sources, output)
    (output / "LICENSE.txt").write_bytes(site_sources["LICENSE"])
    render_guides(site_sources, output, site["version"], site["source_commit"], github)
    downloads = output / "downloads"
    downloads.mkdir()
    releases = []
    for release, factory, app, source, sources, sd_data in prepared:
        (downloads / release["factory_file"]).write_bytes(factory)
        (downloads / release["source_file"]).write_bytes(source)
        if app is not None:
            (downloads / release["app_file"]).write_bytes(app)
        if sd_data is not None:
            (downloads / release["sd_file"]).write_bytes(sd_data)
        else:
            with zipfile.ZipFile(downloads / release["sd_file"], "x", zipfile.ZIP_DEFLATED) as archive:
                for name in SAFE_SD:
                    info = zipfile.ZipInfo(name.removeprefix("sdcard/"), (1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, sources[name])
        release.update(github=github, gitea=gitea)
        manifest = {"name": "LORA Tunnel — " + release["name"], "version": release["version"],
                    "new_install_prompt_erase": True, "new_install_improv_wait_time": 0,
                    "builds": [{"chipFamily": "ESP32-S3", "parts": [
                        {"path": "../../downloads/" + release["factory_file"], "offset": 0}]}]}
        write_json(output / release["manifest"], manifest)
        write_json(output / "firmware" / release["target"] / "release.json", release)
        target_dir = output / "targets" / release["target"]
        copy_assets(sources, target_dir)
        (target_dir / "style.css").write_bytes(site_sources["web/style.css"])
        (target_dir / "LICENSE.txt").write_bytes(sources["LICENSE"])
        (target_dir / "index.html").write_text('<!doctype html><meta charset="utf-8"><a href="../../index.html#install">Choose hardware and install LORA Tunnel</a>', encoding="utf-8")
        render_guides(sources, target_dir, release["version"], release["source_commit"], github)
        if release["target"] == TARGET:
            # Existing integration URLs keep their Heltec-only contract.
            legacy_manifest = json.loads(json.dumps(manifest))
            legacy_manifest["builds"][0]["parts"][0]["path"] = "../downloads/" + release["factory_file"]
            write_json(output / "firmware/manifest.json", legacy_manifest)
            write_json(output / "release.json", release)
        releases.append(release)
    sums = {p.name: digest(p.read_bytes()) for p in sorted(downloads.iterdir())}
    (downloads / "SHA256SUMS.txt").write_text("".join(f"{h}  {n}\n" for n, h in sums.items()), encoding="ascii")
    catalog = {"schema": 2, "site_version": site["version"], "site_commit": site["source_commit"],
               "local_preview": args.local_preview, "targets": releases}
    write_json(output / "catalog.json", catalog)
    write_json(output / "PUBLICATION.json", {"publishable": not args.local_preview and all(r["public_enabled"] for r in releases),
                                             "reason": "Local acceptance preparation only" if args.local_preview else "Release authorization and artifact bindings checked"})
    check_links(output)
    inventory = {}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            if path.stat().st_size > MAX_FILE:
                raise ValueError("Cloudflare Pages per-file limit exceeded: " + path.name)
            inventory[path.relative_to(output).as_posix()] = digest(path.read_bytes())
    write_json(output / "SITE-SHA256.json", inventory)
    print(f"Staged {len(releases)} targets, {len(inventory)} files; local_preview={args.local_preview}; no publication performed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True, help="Local artifact input catalog; see docs/CARDPUTER_RELEASE.md")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--local-preview", action="store_true", help="Prepare unaccepted hardware locally; never deploy this output")
    build(parser.parse_args())


if __name__ == "__main__":
    main()
