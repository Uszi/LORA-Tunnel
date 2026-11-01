# Offline exports and upload contract research

Checked on 2026-11-01. These tools produce local files and make no network
requests. Host tests establish formatting and rejection behavior; they do not
establish WiGLE or WDGWars ingestion acceptance, a working device capture, or
firmware upload support.

## Run

Python 3.10 or newer is sufficient; no packages are required. In Windows use
`py -3` if `python` resolves to the Windows Store placeholder.

```powershell
py -3 tools/export_logs.py wigle path/to/session.jsonl path/to/session-wigle.csv
py -3 tools/export_logs.py wdgwars-mesh-candidate path/to/session.jsonl path/to/session-mesh-candidate.json
py -3 -m unittest discover -s tests -p test_exports.py -v
```

Output paths must be new files and must differ from the original log. Existing
files are never overwritten. The terminal receives a JSON count report with
each skip reason and `uploaded:false`. Keep that report with the export. A failed
export can leave an incomplete new output; the command returns a nonzero code.
The original JSONL file is retained.

Input is read one line at a time with a 64 KiB limit. Malformed JSON, invalid
schemas, oversized lines and an interrupted final JSON record are counted and
skipped. Complete earlier records still export. Unsupported event types such as
session metadata and LoRa packets are skipped by the WiGLE exporter.

## WiGLE CSV

