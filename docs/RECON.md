# LORA Tunnel Recon

Wardriver is the primary capture mode. Recon uses passive observations to
classify particular broadcast formats and vendor/name evidence. Selecting
Recon changes its display, filtering and alert behavior; a category name does
not establish that a device is recording, that it is hostile, or who owns it.

The classifier is based on the owner's MIT Dragon Detector parsers and the
Apache-2.0 OpenDroneID decoder. SquachWatch is the feature and signature-fact
reference; its GPL application implementation is not included. Exact source
revisions, hashes and notices are in `lib/recon/UPSTREAM.md` and
`lib/opendroneid/UPSTREAM.md`.

## What a result means

| Category key | Observed evidence | Interpretation |
| --- | --- | --- |
| `flock` | Flock accessory service, battery/name patterns, transmitter vendor facts and a composite probe fingerprint | A specific accessory signature is stronger than a shared module prefix. An accessory does not prove an adjacent camera. |
| `alpr` | Motorola/Genetec and related registered vendor prefixes | ALPR-vendor candidate; other products from those vendors exist. |
| `axon` | TASER/Axon company/service IDs, BWCDEVICE payload marker, body-camera vendor facts, setup names | Company + service is stronger than either alone. Model and recording state remain unknown. |
| `ibeacon` | Valid Apple iBeacon manufacturer block | UUID, major, minor and measured power are available. Ordinary proximity beacons are common and often benign. |
| `ring` / `camera` | Registered vendor prefixes or camera-like names | Camera-vendor candidate. Generic radio or parent-company prefixes receive weaker grades. |
| `drone` | Decoded BLE, Wi-Fi beacon or supported NAN Remote ID; otherwise drone-vendor evidence | A decoded broadcast supplies reported aircraft fields. Vendor evidence alone does not establish an aircraft or flight. |
| `airtag` | Find My format or Apple pairing-size advertisement | AirTag/Find My candidate; other accessories, including AirPods for pairing patterns, may match. |
| `tracker` | Samsung, Tile, Find Hub, Chipolo or DULT protocol identifiers | Tracker/protocol evidence; ownership and intent are unknown. |
| `glasses` | Meta/Luxottica/Snap/Vuzix identifiers and service combinations | Candidate product family; recording state is unknown. |
| `raven` | Exact research service IDs or acoustic-sensor vendor evidence | Candidate sensor; field acceptance remains separate. |
| `skimmer` | Serial-module names or services | Generic serial-module candidate, never proof of a card skimmer. ESP32-S3 cannot perform Bluetooth Classic inquiry. |
| `hacker` | Radio-tool services, peer beacon fields or names | Candidate radio tool; ordinary use and spoofed broadcasts are possible. |
| `custom` | A configured SD rule | User-supplied evidence, explicitly identified as an SD rule. |

Confidence has three evidence grades, not percentage probabilities:

- `low`: weak indicator, name, shared vendor or broad accessory pattern.
- `medium`: more specific vendor/service or a research/composite match.
- `high`: a strong observed format or identifier combination. This still does
  not authenticate its sender or establish malicious activity.

Randomized BLE addresses and locally administered addresses never provide OUI
vendor evidence. BLE payload signatures can still match those advertisements.
Their addresses remain observations, not permanent device identities.

## Receive and parser boundaries

The pure API is `include/recon.h`. Wi-Fi accepts an AP BSSID/SSID or a full raw
802.11 frame without its trailing FCS. BLE accepts a bounded advertisement and
optional boundary between ADV and SCAN_RSP. Raw limits are 1,024 Wi-Fi bytes and
512 BLE bytes. The classifier allocates no heap and does no radio, SD, UI or
network work. It is called from application processing, outside radio ISRs.

Ordinary AP scans do not contain client probe/data observations or Remote ID
vendor elements. Those detections require the application's promiscuous
receiver to supply raw frames. The parser supports BLE service-data Remote ID,
Wi-Fi beacon message packs and its supported NAN descriptor format. Parser
support does not prove that a particular aircraft/transmitter was received.
This hardware remains scoped to 2.4 GHz Wi-Fi and BLE. Extended/coded BLE
reception is not enabled by this classifier.

