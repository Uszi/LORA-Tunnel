# Native session format, schema 1

The primary record is `/sessions/LT-xxxxxxxx.jsonl` on microSD. Each complete
line is one UTF-8 JSON object. Preserve these native session files even after
export or upload: they retain observations and raw radio bytes that a mapping
CSV cannot represent. The formal structural contract is
`schemas/observation-v1.schema.json` (JSON Schema draft 2020-12).

## Common fields and time

| Field | Meaning |
| --- | --- |
| `schema` | Integer format version, currently 1. |
| `type` | `session_start`, `session_end`, `wifi`, `ble`, or `lora`. |
| `capture_mode` | `wardriver` or `recon`. Optional for older schema-1 files, whose missing mode is treated as wardriver by exporters. |
| `session` | Eight lowercase hex digits, generated per session. Filename collisions are rejected; this is not a device identifier or globally guaranteed UUID. |
| `seq` | Serialization sequence beginning at 1 in this session. A gap can indicate a serialization/drop condition; no gap does not prove every radio packet was received. |
| `monotonic_ms` | ESP32 monotonic time in milliseconds since boot. It can order observations without a GNSS clock, but does not supply UTC or bridge reboots. |
| `utc` | Latest sufficiently fresh GNSS sentence time, `YYYY-MM-DDTHH:MM:SSZ`, otherwise null. No boot-time-derived or invented UTC. |
| `utc_age_ms` | Age of the GNSS time sentence in milliseconds, otherwise null. This differs from position-fix age. |
| `timestamp_basis` | Current writer: `gnss_latest_sentence`. This is not a hardware timestamp of the received packet. |
| `gps` | Receiver fix snapshot at observation submission, described below. |

Current firmware only emits UTC when date and time sentences are valid and at
most 1500 ms old. It uses the last sentence's UTC rather than extrapolating a
wall clock between sentences. Multiple observations can share a second. Wi-Fi
scan results also contain `wifi.timestamp_basis:scan_result_delivery`: several
APs in a scan can share a delivery time and GPS snapshot. This does not claim
each beacon arrived at precisely that timestamp/location. `utc_age_ms` and the
top-level timestamp basis are optional in the schema for earlier v1 fixtures;
new firmware writes them explicitly.

`gps.valid` is true only for a currently accepted receiver fix, no older than
10000 ms; recent invalid RMC/GGA status invalidates it. `gps.age_ms` records
position age, or null if unavailable. When invalid, `lat`, `lon` and `alt_m`
are null. When valid, latitude/longitude are real in-range receiver coordinates;
altitude can still be null. `hdop` is dimensionless and may be null. `sats` is the
available satellite count; zero also represents unavailable/stale satellite
information in this milestone. GNSS altitude/HDOP/satellite sentence ages are
not separately retained yet. No current field is a guaranteed metre accuracy.

The optional extension `gps.accuracy_m` means an actual receiver-provided
horizontal accuracy in metres. Current firmware does not emit it. Exporters
must never convert HDOP directly to that field or fabricate zero accuracy.

## Session markers and counters

Start/end markers add `firmware`, `board` and `counts`. Counters include ingress
drops, log drops, storage errors, radio errors, UI skips and maximum observed
SD flush duration in milliseconds. Counter values are cumulative since boot;
they are not reset just because a new session starts. Compare marker snapshots
to calculate a session delta. Missing `session_end` means orderly closure was
not recorded; keep complete earlier lines and treat the tail as interrupted.

The live `written` implementation counter means records serialized into the
write buffer, not guaranteed durable records. A completed write/flush is stronger
evidence, and physical power-loss tests are still required. Drop counters cannot
count packets that the radio never received during another channel/dwell or a
contention interval. Storage failure can itself prevent final counters from
reaching the file; capture visible diagnostics where practical.

## Wi-Fi observation

`wifi` contains BSSID, SSID when available, channel, MHz frequency, RSSI in dBm,
security classification and the scan-result time basis. This build passively
scans 2.4 GHz channels 1–11; schema 1 permits up to channel 14 for future regional
configuration. Wi-Fi and BLE scanning are deliberately scheduled in turns.

Recon captures raw 802.11 frames separately from AP scans. Such records have
`wifi.wifi_frame:true`, `tx_address`, and `raw_frame_hex`, with no `bssid`.
The transmitter can be a client or NAN peer; it must not become an invented AP
identity. At most **1024 bytes** of a frame are retained after removing its FCS.
`original_len`, `captured_len`, `frame_truncated`, and `fcs_present:false` describe
that boundary. `timestamp_basis:packet_callback` distinguishes these records
from scan-result deliveries. The WiGLE exporter excludes raw Wi-Fi frames even
when an input record has a missing or incorrect capture mode.