The format follows the actual official Android exporter at
`c7b87114f9c4d8a269673fedacc675c5b3cf94be`, specifically
[ObservationUploader.java](https://github.com/wiglenet/wigle-wifi-wardriving/blob/c7b87114f9c4d8a269673fedacc675c5b3cf94be/wiglewifiwardriving/src/main/java/net/wigle/wigleandroid/background/ObservationUploader.java).
It uses the `WigleWifi-1.6` metadata line and these 14 columns:

```text
MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type
```

`FirstSeen` is populated from each timestamped observation, rendered in UTC as
`YYYY-MM-DD HH:MM:SS`; this matches the upstream writer's per-location timestamp.
`Frequency` is MHz for Wi-Fi. The exporter validates 2.4 GHz channel/frequency
consistency, keeps RSSI in dBm, and uses the receiver's contemporaneous GNSS
location. Repeated sightings remain repeated rows. This preserves changing
signal and receiver position instead of reducing a drive to one row per device.

The file contains `WIFI` and supported `BLE` rows. A BLE advertised name goes in
SSID, known manufacturer ID in MfgrId, and unavailable channel/frequency stays
empty: an ESP32 BLE scan callback does not identify the advertisement's RF
channel. `AuthMode` is `[BLE]`, not a Wi-Fi encryption claim. Full advertising
bytes and address type stay in the native log because CSV has no corresponding
fields. The official client's [NetworkType](https://github.com/wiglenet/wigle-wifi-wardriving/blob/c7b87114f9c4d8a269673fedacc675c5b3cf94be/wiglewifiwardriving/src/main/java/net/wigle/wigleandroid/model/NetworkType.java)
explicitly includes BLE, but offers no native LoRa/Meshtastic type. LoRa export
to WiGLE remains unsupported by this project.

Default eligibility requires a valid receiver fix no older than 10 seconds,
finite in-range coordinates, a timestamp with an explicit timezone, a valid
address and RSSI, and supported radio metadata. Missing/stale fixes never become
zero coordinates. A legitimate valid fix at latitude/longitude zero remains
valid for WiGLE export. Unknown altitude or accuracy is blank. HDOP is
dimensionless and is never written as AccuracyMeters. Actual GNSS horizontal
accuracy can be provided in optional `gps.accuracy_m`.
[Kismet's exporter documentation](https://kismetwireless.net/docs/readme/logging/wiglecsv/)
also requires an available GPS location for its map-oriented CSV.

BLE public and public-identity addresses export by default. Random and unknown
address types stay local with skip counts. `--include-random-ble` permits known
random address types explicitly and reports how many were included; the CSV
cannot communicate that these are temporary identities. Unknown address type
still fails eligibility. CSV uses standard quoting for commas/quotes, escapes
embedded CR/LF, and preserves the original content in JSONL.

If `wifi.ssid_sanitized` is true, the exporter uses `ssid_hex` only when it
decodes as exact valid UTF-8 within the 32-byte SSID limit. Otherwise SSID is
blank, with a count, instead of exporting display replacement `?` characters as
an observed name. A BLE `name_sanitized` flag likewise produces a blank CSV name;
its original AD bytes stay in the native payload for later decoding. Existing
logs without the optional flags retain their previous text-field behavior.

## WDGWars mesh candidate payload

The public [WDGWars developer page](https://www.wdgwars.pl/press) confirms CSV
upload at `https://wdgwars.pl/api/upload-csv`, with an `X-API-Key` header and
multipart `file` field. That is the Wi-Fi/BLE route. Its JSON upload route is
`https://wdgwars.pl/api/upload`; it uses a Base64 JSON payload, random nonce, and
HMAC-SHA256. Do not put mesh packets into ordinary WiGLE CSV rows.

The separately reviewed [Heimdall implementation](https://github.com/Yggdrasil-AI-labs/meshcore-to-wdgwars/blob/dfcd39b82e8b9e79cf1585da4185a37124705c2a/heimdall.py),
v0.13.0, uses `https://wdgwars.pl/endpoint/upload/` as the corresponding alias.
The payload has `networks`, `aircraft`, and `meshcore_nodes` arrays. Mesh records
use node_id, node_type, name, lat, lon, rssi, first_seen, the envelope marker
`type:MESHCORE`, and explicit `network:meshcore` or `network:meshtastic`.
The marker's historical name does not mean every record is MeshCore.

Its [node identity notes](https://github.com/Yggdrasil-AI-labs/meshcore-to-wdgwars/blob/dfcd39b82e8b9e79cf1585da4185a37124705c2a/docs/meshcore-node-id.md)
describe the two namespaces: MeshCore uses the first 16 hex characters of the
node's public key, while Meshtastic uses its 32-bit device number. Never pad a
short MeshCore path hash into an invented ID or derive a Meshtastic ID from a
MeshCore public key. MeshCore records include the full key when available.

The local exporter requires a decoded observation with this additional shape:

```json
{
  "mesh": {
    "protocol": "meshtastic",
    "decode_status": "decoded",
    "source_id": "abcdef12",
    "node_type": "CLIENT",
    "name": "optional advertised name",
    "hop_count": 2,
    "node_position": {
      "valid": true,
      "source": "node_advertised",
      "lat": 3.5,
      "lon": 4.5
    }
  }
}
```

This snippet is synthetic and must never be uploaded. These fields must come
from real decoded data when used on a capture. The exporter does not manufacture
them from raw packet length or a receiver fix. A sender-advertised position is
a claim by that sender; this exporter does not authenticate that claim or verify
MeshCore Ed25519 signatures.

The candidate exporter requires valid node-advertised coordinates, nonzero node
position, explicit UTC, supported captured role, RSSI, and known hop count.
Receiver GPS is never substituted for node position, including for relayed
traffic. The reviewed server contract treats absent hop information as direct,
so unknown hops are skipped rather than accidentally asserting direct reception.
Future integration should preserve actual hop fields and verify how the service
uses them. Conservative role allowlists can skip future roles visibly.

Meshtastic needs exactly eight hex digits for the source number, excluding
broadcast and zero. MeshCore requires the full 64-hex key and an exactly matching
16-hex source prefix. Missing names fall back to the identifier in the interchange
file; this does not claim an advertised human-readable name existed.

The output is an **unsigned candidate payload**, not a ready HTTP request.
It is kept separate because the public official page does not publish the full
mesh validation contract; the detailed contract comes from this independently
maintained integration. Live acceptance still needs a real eligible capture and
an explicitly configured upload integration. Do not infer success from HTTP 200
alone: examine imported, rejected, and per-reason counts. No endpoint was called
with credentials during development.

## Upload design and Marauder reference

WiGLE's current client defines
`https://api.wigle.net/api/v2/file/upload` in
[UrlConfig.java](https://github.com/wiglenet/wigle-wifi-wardriving/blob/c7b87114f9c4d8a269673fedacc675c5b3cf94be/wiglewifiwardriving/src/main/java/net/wigle/wigleandroid/util/UrlConfig.java)
and [AbstractApiRequest.java](https://github.com/wiglenet/wigle-wifi-wardriving/blob/c7b87114f9c4d8a269673fedacc675c5b3cf94be/wiglewifiwardriving/src/main/java/net/wigle/wigleandroid/background/AbstractApiRequest.java)
uses HTTP Basic authentication over `API-name:API-token`. A future uploader
should stream closed CSV files over verified TLS, keep a per-service result
journal, preserve source logs, and honor user-controlled donation/sharing options.

WDGWars JSON signing uses the API key's UTF-8 text as the HMAC key, not its
hex-decoded bytes. Compact JSON bytes become Base64; an eight-byte random nonce
becomes 16 hex characters; the signed message is nonce text concatenated with
Base64 text. The request JSON contains data, nonce, sig; authentication also uses
the X-API-Key header. This is documented for later implementation only.

The user's recollection is substantially confirmed: ESP32Marauder's `develop`
branch at `79cc6bcba1db6679baee1dab2b0c1ad5010e6363` contains WiGLE and WDGWars
upload routines and imports `/wigle_api_name.txt`, `/wigle_api_token.txt`, and
`/wdg_key.txt` from SD in
[WiFiScan.cpp](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp).
It also contains touch keyboard code. This inspection did not verify the entire
manual credential-entry flow. LORA Tunnel's own keyboard/configuration flow must
be implemented and tested. That upstream import path prints credentials to the
serial log; LORA Tunnel must not copy that behavior.

Keep configurable auto-upload off until enabled by the user. Upload on known
Wi-Fi after capture stops, pause scans deliberately, use bounded batches/retries,
back off on 429, and leave partial failures pending. Credentials belong in the
SD configuration requested by the user, masked in UI and excluded from all
logs, example files, exports, crash output, and source control. No credentials
are needed to run these offline tools.

## Sources, licenses, and fixtures

This exporter is newly written interoperability code. No upstream source code
was copied or vendored. WiGLE Android is BSD-3-Clause; Heimdall is MIT, copyright
2026 HiroAlleyCat; ESP32Marauder is MIT, copyright 2020 Just Call Me Koko. If later
implementation copies/adapts code, preserve the applicable notices and record
the component and revision. Protocol research does not transfer ownership or
establish that the whole project derives from those programs.

Every file beginning with `synthetic-` in `examples/` contains invented test
data. None represents a scan, radio decode, GPS measurement, or accepted upload.
**Do not upload the example files.** They demonstrate a valid Wi-Fi row, public
BLE, suppressed random BLE, missing receiver GPS, and a separate mesh position.
Run the host tests after changing filters, schemas, or field mappings; then
validate new exports against services with real observations before claiming
compatibility beyond the implemented file format.
