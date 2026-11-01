# OpenDroneID core

- Upstream: https://github.com/opendroneid/opendroneid-core-c
- Commit: `849c03adbe0cdd68997a462fafab397edcfe53f3`
- Copied without modification: `libopendroneid/opendroneid.c`, `libopendroneid/opendroneid.h`, `LICENSE`.
- License: [Apache-2.0](LICENSE). Original copyright and SPDX notices remain in both source files. Intel Corporation, OpenDroneID contributors and other named authors retain their rights.
- Dragon Detector supplies its own bounded BLE/Wi-Fi transport framing in `firmware/include/remote_id.h`. Upstream's Wi-Fi transmitter is not compiled.
- `library.json` is the local PlatformIO packaging descriptor. `ODID_DISABLE_PRINTF` excludes optional upstream debug printers.

This dependency retains Apache-2.0; the application's MIT license does not replace it.

LORA Tunnel copied this exact dependency from Dragon Detector revision
`7a09930ee8ab112b9b87e94c1c9e47e5e6faaa21` on 2026-11-01, without modifying the
decoder or its license. The LORA Tunnel framing adapter is in `lib/recon`.

SHA-256:

- `src/opendroneid.c`: `60b0964f5f2a0dc13833eb6a304f7bd3f64bb9c227fae168c7caba53c82927c2`
- `src/opendroneid.h`: `a60b9b38c4fa82d7c85437dc11f57f0dea4ebf8bb4b90b3ff3c21c585ccd55f4`
- `LICENSE`: `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`
