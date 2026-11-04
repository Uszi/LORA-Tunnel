# Home Wi-Fi uploads

LORA Tunnel can send eligible records from completed native SD sessions to
WiGLE, Wardrift.net, and WDGWars. Configure **Settings â†’ Connection keys**,
then enable each destination under **Settings â†’ Automatic uploads**. Automatic
uploads are off by default, including when an older configuration already has
API credentials. A Wi-Fi password may be blank for an open network, 8â€“63
characters, or a 64-digit hexadecimal key. SSIDs are limited to 32 bytes.

WiGLE uses its API name and API token; Wardrift and WDGWars each use their own
API key/token. These are service API credentials, not necessarily account login
passwords. Credentials are stored in the existing SD configuration. The device
does not print credentials, network identifiers, coordinates, request bodies,
or server response bodies in its upload status.

Imported credential values must pass the same length and character checks as
keyboard input. Invalid or oversized values are treated as unavailable at
runtime; they are never silently truncated into a different credential. Loading
an invalid value leaves the original SD file unchanged, so it can be corrected
through the keyboard or an SD-card editor.

## When uploads run

Automatic work starts only while capture is stopped and the logger has closed
its session, at startup or after a completed capture. Editing connection keys
suppresses automatic work. The firmware never pauses a capture to connect to
Wi-Fi. **Upload now** starts a selected service manually while stopped, even
when that service's automatic switch is off. The Cardputer Uploads menu uses
three pages and shows the worker status and setting-save/refusal notice on
separate lines. New automatic uploads wait while that status page is open.

A WDGWars job already submitted during this boot can continue polling on the
Uploads page while capture is stopped. This continuation performs only GET
requests for valid persisted jobs; it does not stage another CSV or submit a
new session. Connection-key editing, Power, capture and cancellation suppress
that continuation. Pending jobs normally wait 30 seconds between polls;
explicit **Upload now: WDGWars** can request a check sooner.

If the device restarts with **WDGWars automatic: OFF**, use **Upload now:
WDGWars** to rediscover a saved pending job. Opening its status page alone does
not start a network request after reboot. The manual action polls the saved
job instead of resubmitting its CSV. With automatic uploads enabled, ordinary
idle scheduling also rediscovers saved jobs.

The worker associates only with the configured home SSID. It uploads through
HTTPS with the pinned ESP-IDF full certificate bundle and hostname validation;
redirects are disabled. If system time is unavailable, it first obtains time
through NTP. If time, association, certificate validation, or storage is
unavailable, the original capture remains on SD. No insecure TLS fallback is
provided. Network operations run in a separate task so the keyboard and GNSS
loop can continue. A socket operation has a four-second timeout and an HTTP
transaction has a 60-second overall streaming/read bound, subject to SDK call
timeouts; physical cancellation responsiveness still needs measurement.

**Cancel upload** requests cancellation. Starting capture or confirming sleep
also requests cancellation and waits for the worker to release network/storage
ownership. A request already received by a server cannot be recalled. Device
status distinguishes an uncertain receipt from an accepted upload. Cancel also
clears pending upload retries and job polls for the current idle period, including
retries scheduled during a result-checkpoint write. Leaving the Uploads page after cancellation
does not restart the worker. Enabled services become eligible again after a
later capture is stopped, a configuration change, a manual request, or reboot.

On Cardputer, the upload worker releases the BLE host/controller before its
large parsing/TLS allocations and restores the passive scanner after closing
all HTTP/files and freeing upload buffers. Start cancels upload and waits for
restoration; a failed restoration blocks capture before a session is opened.
Heltec keeps its existing BLE lifetime. Each Cardputer BLE transition has an
independent 15-second recovery timer, armed only at stopped/saved boundaries.
A timeout emits an explicit nonsecret `LT BLE transition_timeout=... failed=1`
diagnostic and restarts into idle operation with checkpoints/config retained.
Exercising that timer is failed hardware acceptance, never a successful upload
or restoration. The 16 KiB worker stack must still fit before BLE can be released;
worker creation failure remains visible and sends no data. Cardputer uploader
files use checked 512-byte stdio buffers. Opening a file fails cleanly if that
buffer cannot be configured; offline fixtures enforce the two-regular-file
ceiling across success, retry, cancellation and checkpoint failure paths.
Directory handles are counted separately by the filesystem.

## What is eligible

The uploader requires a schema-1 `session_start` and matching `session_end` as
the first and last records of a closed native JSONL file. It keeps native logs
unchanged and stages derived CSV on SD. Source SHA-256 and byte length identify
the exact capture covered by each checkpoint.

