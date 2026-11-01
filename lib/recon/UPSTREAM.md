# Passive Recon source and evidence

The LORA Tunnel adapter in `src/TunnelRecon.cpp` uses the owner's current
MIT-licensed Dragon Detector implementation. It does not contain SquachWatch
GPL application source. The original copyright and permission notice is in
`LICENSE-DragonDetector`; relevant inherited Marauder and Flock Noir notices are
in `notices/`. `DRAGON-CREDITS.md` is an archival copy of the source project's
credits, including references to files and features outside this small import.

Imported source snapshot:

- Source project: Dragon Detector / DragonDetector-VectorPotential, maintained by Valleytech Solutions.
- Git revision: `7a09930ee8ab112b9b87e94c1c9e47e5e6faaa21`.
- Original files: `firmware/include/detection.h`, `axon.h`, `remote_id.h`, and
  `vendor_prefixes.h`. Original SHA-256 values are in `source-manifest.json`.
- Public-fact data: `data/vendor-prefixes.csv`, 201 prefix rows. The embedded
  table retains per-row evidence and conservative confidence. This CSV is a
  source dataset; this library does not read the SD card or change the existing
  device configuration format.
- Coverage ledger: `data/squachwatch-coverage.json`, 158 identifier facts checked
  against SquachWatch commit `3e9be6c02265b14c798e58da01a0913866143b0f`:
  110 address patterns, 25 UUIDs, 8 company IDs, 8 SSID rules, and 7 names.

LORA Tunnel changes dated 2026-11-01: bounded public adapter and result types;
removed the unrelated adult-device parser from this subset; suppress all
vendor/prefix inference for randomized or locally administered addresses;
retain Remote ID ID type, aircraft type, operator-location type, and separate
altitude/speed validity; expose the complete iBeacon UUID, major/minor and
signed measured power. The API accepts no more than 1,024 Wi-Fi bytes or 512
BLE bytes. It performs no radio operations, allocation, logging or uploads.

The adjacent `lib/opendroneid` is the unmodified Apache-2.0 decoder pinned to
`849c03adbe0cdd68997a462fafab397edcfe53f3`. PlatformIO discovers this local library
through the `<opendroneid.h>` include; no registry dependency or wildcard is
declared. Its source, notice, license and hashes are retained there.

## Interpretation

The categories are observed evidence, never an assertion that a person is
recording or that a device is malicious. Vendor matches remain candidates.
Find My can describe an AirTag or another accessory; Apple pairing-size
advertisements can also be AirPods. iBeacon is a common benign broadcast
format. Remote ID is self-reported and unauthenticated: takeoff, fixed and live
operator locations are distinct, and none are receiver GNSS coordinates.

The underlying source can classify BLE services/manufacturer data/names,
Wi-Fi beacons, probes and data-transmitter addresses, and Remote ID carried
by BLE service data, Wi-Fi beacon vendor elements or supported NAN service
descriptors. A normal AP scan has no probe/client frames or raw Remote ID
elements. Actual reception depends on the caller supplying those transports.
This library does not enable BLE extended/coded scanning, 5 GHz reception,
Bluetooth Classic, or active queries. No physical reception is established by
the synthetic tests.

The wrapper intentionally rejects malformed complete AD/frame payloads instead
of preserving a partial Axon match from a damaged packet. Its results are
stateless: missing fields in the current observation remain missing. A caller
that accumulates device summaries must maintain independent field ages.

## Validation

`test/test_recon/test_main.cpp` uses synthetic packet fixtures and the pinned
decoder's encoders. Direct MinGW GCC/G++ compilation with C++14 and Unity passed
14 tests on 2026-11-01. The tests cover candidate confidence, random addresses,
Axon company/service and marker evidence, custom versus assigned UUIDs,
iBeacon fields, ambiguous Apple accessories, Find Hub frame length, malformed
AD/segment boundaries, Remote ID message and pack lengths, binary ID type,
invalid altitude/speed, takeoff-location type, directed probes and bounded
inputs, Flock accessory versus name evidence, and nested NAN lengths. Firmware
compilation and device acceptance are separate checks owned
by the application integration.

On the native 32-bit compiler, `Result` is 320 bytes and the OpenDroneID decode
workspace is 920 bytes on the stack. Target ABI sizes may differ.
