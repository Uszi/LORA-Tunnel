# Building and publishing the beta installer

The installer is a static HTTPS site at `https://valleytech-lora-tunnel.pages.dev/`.
The existing Valleytech Shopify site provides the project page and directory
cards. Deployment does not flash hardware or enable firmware uploads.

## Build a release site

First build/test the firmware, preserve the final factory image and hashes,
commit the public source, and create its source archive with
`tools/package_source.py`. Publish the matching dependency-source/relinking
companion with the same GitHub release. The companion exceeds Cloudflare Pages'
25 MiB per-file limit, so it remains a direct release download.

Install `tools/site-requirements.txt`, then run `python tools/build_web.py --help`
for the required inputs. Supply the final factory file and SHA-256, matching
source ZIP and SHA-256, full source commit, public GitHub repository URL,
verified relinking release URL/hash, and a new empty output directory such as
`build-output/web-0.3.0-beta.1`. Supply `--gitea-url` only after that repository is
published; omission produces an explicitly pending Gitea account link.

The builder checks:

- Factory SHA-256, ESP32-S3 image header, DIO flash mode, 16 MB flash field,
  application offset and expected beta version.
- Source ZIP SHA-256, unique safe filenames, exact `SOURCE-SHA256.json`
  membership and every file hash.
- Every source/archive file against the supplied Git commit's blob hash.
  A different source archive with the same version string is rejected.
- Empty sample credentials/rules; only explicitly allowed SD examples enter
  the SD starter archive.
- Installer dependency hashes, local HTML links and the hosting file-size
  limit. Web templates, documentation, UI previews and logo are taken from the
  matched source ZIP, not a later working copy.

It writes a one-build ESP Web Tools manifest for `ESP32-S3` and a single merged
factory image at offset 0. `new_install_prompt_erase=true` presents the erase
choice; no Improv provisioning is requested. Leave **Erase device** unchecked
when using scoped recovery; whole-chip erase requires a verified full backup.

The frontend checks release/manifest consistency and requires hardware
confirmation. It does **not** independently hash the binary in the browser.
The packaging and post-deployment download checks provide the recorded file
integrity evidence.

## Validate and publish

Run Python tests plus `node --test tests/test_web_app.cjs`. The latter uses a
minimal simulated DOM and never opens a browser or serial port. Inspect the
staged page, manifest, guides, credits and all artifact hashes before deploying.
Host checks and manifest consistency do not prove physical browser flashing.

Use the authenticated Cloudflare Pages API for project management. Pages direct
upload accepts a directory through the existing Wrangler Pages uploader; no
Wrangler project configuration is required or generated. Use a preview branch
first when reviewing a new deployment, then explicitly publish the checked
directory to the project's `main` production branch. Keep account credentials,
API responses and deployment logs outside the public source inventory.

After deployment, fetch the public manifest, release metadata, binary, source
archive and SD archive. Compare downloaded hashes to the local staged files;
check guide/images/script URLs and relevant iframe/Web Serial headers. Recheck
the external relinking asset as well. Do not reset other projects' install
counters or change their firmware/assets.

For Shopify, save the complete current bodies privately, validate the Admin
GraphQL page operations, create/update `/pages/lora-tunnel`, and append only its
card to the existing Firmware Flashers and Code & Projects grids. Preserve the
rest of each body exactly. The embedded installer delegates Web Serial to the
exact Pages origin and provides a direct-tab fallback. Re-read the published
records and fetch the public pages after mutation.

## Installer attribution

ESP Web Tools 10.4.0 is pinned and self-hosted. Its production dependency
notices are under `web/vendor/esp-web-tools-10.4.0/notices`. The independent
legacy serial-loader programs are GPL-2.0-or-later; exact corresponding source
trees and provenance are under `third_party/esp-web-tools-stubs`. Original
LORA Tunnel code remains MIT licensed. See [THIRD_PARTY.md](THIRD_PARTY.md).