Only Wardriver AP scan results and public/public-identity BLE observations with
a valid receiver fix no older than 10 seconds, valid coordinates, and an actual
native UTC timestamp are exported. Recon records, raw Wi-Fi frames, LoRa data,
random BLE addresses, records or sessions marked synthetic, malformed metadata, and invalid/stale/missing fixes are
excluded. HDOP is never substituted for accuracy in metres. Sanitized SSIDs are
recovered only from valid original UTF-8 bytes; an unrecoverable name stays
blank. CSV quotes and line-control escaping preserve field boundaries.

The format follows the same eligibility rules as the offline WiGLE exporter
for firmware-produced logs. The embedded reader bounds each line to the
firmware writer's 8192-byte maximum and requires its `YYYY-MM-DDTHH:MM:SSZ`
timestamp form; the desktop importer additionally supports larger imported
records and other timezone-qualified timestamps. No claim is made that arbitrary
third-party JSONL is accepted on-device.

Each staged CSV is at most 1 MiB, below Wardrift's documented 10 MiB request
limit. A larger session is split deterministically into several chunks. A
session with no eligible rows is marked skipped locally without an HTTP upload.
Its whole-file summary is also `skipped`, and the UI reports **No eligible data;
nothing uploaded**. Previously processed data reports **No new data; already
processed**; an obsolete polling trigger with no pending jobs reports **No
pending upload jobs**. These are local outcomes, not service receipts. Older
checkpoints that marked an empty whole file locally complete remain compatible,
but do not produce a new accepted-upload message.

## Receipts, retries, and recovery

Progress is independent for each source, chunk, and destination under
`/lora-tunnel/uploads/`. Dual alternating checkpoint slots are flushed, closed,
and read back before a request is sent or considered complete. Successful
chunks remain complete across restarts. Whole-file completion avoids repeatedly
rewriting derived CSV, while the original source hash is still checked.

- **WiGLE:** multipart POST to `/api/v2/file/upload`, HTTP Basic API credentials;
  HTTP 200 and JSON `success: true` establish acceptance.
- **Wardrift:** CSV POST to `/v1/ingest/signals`, `X-API-Key` and `X-File-Name`;
  HTTP 200 and JSON `accepted: true` or `duplicate: true` establish acceptance.
  An HTTP 409, HTML page, or a count without an acceptance flag does not.
- **WDGWars:** multipart POST to `/api/v2/upload-csv`, `X-API-Key`; HTTP 202 and
  a valid string or positive-integer `job_id` establish a pending job. Its ID is
  saved before checking `/api/v2/upload-job/{id}`. Only `status: done` at HTTP
  200 completes the checkpoint. Pending jobs are polled without reposting CSV.
  A terminal failed job remains failed until an explicit manual retry.

Temporary failures use 30-second, 120-second, and 600-second backoff intervals,
with no more than three failed attempts per source/service in one boot. After
the third failure, a manual retry or restart is required. Connection attempts
are also limited to three until configuration changes or a manual retry.
Authentication failures pause that service across restarts until a configuration
change or manual retry. Neither rejected requests nor ambiguous POST receipts
are silently retried. If power or connection is lost after a body may have been
sent, an explicit manual retry can potentially duplicate a server-side receipt;
the saved source/chunk identity remains available for inspection.

The `staging.csv` file is disposable derived data. Keep original `/sessions/`
logs and checkpoints together when backing up the card. Deleting checkpoints
removes the firmware's record of prior service acceptance and can cause another
upload. A successful response followed by an SD failure is reported as a
checkpoint failure, not silently treated as durable completion.

## Evidence and remaining acceptance

Native tests exercise CSV eligibility, escaping, time validity, receipt parsing,
job IDs, and retry policy. `tools/check_uploader.py` compiles the real worker
against an in-memory SD adapter and scripted HTTP transport. It exercises
streamed multipart lengths, fragmented/oversized/error responses, restart
checkpoints, asynchronous jobs, authentication pauses, cancellation, SD failure,
and multi-chunk sessions. Its hash adapter is a deterministic test stand-in;
embedded builds use the SDK mbedTLS SHA-256 implementation. These checks make no
live network requests and do not establish real certificate/service acceptance.

Before public release, test configured-home association, real WiGLE/Wardrift/
WDGWars receipts, capture-to-upload transitions, disconnect/reconnect, SD
failure/restart recovery, UI responsiveness, and peak TLS memory on the actual
Cardputer. Verify remote records privately against the eligible SD rows. Until
then, live upload acceptance remains pending alongside device acceptance.

The upload implementation is original. Protocol shape was checked against the
[WiGLE API](https://api.wigle.net/swagger) and primary project integrations,
including the [Wardrift integration proposal](https://github.com/justcallmekoko/ESP32Marauder/pull/1634).
No external uploader implementation is incorporated.
