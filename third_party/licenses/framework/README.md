# Framework and runtime notices

These notices accompany LORA Tunnel's firmware and its corresponding-source
release assets. They supplement the direct-library license files in the parent
directory. The original application MIT license does not replace any of them.

`SOURCE-MANIFEST.json` identifies every retrieved notice, its SHA-256 and source.
ESP-IDF 5.5.5 is pinned to commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`, matching the installed Arduino
3.3.12 SDK package's version record. Submodule revisions were taken from that
exact Git tree, not from each repository's current branch.

| Material | Source and scope |
| --- | --- |
| `arduino/` | LGPL-2.1 text and original leading source notices from the installed Arduino-ESP32 3.3.12 core/built-in libraries. Complete corresponding source is supplied in the relink companion. Arduino files retain their individual copyright notices and component-specific terms. |
| `esp-idf/` | ESP-IDF Apache-2.0 license and retained FreeRTOS, Newlib, WPA supplicant and tinycrypt notices from the pinned SDK source. |
| `sdk-header/` | Original leading notices from installed ESP32-S3 SDK headers. This preserves additional component attribution, including managed-component headers; it is a notice collection, not a substitute for library source. Some collected components are not linked into this firmware. |
| `esp32-wifi-lib/`, `esp32c3-bt-lib/`, `esp-phy-lib/`, `esp-coex-lib/` | Exact Espressif radio-component licenses at the SDK's submodule revisions. These precompiled components have their own terms and are not relicensed under MIT or LGPL by this project. The S3 uses the C3-family Bluetooth controller package. |
| `lwip/`, `mbedtls/`, `protobuf-c/`, `cJSON/` | Retained networking/cryptographic/parser licenses at pinned SDK source revisions. Mbed TLS is used under its Apache-2.0 option; the Apache text is also present under `esp-idf/LICENSE`. |
| `tlsf/` | Unmodified header/source carrying the allocator's original BSD notice; retained at the pinned SDK revision because its license is embedded in the source. |
| `toolchain-runtime/` | Installed Xtensa toolchain 14.2.0+20260121 notices, including GCC Runtime Library Exception, libstdc++/libgcc license texts and Newlib notices. This does not indicate that compiler executables are included in the firmware or source companion. |

The LGPL source companion contains complete Arduino/TinyGPSPlus source and
the other direct library source packages, with original embedded notices.
The matching application archive supplies the MIT application and local
Recon/OpenDroneID source. See [RELINKING.md](../../../docs/RELINKING.md) for the
modified-library build/relink route and distribution layout.

Notice collection checked on 2026-11-01. The accompanying manifest and SDK
component lock provide traceable inputs; this is not a claim that every SDK
component is MIT licensed or that every component in its dependency catalog is
linked into LORA Tunnel. Tools and unused example components remain separately
licensed. Keep notices, component source and the corresponding-source download
available with the firmware release.
