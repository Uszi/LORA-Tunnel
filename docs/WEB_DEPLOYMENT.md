# Building and publishing the two-target installer

The installer is a static site at
https://valleytech-lora-tunnel.pages.dev/. The existing Shopify page embeds this
site; adding a firmware target does not require changing Shopify pages or menus.

## Prepare artifacts locally

Build/test current source, preserve the Cardputer factory/application pair and
hashes, then create source and dependency archives from the final local commit.
Keep the published Heltec 0.3.0-beta.1 factory, source, SD and dependency archives
unchanged. A current-source Heltec compile is a regression check, not a new
published Heltec image.

The schema-2 input catalog in [CARDPUTER_RELEASE.md](CARDPUTER_RELEASE.md) binds
each target independently to its version, source commit, source ZIP/hash,
factory/hash and matching dependency companion. The site source/commit is a
separate field so current shared presentation can coexist with older firmware.

Install the pinned host dependencies from `tools/site-requirements.txt`, then:

```sh
python tools/build_web.py --catalog dist/catalog-input.json --output build-output/web-cardputer-local --local-preview
```

Use a new empty output directory. The builder never deletes/overwrites a staged
site and does not publish. Dependency-source archives exceed the Pages 25 MiB
per-file limit and remain versioned GitHub release downloads.

The builder checks source archive membership, safe/unique names, every source
hash, and every source blob against the supplied commit. It also checks exact
firmware/dependency pairing, target memory/image details, blank shipped
credentials/rules, retained installer dependency hashes, all local HTML links
and the hosting file-size limit. Cardputer packaging additionally validates
ESP image checksums/digests, exact partitions, NVS blankness, application size,
board/version identity and factory/Launcher app equality.

## Preview and physical acceptance

`--local-preview` explicitly marks the result unpublishable in
`PUBLICATION.json`. Pending Cardputer installation requires all of: a preview
build, a loopback hostname, the `?acceptance=1` query and the hardware
confirmation. Serve only on loopback for the physical acceptance run.

Each target has its own ESP Web Tools manifest with one ESP32-S3 factory image
at offset zero. Both boards identify as ESP32-S3, so the selector—not automatic
chip detection—chooses the target. Changing hardware clears confirmation.
The original `release.json` and `firmware/manifest.json` URLs retain their
Heltec-only contract. Each target's guides/images are rendered from that
target's matching source, preserving old-release documentation.

The frontend checks metadata/manifest consistency; it does not independently
hash the downloaded binary. Packaging and downloaded-file verification provide
that integrity evidence. Leave the erase option unchecked when relying on a
scoped recovery backup. A factory install replaces Launcher; its application
download belongs in Launcher's SD installer.

Run Python/Node host checks, inspect the staged site at desktop and mobile
widths, then complete the physical and live-service acceptance record from
[CARDPUTER_RELEASE.md](CARDPUTER_RELEASE.md). Simulated browser/host tests cannot
satisfy physical flashing, reception, SD or live upload checks.

## Publish after acceptance

Set the Cardputer catalog entry's `public_enabled` to true and supply its actual
acceptance record. Build again **without** `--local-preview`. Production staging
rejects pending or mismatched Cardputer acceptance; `PUBLICATION.json` must say
`publishable: true` before deployment. Publish new GitHub/Cardputer assets and
the existing Gitea packaged distribution, then verify public file hashes.
Retain the separate source/relink materials alongside distributed firmware.

Use the existing Cloudflare Pages uploader to deploy the checked production
directory after review. Do not deploy a localhost preparation directory.
Keep account responses/logs private. Re-fetch public catalog, manifests,
release metadata, binaries, source/SD archives, guides, images and the external
dependency companion; compare hashes and check existing Heltec integration
URLs. Preserve unrelated projects and installation counters.

M5Burner submission, review and public catalog availability are separate
states. Submit only the checked Cardputer factory BIN at 0x0 after acceptance.
Use the prepared submission text and cover, then verify the public download
when approved. No Shopify edits are part of this release procedure.

## Installer attribution

ESP Web Tools 10.4.0 is pinned and self-hosted. Its dependency notices remain
under `web/vendor/esp-web-tools-10.4.0/notices`. Independent loader stubs are
GPL-2.0-or-later, with exact source/provenance under
`third_party/esp-web-tools-stubs`. Original LORA Tunnel code remains MIT;
dependencies retain their own terms. See [THIRD_PARTY.md](THIRD_PARTY.md).