Wi-Fi `ssid` is a bounded display-safe UTF-8 string, up to the 32-byte SSID
limit; invalid bytes are replaced by `?` and `ssid_sanitized` marks that change.
`ssid_hex` retains the original bytes returned by the ESP32 scan SDK before text
sanitization, up to 32 bytes. This is not a complete raw-beacon capture: an
embedded NUL can limit the SDK C-string result, so any suffix beyond it remains
unknown. An empty SSID means hidden/unavailable, not a fabricated name.
The WiGLE exporter recovers exact valid UTF-8 from `ssid_hex` when sanitization
was reported; if those bytes are missing, malformed or not valid UTF-8, it leaves
the exported SSID blank and counts that outcome. It never exports a replacement
`?` as though it were an observed name. `security` reflects the ESP32 scan
classification; it is not a complete negotiated-security audit. Preserve these
limitations when describing recon evidence or exporting names.

## BLE observation

`ble` contains address, address type, RSSI, bounded advertised name, raw
`payload_hex`, `payload_truncated`, and optional manufacturer ID or malformed
advertisement flag. Address types are `public`, `random`, `public_identity`,
`random_identity` or `unknown`, following the scanner's reported type. A random
address is an observation key, not proof of a persistent physical identity.

The raw payload retains at most **512 bytes**, represented as at most 1024 hex
characters. `payload_truncated:true` means more bytes were presented than kept.
`original_len` and `captured_len` retain those sizes. The optional
`advertising_len` preserves NimBLE's ADV/SCAN_RSP boundary within the captured
bytes; zero means no separate boundary is available. Current scans are passive,
but any concatenated input must be parsed as separate segments. Zero padding
ends only its own segment, and an AD element cannot consume the next segment.
Earlier schema-1 captures limited payloads to 255 bytes and remain valid.
The name is limited to 39 bytes plus terminator. Valid UTF-8 is preserved;
`name_sanitized` marks invalid-byte replacement in the display-safe string.
The CSV exporter leaves a sanitized BLE name blank, with a count, while the
native raw advertising bytes remain available for later name/service decoding.
Service/manufacturer structures can be interpreted later from those raw bytes.
The current convenience
`manufacturer_id` takes the little-endian ID from a valid manufacturer AD
element; if multiple such elements exist, the last parsed one wins. Raw payload
remains authoritative. An invalid AD length or segment boundary sets
`malformed_ad` and omits the convenience manufacturer ID without reading beyond
the buffer. This milestone does not flatten all services into separate
fields or persist a complete extended-advertisement stream beyond the bound.

## Recon evidence

Wardriver sessions retain observations for ordinary inventory/export. Recon
sessions retain matched observations in enabled categories with their evidence;
they are not complete Wi-Fi/BLE inventories. `recon` contains `category`,
`confidence`, `label`, `vendor`, `evidence`, and `payload_valid`. Confidence names
describe signature strength, not measured probabilities. Vendor matches and
spoofable identifiers do not establish a product, operator, purpose, or recording
state. Local rules and category/confidence settings control matches, display
and alerts. Offline WiGLE and WDGWars mesh exporters exclude Recon and unknown modes.

When actually decoded, `recon.remote_id` keeps a self-reported broadcast ID,
`id_type`, aircraft type, and whichever aircraft/operator coordinates, altitude
and speed are available in that observation. It records
`source:self_reported_broadcast` and `authenticated:false`; no broadcast
authentication has been established. Operator coordinates include the reported
`operator_location_type` and do not automatically mean a current pilot position.
Missing fields are omitted, not filled from earlier packets or receiver GPS.
Receiver location remains in the separate top-level `gps` object.

A parsed `recon.ibeacon` contains UUID hex, major/minor and measured power.
These values describe a common proximity-beacon format, not a permanent physical
identity or evidence of malicious activity. Native raw bytes remain available
for later interpretation.

## LoRa reception and candidate classification

`lora` contains the fixed receive profile: MHz frequency, kHz bandwidth,
spreading factor, coding-rate denominator, sync word, preamble symbols, profile
name and LNA setting. It also stores RSSI, SNR, packet length and raw received
bytes in `raw_hex`, bounded to **255 bytes**. `original_len` and `truncated` retain
any difference between supplied and retained lengths. LoRa RSSI is stored as an
integer, so fractional driver RSSI
resolution is not preserved; SNR remains floating point. The current band is
902–928 MHz. The receiver can listen to only one modem/frequency profile at a
time. Radio settings come from the session's fixed configuration; changes while
capturing/hopping are not implemented.

