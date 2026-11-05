# Combined Meshtastic and MeshCore observation

LORA Tunnel 0.5.0 provides a shared LoRa capture path with separate Meshtastic and MeshCore decoders. Wardriver sessions can stay on one protocol or alternate between a selected profile for each protocol. The device saves received radio bytes and radio settings before attempting to decode them, so later software can revisit incomplete or unsupported results.

This document describes the source implementation. It does not certify a successful hardware capture, flash, drive, or service upload. The validation checklist below separates host checks from physical acceptance. The supported subset is listed explicitly; this is not a claim of complete Kismet decoder parity.

[Completed software checks and remaining physical acceptance](COMBINED_MESH_VALIDATION.md).

## Choose a mode

Select the correct region first. While the session is stopped, open **Settings** and select the **LoRa** mode control. Each selection cycles through **Meshtastic**, **MeshCore**, and **Both / slow scan**. The setting is saved to the SD configuration. A mode requiring MeshCore is rejected when the selected region has no supported MeshCore receive profile. Recon mode retains its Wi-Fi/BLE detection behavior; this LoRa feature belongs to Wardriver mode.

The LoRa view shows the active profile, decoded/other totals, and the time until the next profile change when combined scanning is active. Received nodes retain protocol-specific identities. Display summaries can combine later sightings of a node, while the observation log retains each received packet.

For a USB bench check, `python tools/bench_serial.py --port YOUR_PORT --command status --seconds 60` also shows a numeric `LT RADIO` record on 0.5.0 and newer builds. Mode 0 is Meshtastic, 1 is MeshCore and 2 is combined; active protocol 1 is Meshtastic and 2 is MeshCore. Frequency is in kHz and bandwidth in Hz. Per-protocol listen times, receive counts and switch/deferral counters let you check scheduling separately from packet decoding. These are firmware-reported settings and counters, not independent RF measurements. The helper retains only allowlisted numeric radio fields and does not print payloads, keys or locations. A radio record alone cannot acknowledge a command; a complete, fresh `LT STATUS` record remains required.

