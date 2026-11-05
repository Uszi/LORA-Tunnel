# LoRa reception and source findings

Source review date: 2026-11-01. This document separates inspected upstream behavior, proposed device behavior, and implemented parser behavior. A successful build or parser test does not establish radio reception on the physical kit.

The original fixed-profile milestone below is extended in **0.5.0-dev** by [combined Meshtastic/MeshCore reception and local decoding](COMBINED_MESH.md), researched 2026-11-05. That guide is the current supported-decoder/profile contract. Published 0.3/0.4 binaries retain the earlier header-only behavior.

## What LoRaPipe and Kismet actually do

The inspected LoRaPipe revision is [`4ae9f86bb8256f8084f50577128b0facdca7bee2`](https://github.com/krak3n77/lorapipe/tree/4ae9f86bb8256f8084f50577128b0facdca7bee2). Its microcontroller configures the radio through RadioLib, reads raw packet bytes, and emits received bytes with RSSI/SNR and a local clock timestamp. The CLI record is `timestamp,RXLOG,rssi,snr,hex`; KISS serial framing is another operating mode. Its command interface also includes transmission and persistent radio configuration. Those transmit paths are unnecessary for LORA Tunnel. See [firmware receive output](https://github.com/krak3n77/lorapipe/blob/4ae9f86bb8256f8084f50577128b0facdca7bee2/examples/simple_repeater/main.cpp) and [RadioLib receive wrapper](https://github.com/krak3n77/lorapipe/blob/4ae9f86bb8256f8084f50577128b0facdca7bee2/src/helpers/radiolib/RadioLibWrappers.cpp).

The inspected Kismet revision is [`28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd`](https://github.com/kismetwireless/kismet/tree/28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd). Its host capture helper opens the serial port, selects radio settings, controls channel changes, associates incoming lines with the active profile, and supplies a host timestamp. The server turns those records into LoRaTAP metadata, routes them by the configured sync word, and runs Meshtastic parsing, known-channel decryption, node tracking, and storage/UI services. See [capture helper](https://github.com/kismetwireless/kismet/blob/28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd/capture_lorapipe/capture_lorapipe.c), [datasource conversion](https://github.com/kismetwireless/kismet/blob/28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd/datasource_lorapipe.cc), and [Meshtastic decoder](https://github.com/kismetwireless/kismet/blob/28a48f1349a0d9c4e80a9435ff9ce75a002a4bcd/phy_meshtastic.cc).

LORA Tunnel adopts the separation between raw radio reception and later interpretation. It uses a local receive queue instead of a serial connection to Kismet. The standalone device must own GNSS association, session timing, parser state, bounded device summaries, SD logging, touchscreen rendering, and exporters. Kismet is not an embedded runtime dependency. No LoRaPipe or Kismet implementation code was copied into the header parser.

## Fixed receive profiles first

Fresh installations now require an explicit region choice; they do not default to the developer's US location. The Settings/first-setup selector uses the verified profiles and persistence rules in [REGIONS.md](REGIONS.md). This high-band build accepts 18 regional profiles in 863-928 MHz and keeps seven lower-band definitions unavailable. It remains receive-only and selects one fixed profile at a time.

The SX1262 tunes one frequency/bandwidth/spreading-factor/sync-word combination at a time. A configured sync word is useful classification evidence; it is not a cryptographic protocol or sender identity check. The LoRa explicit header communicates coding rate, so the configured transmit coding rate and actual received coding rate should not be conflated. Record an actual received value only if the driver provides it. [Kismet LoRaPipe documentation](https://www.kismetwireless.net/docs/readme/datasources/lora-lorapipe/)

Verified standard Meshtastic LongFast examples:

| Region/profile | Center frequency | Bandwidth | SF | Configured CR | Sync word | Preamble |
| --- | --- | --- | --- | --- | --- | --- |
| US / LongFast, default channel | 906.875 MHz | 250 kHz | 11 | 4/5 | `0x2B` | 16 symbols |
| EU_868 / LongFast, default channel | 869.525 MHz | 250 kHz | 11 | 4/5 | `0x2B` | 16 symbols |

The frequencies are confirmed by the [Kismet channel reference](https://www.kismetwireless.net/docs/readme/datasources/lora-lorapipe/#channel-reference) and [Meshtastic radio settings](https://meshtastic.org/docs/overview/radio-settings/). The current upstream firmware defines LongFast at SF11/BW250/CR4/5 in [MeshRadio.h](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/src/mesh/MeshRadio.h), uses a 16-symbol sub-GHz preamble in [RadioInterface.h](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/src/mesh/RadioInterface.h), and enables CRC in [SX126xInterface.cpp](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/src/mesh/SX126xInterface.cpp).

These are profile examples, not an automatic region decision. A product marketed as “915 MHz” does not establish the deployment country or a network's actual channel. Country, antenna/hardware variant, and selected local network settings must agree before physical reception testing. Custom channel names and manually selected frequency slots can move a network away from the default. The initial implementation should remain on one selected profile, with no transmit, acknowledgements, rebroadcasting, discovery packets, or mesh joining.

The development implementation supports a locked profile or slow rotation between one selected Meshtastic and one selected MeshCore profile, with configurable 30–600 second dwell and bounded in-progress-packet deferral. The default is an initial measurement setting, not a coverage guarantee. Sparse traffic may require minutes of dwell, and a whole packet can be missed during every other profile's interval. The session-end record retains listening time per protocol, profile changes, deferrals and interruptions. Known-transmitter validation remains necessary.

## Header parser contract

`lt::parseMeshHeader(const uint8_t*, size_t, lt::MeshHeader&)` is an original, allocation-free C++17 parser in `include/mesh_header.h`. It checks a nonnull buffer and the 16..255-byte SX1262 frame length range, reads fields with explicit little-endian byte operations, and leaves output unchanged on failure. It tolerates an unaligned input pointer. A 16-byte input can yield a header candidate even though it contains no payload.

Success establishes only that a buffer fits the header layout. There is no magic signature in those 16 bytes. Callers must not convert every successful parse into a verified Meshtastic detection or a named node. Use `meshtastic_candidate` only with matching receive-profile evidence and successful radio read/CRC; retain `unknown_lora` otherwise. A subsequent validated payload decoder can raise confidence. Unknown or undecoded payloads remain `not_decoded`; ciphertext, names, messages, and advertised positions are not inferred from header bytes.

The inspected upstream [wire header](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/src/mesh/RadioInterface.h) has destination, source, and packet ID at byte offsets 0, 4, and 8 (32-bit little-endian); flags at 12; channel hash at 13; next-hop byte at 14; relay-node byte at 15. The channel hash is an 8-bit decoding hint and can collide. It is not a channel name or encryption key.

Flags preserve remaining hop limit (bits 0..2), acknowledgement request (bit 3), MQTT origin flag (bit 4), and starting hop limit (bits 5..7). The parser retains raw next-hop/relay bytes. Downstream code should mark them unavailable when `hop_start == 0`, matching the compatibility behavior in [RadioLibInterface.cpp](https://github.com/meshtastic/firmware/blob/86d4d4a06c805bc0ccd812078c0ed1d8eb8a4c54/src/mesh/RadioLibInterface.cpp). These one-byte relay hints are not complete node identifiers. A difference between starting and remaining hop limits is at most a claimed path metric, not a measured distance or authenticated route.

Native tests cover field endianness, flag extraction, every truncated header size, null pointers, impossible lengths, unchanged output on failure, maximum-length frames, and unaligned input. They also explicitly document that arbitrary zero bytes can fit the layout. Tests use synthetic data, not captures.

The native target uses C++14. Install a host C++ compiler and make it available on PATH, then run from the project directory:

```sh
python -m platformio test -e native -f test_mesh
```

The recorded Windows checks used MinGW GCC 5.1. The native target does not force a Windows compiler package on Linux/macOS. Embedded firmware uses its separately pinned ESP32 toolchain.

## Observation and summary requirements

Each raw observation should retain session ID, sequence number, monotonic capture time, nullable UTC plus time source/validity, receiver GNSS snapshot and age, profile ID, center frequency, bandwidth, SF, configured CR, sync word, preamble/header/CRC settings, driver result and CRC status, packet length, RSSI, SNR, and full received bytes as hex or base64. Add receive-frequency error or actual received CR only when supported by the driver.

Classification fields should include protocol candidate, confidence, parser revision, payload status, and the parsed header fields when available. Preserve radio-profile generation alongside the packet so a later profile change cannot relabel a queued packet. Preserve malformed frames where practical, with explicit errors; never feed a truncated packet into a decoder as if complete.

Receiver coordinates belong under `receiver_location`. Any decoded mesh position belongs under `advertised_location`, together with its source node ID and advertised timestamp if present. Hearing a relay does not locate the original sender. Keep invalid/stale fixes explicit and never substitute `(0,0)` or an advertised mesh position for the receiver fix.

Use a bounded queue between radio service and logging. The ISR should only notify a task; the task should read bytes/metadata, return the radio to RX, and enqueue without waiting for SD. Separate counters must expose radio read/CRC errors, queue overflow, storage failures, and malformed decode attempts. Device summaries can suppress repeated display rows while every useful timestamped sighting remains in the observation stream. A source-ID/packet-ID/payload fingerprint can group apparent retransmissions; retain their distinct RSSI, receiver position, time and hop hints.

## Follow-on decoding

Meshtastic normally encrypts application data, including the default public channel. Knowing the published default channel key can permit decoding that channel; it does not unlock private channels or direct-message encryption. Kismet's known-key decoder is host-side and separately licensed. The original header-only milestone is now extended by original bounded Meshtastic AES-CTR/protobuf and MeshCore public-advert parsers. See [COMBINED_MESH.md](COMBINED_MESH.md) for exact supported messages, configured-key handling, signature verification and remaining gaps.

For a first physical acceptance test, use a known Meshtastic transmitter with matching region/profile. Compare its packet ID to a receiver log, inspect the complete raw bytes and RSSI/SNR, and confirm that LORA Tunnel never transmits. Repeat with GNSS missing, SD missing/full, a long maximum-size packet, rapid repeats, and interrupted power. Neither source inspection nor successful native tests prove these conditions pass.