Remote ID results keep ID type, aircraft type and operator-location type.
Binary IDs remain hex rather than being treated as text. Missing fields are
explicit, with independent altitude and speed validity. System coordinates
can describe takeoff, fixed or live locations; they must not all be described
as the pilot's present position. All advertised locations are separate from
the receiver's GNSS fix. Results are stateless per observation. A device
summary that accumulates fields must retain independent sighting times and
mark stale data.

Malformed complete AD streams and invalid frame lengths do not produce
decoded-protocol claims. Unknown traffic still belongs in Wardriver capture.
Recon only selects evidence-bearing observations according to its enabled
categories and configured display/alert thresholds.

## SD configuration

In `/lora-tunnel/config.json`:

```json
{
  "ui": {"buttons_enabled": true},
  "recon": {
    "buzzer": true,
    "display_min": "low",
    "alert_min": "medium",
    "categories": ["flock", "alpr", "axon", "ibeacon", "ring", "camera", "drone", "airtag", "tracker", "glasses", "raven", "skimmer", "hacker", "custom"]
  }
}
```

Merge these fields into the complete configuration; this excerpt does not
replace the required `schema` and `lora` sections. An omitted category list
enables every category, and an empty list disables all. Invalid optional
values retain their defaults. The existing strict radio validation and
primary/backup configuration recovery are unchanged. Physical-button shortcuts
default to enabled; their touchscreen equivalents remain available.

The built-in detector works with the supplied empty `detection_rules.json`.
Optional custom rules are loaded from that file after reboot. There are at
most 64 valid rules and a 16 KiB file/document limit. Accepted types:

| Type | `match` | Transport |
| --- | --- | --- |
| `name` | Exact case-insensitive advertised name | Wi-Fi SSID or BLE name |
| `ssid_prefix` | Case-insensitive prefix, at most 32 UTF-8 bytes | Wi-Fi only |
| `oui` | Three hexadecimal bytes, e.g. `00:04:7D` | Globally assigned Wi-Fi or public/public-identity BLE address only |
| `service16` | Four hex digits, optionally `0x` prefixed | Valid BLE service list/data, including the exact Bluetooth Base UUID representation |
| `manufacturer` | Four hex digits, optionally `0x` prefixed | BLE manufacturer-specific company ID |

Each rule may contain `label`, `category`, `confidence` and Boolean `alert`.
Match/label fields allow at most 47 UTF-8 bytes, reject embedded NUL/control
characters, and never split displayed UTF-8 sequences. Unknown categories,
invalid numeric fields and malformed entries are skipped. BLE identifier rules
validate the complete advertisement framing and do not interpret arbitrary
128-bit UUID substrings as assigned services.

Legacy name/OUI rules without `category` remain custom/low matches. An SD rule
only replaces a result when its configured grade is strictly stronger; equal
grades keep the earlier built-in result. A winning rule's `alert:false`
disables its alert, but a weaker rule is not a mute for stronger built-in
evidence. Custom confidence is a user's setting, not validation by the project.

The source/SD examples include an editable 201-row `vendor-prefixes.csv` with
evidence, category and confidence plus its MIT notice. It is a reference;
firmware uses the compiled table and does not silently import the CSV. Runtime
additions or explicit overrides use `detection_rules.json`.

## Validation evidence

Fourteen direct-GCC C++14 Unity parser tests and nine settings/rule tests passed
on 2026-11-01. Fixtures are synthetic, including valid/truncated Remote ID,
NAN nested lengths, ID/operator-location semantics, randomized addresses,
ambiguous Apple accessories, iBeacon fields, malformed AD segments, custom
UUIDs, configuration bounds and stronger-evidence selection.

These results do not claim physical capture, detection range, buzzer behavior,
GNSS accuracy, uploads or UI acceptance. Firmware compilation and the hardware
checklist remain separate validation steps in `docs/VALIDATION.md` and the
project's practical test checklist.