`radio_read_status:ok` records successful driver delivery.
`crc_status:not_reported` is deliberate. A successful RadioLib read is not an
independently retained CRC-observation flag. `payload_status:not_decoded` means
no payload parsing/decryption was performed; it does not claim all traffic is
encrypted. Unknown payload bytes remain useful and are preserved.

`classification` is `unknown` or `meshtastic_header_candidate` in this build.
A candidate adds `candidate_header` with eight-digit from/to/packet IDs,
channel hash, hop-limit/start, MQTT/ACK flags and raw next-hop/relay bytes.
These are a plausible interpretation of a packet's bytes, **not verified source
identity, authenticated protocol classification, decoded position or proof that
the original sender is near the receiver**. A header alone cannot justify a
human-readable node name, message or WDGWars mesh record. `relay_fields_available`
indicates the parser has a nonzero hop-start value; do not turn an unavailable
field into a zero-hop/direct claim.

Future decoded mesh extensions may use a top-level `mesh` object. The schema
allows that extension but does not infer it from `candidate_header` or validate
it as an authenticated sender. The experimental offline exporter has separate
strict eligibility rules in `docs/EXPORTS.md`. Any advertised node position
belongs in that extension with its source explicitly labelled; it must never
overwrite the receiver's `gps` object.

## Summaries, bounds and interrupted logs

The UI maintains **64 shared RAM summary slots** with least-recently-seen
replacement. Wi-Fi/BLE summaries use kind/address/type; LoRa summaries use a
clearly marked candidate sender or packet fingerprint. A summary eviction
discards only that RAM view. Observation logging is requested before summary
update, so eviction does not erase historical observations already captured in
the native log. Summary totals are bounded sightings, not a census of unique
physical devices or a full session database.

The current pipeline uses a **48-observation capture queue** and **32-observation
logger queue**, both with explicitly allocated PSRAM payload storage. The
observation's raw array has **1024 bytes**; acquisition/serialization bounds are
Wi-Fi 1024, BLE 512 and LoRa 255 bytes. The logger has an **8192-byte static JSON
document arena**, **8192-byte SD write buffer**, and **12288-byte writer stack**
(ESP-IDF specifies task stack sizes in bytes). Missing queue memory makes
initialization fail explicitly. Oversized/overflowing JSON records increment the
log-drop counter rather than overflowing the write buffer. Writer flushes occur
approximately every two seconds, on buffer pressure, and at orderly stop.
Queue overflow, malformed records and write errors have visible counters.
SD writes share a mutex with TFT operations; slow-card effects require hardware
measurement. These limits bound application allocations but do not prove total
framework heap use or watchdog behavior under load.

A JSONL parser should process complete lines independently. Ignore/report a
truncated final line without throwing away valid earlier records. Never append
a fresh session over an old session file. The Python exporter uses a separate
64 KiB maximum input-line limit so it can reject corrupt/untrusted oversized
records without unbounded memory growth; it streams the file and reports skips.

Schema validation checks structure, not all semantics. In particular, compare
LoRa byte length to `length`, Wi-Fi channel to frequency, timestamp freshness,
and protocol decode evidence in code. A syntactically valid example is not a
real observation. Files marked `synthetic:true` are test fixtures and must not be
uploaded.

## SD configuration

`schemas/config-v1.schema.json` describes `/lora-tunnel/config.json`. Implemented
sections include `lora`, `schedule`, `recon`, `ui.buttons_enabled`, and
`credentials`. A missing config uses this user's confirmed United States / 915
MHz defaults with US LongFast reception enabled. An existing invalid config
without a usable backup disables LoRa and reports the problem. A valid previous
configuration may be recovered from `config.bak`; saving credentials uses a
validated temporary file before promotion and preserves a usable backup.

Both config and the separate `/lora-tunnel/detection_rules.json` input have a
16 KiB file cap and fixed 16 KiB JSON arena. Parsed rule count is limited to 64;
rule types support exact case-insensitive name match, Wi-Fi SSID prefix,
OUI prefix, BLE service16 and manufacturer identifiers. BLE identifier rules
respect advertising boundaries and require valid AD structure. OUI matching
excludes randomized BLE and locally assigned addresses.
Vendor/name evidence is not proof of a device's purpose. Rule labels/matches
are bounded to 47 bytes. The rule file is separate from credentials.

Credential buffers hold at most 127 UTF-8 bytes each. JSON Schema `maxLength`
counts characters, so firmware byte limits remain authoritative for non-ASCII
input. Config strings longer than firmware buffers can be truncated on load;
keep them within their byte limits. Plaintext SD credentials are masked in UI
and excluded from logs/source control. Storing them does not mean firmware
uploads or auto-upload are implemented. This milestone has no upload scheduler
or network submission control; additional unrecognized config fields are
forward-compatible data, not active features.