One SX1262 can listen to one frequency, bandwidth, spreading factor and sync word at a time. Meshtastic uses sync word `0x2B`; MeshCore uses `0x12`. Even when two communities use the same center frequency, those receive settings remain distinct. An opposite-profile transmission can be missed while the receiver listens elsewhere. Software cannot recover a packet that the radio never demodulated. [Kismet LoRaPipe receive and hopping documentation](https://www.kismetwireless.net/docs/readme/datasources/lora-lorapipe/)

Combined mode uses a configurable dwell of **30–600 seconds per profile**. The default is 30 seconds. Pending receive work is serviced before a change, and an observed in-progress packet can defer it within a bounded grace period. This avoids an unlimited stall caused by noise or an incomplete preamble. Longer dwell increases listening time on the selected profile but also increases the gap on the other profile. Sparse node announcements can require much longer observation; a quiet interval does not prove that no nodes are present. Profile locking is useful when comparing against a known transmitter.

## Receive profiles

Existing Meshtastic region settings continue to select their LongFast starting profile, with validated custom frequency/modem settings available in SD configuration. The following MeshCore selections are included. `AUTO` chooses the first listed profile for that region; it does not detect a community's settings automatically.

| Region | MeshCore selection | Frequency MHz | Bandwidth kHz | SF | Configured CR |
| --- | --- | ---: | ---: | ---: | --- |
| US / Canada | `US-NARROW` (`AUTO`) | 910.525 | 62.5 | 7 | 4/5 |
| US / Canada | `US-SOCAL` | 927.875 | 62.5 | 7 | 4/5 |
| Europe 868 | `EU_868-NARROW` (`AUTO`) | 869.618 | 62.5 | 8 | 4/8 |
| Europe 868 | `EU_868-SF7` | 869.618 | 62.5 | 7 | 4/5 |
| Europe 868 | `EU_868-LEGACY` | 869.525 | 250 | 11 | 4/5 |
| Australia / NZ 915 | `ANZ-DEFAULT` (`AUTO`) | 915.800 | 250 | 10 | 4/5 |
| Australia / NZ 915 | `ANZ-NARROW` | 916.575 | 62.5 | 7 | 4/7 |
| Australia / NZ 915 | `ANZ-MID` | 915.075 | 125 | 9 | 4/5 |
| Australia / NZ 915 | `ANZ-NZ_NARROW` | 917.375 | 62.5 | 7 | 4/5 |

These are receive presets derived from the [MeshCore FAQ](https://github.com/meshcore-dev/MeshCore/blob/81dbae592ffad40b4212bc0b37e82b38da08f4bc/docs/faq.md) and [Kismet channel reference](https://www.kismetwireless.net/docs/readme/datasources/lora-lorapipe/#channel-reference). Selection is checked against the target's supported band. The Cardputer U214 target is limited to 868–923 MHz; it cannot select `US-SOCAL`. Importing a valid Heltec configuration that requests an incompatible Cardputer frequency disables LoRa and preserves the rest of the configuration. Changing regions resets the MeshCore selection to `AUTO`; regions without an included MeshCore preset revert to Meshtastic mode.

## Meshtastic decoding

The local parser supports ordinary AES-CTR channel payloads with the published default AES-128 key, configured AES-128/AES-256 keys, and explicitly configured plaintext channels. It reads the bounded protobuf wire format directly; it does not embed Meshtastic's network stack or generated GPL schema files.

Supported application fields are:

- Node information: long name, short name, source ID from the radio header, and hardware/role values when those fields are present.
- Position: both advertised coordinates when present and valid, reported MSL altitude, timestamp, and available precision bits. An explicit `(0,0)` coordinate pair remains a valid pair; missing coordinates do not become zero coordinates.
- Text: ordinary text, detection-sensor text and alert text, bounded to 159 UTF-8 bytes for decoded display/log fields. Raw radio bytes retain the original content.
- Device telemetry: reported battery level and voltage, with optional message timestamp. Battery value `101` means external power, not 101 percent. Other telemetry variants remain unsupported.

Node names, positions and telemetry arrive in separate broadcasts. A packet does not imply that every field is available. The receiver does not query nodes, join channels, send acknowledgments or relay packets.

AES-CTR does not authenticate the sender. A valid protobuf result, matching channel hash or successful radio read is not proof of identity. Current XEdDSA signatures are reported as present and unverified. Optional authenticated AES-CCM channel mode, private-key-encrypted direct messages, compressed text and other application payloads are outside this decoder's current scope. Retained raw packets remain available. [Meshtastic encryption documentation](https://meshtastic.org/docs/overview/encryption/), [inspected message definitions](https://github.com/meshtastic/protobufs/tree/85f3f4357946a52daa5310285476d2dffb137c7b/meshtastic)

## MeshCore decoding

Public MeshCore node advertisements contain a public identity key, timestamp, signature and optional application fields. LORA Tunnel parses the available name, reported role and advertised position, and verifies the Ed25519 advert signature when the firmware crypto adapter is present. The signed data excludes mutable routing/path fields. A verified signature establishes that the advert was signed for that key; it does not establish the truth of its location, ownership, proximity or freshness.

The parser validates current one-, two- and three-byte path-hash layouts, transport-header lengths and payload bounds. It records packet type, path count and hash width. Supported acknowledgment shape is recognized without inventing a node identity. Encrypted message envelopes remain identifiable as encrypted/unknown or unsupported, and raw bytes remain available. MeshCore group-message decryption, direct-message decryption and other application-specific payload decoding are not included in this milestone. A displayed group sender name would require separate trust handling if group decoding is added later. [MeshCore packet format](https://github.com/meshcore-dev/MeshCore/blob/81dbae592ffad40b4212bc0b37e82b38da08f4bc/docs/packet_format.md), [MeshCore payload format](https://github.com/meshcore-dev/MeshCore/blob/81dbae592ffad40b4212bc0b37e82b38da08f4bc/docs/payloads.md)

## SD configuration

Start from [config.example.json](../sdcard/lora-tunnel/config.example.json), save the private working copy as `/lora-tunnel/config.json`, and choose your region on the device. New optional fields inside `lora` are:

```json
{
  "mode": "combined",
  "dwell_ms": 30000,
  "meshcore_profile": "AUTO",
  "decode_public": true,
  "meshtastic_keys": []
}
```

This fragment is not a complete configuration. Merge it into the existing `lora` object; preserve the region and other settings. Existing files without these fields retain Meshtastic-only behavior.

`decode_public` attempts the published Meshtastic default key. It also works with a renamed ordinary channel using that key, subject to supported framing. Setting it to false does not disable public MeshCore advert parsing or remove explicitly supplied Meshtastic keys.

`meshtastic_keys` accepts at most four objects, each containing:

- `name`: the exact, case-sensitive channel name, 1–39 UTF-8 bytes. The channel name can differ from the modem preset name.
- `key_hex`: the expanded AES key as exactly 32 or 64 hexadecimal characters. An empty string explicitly configures plaintext for that channel. Base64 shortcuts such as `AQ==` are not accepted in this field; the published default is already covered by `decode_public`.

Private channel keys are separate from WiGLE or WDGWars API credentials. Keys are read from the private SD configuration and are not included in observations, status output or examples. Configure these protocol keys through the SD file; the existing connection-key keyboard is for upload/network credentials. The firmware computes the eight-bit channel hash from the name and key and tries matching entries within a fixed bound. A hash collision does not establish identity or successful decryption.

## Logs and trust

The session schema remains version 1 with optional decoder fields. Each LoRa observation retains the **receive-time** profile snapshot: frequency, bandwidth, SF, configured coding rate, sync word, preamble and LNA setting. A later profile change cannot relabel a queued packet. Configured coding rate is not an independently measured received coding rate.

`lora.raw_hex`, RSSI/SNR, packet length and candidate header fields remain available. New fields include `lora.protocol`, `lora.decoder_version`, `lora.payload_status`, and `lora.decoded`. The decoded object carries authentication/signature status and only available fields. Text replacement or truncation is explicit. See [the observation schema](../schemas/observation-v1.schema.json).

Receiver location remains in `gps`. A node's broadcast location is in `lora.decoded.advertised_position`. Do not substitute one for the other. A received relay does not place the original sender near the receiver. An invalid signature does not erase the raw observation, but the firmware does not promote it to a trusted node summary.

These additions do not by themselves establish WiGLE LoRa support or a WDGWars mesh upload contract. Exporters and live-service acceptance remain separate work; original logs must be retained.

## Capability and validation ledger

| Capability | This implementation |
| --- | --- |
| Standalone Wi-Fi/BLE/LoRa logging | Existing Wardriver capture path retained |
| Raw LoRa bytes and receive-time settings | Preserved for both protocols |
| Meshtastic names, positions, ordinary text and device metrics | Supported with compatible plaintext/default/configured-key AES-CTR payloads |
| MeshCore public adverts | Parsed, with Ed25519 signature result reported separately |
| One selected profile for each protocol in one session | Supported through slow automatic rotation |
| Arbitrary preset lists / all Kismet regions | Not yet implemented; current MeshCore presets are listed above |
| MeshCore encrypted group text | Not yet implemented |
| Meshtastic optional AEAD and XEdDSA verification | Not yet implemented |
| Private PKI direct-message decryption | Not available to a passive observer without endpoint secrets |
| Kismet server, datasource protocol or PCAP-NG compatibility | Not supplied by these changes; local capture remains JSONL |
| Continuous reception of two differing radio profiles | Requires separate receiver hardware |

Host tests exercise malformed/truncated packets, bounded lengths, missing fields, signed-coordinate encoding, default-key/nonce handling, explicit private-key configuration, wrong-key results, MeshCore path formats, signature verification, configuration recovery semantics and the scheduler. Synthetic fixtures use invented node data and public/test keys. Firmware builds validate the pinned Heltec and Cardputer targets; compilation does not establish radio performance.

Before release acceptance, use a known transmitter for each protocol and compare its configured settings, name and advertised location against both raw and decoded records. Test each locked profile first, then combined mode, including a packet spanning a scheduled change. Verify that each queued record retains the profile that received it, observe counters and UI responsiveness during SD writes, repeat stop/start and region/mode changes, and inspect missing-GPS and storage-failure behavior. Measure packet coverage and memory/stack headroom on both targets. No physical results should be claimed until those checks have evidence.
