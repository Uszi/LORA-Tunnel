# Cardputer 0.4.0-beta.1 software candidate

Updated 2026-11-04 (America/New_York). **Public publication remains pending.**
**HW11 is installed by an independently verified USB application update.**
The upload parser now releases its 14,337 requested buffer bytes before HTTP,
and Cardputer waits for idle-task reclamation after BLE shutdown before
allocating the upload worker. The device passed 100 consecutive offline
maintenance cycles and 30 timed cancellation checks (26 canceled, four already
completed). Every cycle returned to a ready BLE scanner with capture stopped.
The reported lifetime internal-heap minimum was **55,416 bytes**; the sampled
largest internal block remained **31,732 bytes**. There were no observed resets,
allocation failures or error-counter increases. The expected absent-storage
error counter remained one. These checks made no Wi-Fi association, TLS
connection, live upload, capture or storage access. Detailed scope follows
under the HW10/HW11 checkpoint below.

Historical HW7 save/navigation and short-capture results remain scoped to HW7.
The user-confirmed US save and fresh keyboard navigation had a qualified
functional/resource pass: minimum internal heap was **37,780 bytes**, with
**17 ms** dispatch-to-presentation latency and no reported allocation failure.
The short HW7 capture checks retained a **34,368-byte** internal-heap minimum;
both sessions independently passed copied-file readback. These results do not
establish HW11 behavior or complete release acceptance. The user canceled
further SD-card testing; no completed physical full-card fault test is claimed
or scheduled as the next step.

**Historical HW6 evidence remains scoped to HW6.** Its later explicit region
save lowered the lifetime internal-heap minimum to **16,908 bytes**, failing
the **32,768-byte** target despite a successful save and no reported allocation
failure. The HW7 save/navigation result above supersedes that failing workload
for the tested HW7 interaction; it does not erase the historical failure or
prove every possible configuration workload. The user confirmed HW6 custom
tones without repeating during its short Recon check. HW6's 30-minute run
passed continuity and resource checks, but its strict overall result remains
**failed** because the required 30 new UI presentation samples were not
exercised (`ui_samples` stayed zero). That is stationary runtime evidence,
not interactive or field acceptance. HW6 Recon and full Wardriver copied-file
readback passed, including 34 saved LoRa records and explicit accounting for
851 log drops. Known-source LoRa matching, Launcher, battery/power and live
uploads are not established by those results. **HW5's earlier minimum-heap
failure also remains recorded below**: 25,168 bytes after Recon, 7,600 bytes
below the target. Independent readback passed for three earlier HW3/HW4
sessions. The published Heltec 0.3.0-beta.1 artifacts remain unchanged.

The HW6 application was compiled for `cardputer-adv-lora1262` with the pinned
Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 toolchain. Its application image is
**1,727,152 bytes**, leaving **1,615,184 bytes** below the
3,342,336-byte Launcher/standalone application ceiling. The merged factory
image is 1,792,688 bytes. Linker-reported static RAM is 88,176 bytes; this is not
a measurement of runtime free heap or task stack margin. The HW6-source Heltec
regression build passed in 295.63 seconds. The final Cardputer incremental build
passed in 20.26 seconds; its application, factory image and ELF are byte-identical
after restoring the source's original LF line endings. Canonical package/build
paths below remain unchanged. These builds do not replace the published Heltec
0.3.0-beta.1 binary or constitute new physical Heltec acceptance.

| Check | Result |
| --- | --- |
| Native parser/configuration/input/receipt and alert suites | Full PlatformIO run passed 134/134 cases across 16 suites in 18.026 seconds |
| Actual-source alert parser/scheduler/history suite | 14 tests passed independently and as part of the 134-case native run |
| Python exporter, serial, UI, linker, readback and release/package tests | 97 passed in the HW6 full-suite run, including the 25 benchmark and 12 readback tests listed below |
| Focused serial/Cardputer benchmark fixtures | 25 passed, independently rerun with fake serial and clocks; no device port |
| Strict offline SD-session readback fixtures | 12 passed against the production schema; synthetic copied-file fixtures only |
| Installer selection/failure tests in Node | 6 passed |
| Public schemas and shipped example records | 3 schemas, 9 documents/records valid |
| Real Cardputer UI compiled with host drawing adapter | 133 checks passed; 18 synthetic screen previews |
| Production key mapper with pinned M5Cardputer SDK | 214 assertions passed; all 95 printable ASCII characters reachable |
| Actual ingress drain with complete raw-record copies | 127 checks passed |
| Actual compact AP inventory and bounded result delivery | 278 checks passed; all 64 inventory slots retained |
| Actual configuration allocation/persistence handling | 96 Cardputer checks and 71 Heltec checks passed |
| Complete logger evidence within the Cardputer JSON arena | 21 checks passed |
| Real upload worker with in-memory SD and scripted HTTPS | 516 Cardputer checks and 425 Heltec checks passed; no network requests |
| Actual session request/marker, stopped-session and alert integration handling | 76 checks passed |
| Embedded builds | Cardputer HW6 final incremental build and current-source Heltec regression passed; HW6 USB application installation is separately verified below |
| Cardputer image/package validation | Passed image digests, partition table, target/version, application ceiling, blank factory NVS and factory/application equality |
| HW4/HW5/HW6 linker placement | Post-link verification passed; 12,041 linked input-section bytes placed in flash under the pinned official non-ISR policy, producing an 11,008-byte build-time heap-boundary gain; required ISR/cache-disabled code remains in IRAM |

The factory and Launcher files contain the same application. These hashes
identify the **packaged HW6 candidate and its verified installed application**;
the HW6 merged factory has not yet been installed through the web flasher, and
this is not a fully accepted public release:

```text
5d3f668ea224a39c705bc8ead401c6acbfe0eabda620ceee98ee79c0753bec41  LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-FACTORY-0x0.bin
b0c8a2680cef93d70f789488903ab9a6d391f717ea47f449dcabe518ef6e6254  LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin
```

The HW6 package records `hardware_validation: pending` and
`public_enabled: false`. Local package `build-output/cardputer-hw6` and matching
build snapshot `build-output/cardputer-hw6-build` retain the verified binaries,
ELF, map, generated linker script and placement reports. Release source and
relink companions must be generated from the matching final build and source;
the companion validator rejects stale linker output or mismatched ELF/map
verification. Browser layout evidence and earlier staged inventories are
separate from HW6 device acceptance.

Software fixes include scoped configuration scratch allocation and exact-sized
custom rules, checked 512-byte Cardputer file buffers, two regular SD-file
slots, lossless ingress/AP storage reductions, and the checked project-owned
linker policy. The full 32,400-byte RGB332 display buffer, ingress/writer queue
counts, summary counts and raw evidence limits are retained. Large Start/Stop
marker locals were moved into a separate non-inlined helper to reduce the
main-loop call chain's stack demand. The limited HW4 stack measurements below
are not worst-case evidence or sustained-operation acceptance.

HW5's only firmware change from HW4 sets the Cardputer writer task stack to
**10,240 bytes**, down from 12,288 bytes, through `board::WRITER_STACK_BYTES`.
Heltec remains at **12,288 bytes**. HW4's observed writer high-water usage was
7,192 bytes, so the same workload would leave a projected 3,048-byte margin
with HW5's stack. This projection is not a worst-case bound or an observed HW5
margin. It reduces the requested task-stack allocation by 2,048 bytes; actual
heap and stack headroom must be measured again through boot, settings saves,
maximum raw evidence, capture/Stop, and upload/restore transitions before any
acceptance claim. The full display buffer and capture capacities are unchanged.

HW6 adds [21 original short RTTTL motifs](ALERTS.md) shared by both current
board builds. Sound is restricted to active Recon capture and is admitted once
per observed identity per explicit Recon session. A non-evicting 128-identity
history uses exactly 1 KiB and suppresses new sound when full; display-cache
eviction and mute/unmute do not replay a device. Stop, mode change, mute and
sleep cancel the motif. Host tests cover the actual scheduler/history and
main-loop admission paths. The user has now confirmed short custom tones
without repeating on HW6, within the limited Recon test described below; this
does not establish every motif, long-session behavior or Heltec acoustics.

Cardputer now initializes its audio task/I2S resources before capture and BLE,
so first sound does not trigger late initialization. Its 32 summary records
store 64 evidence bytes each, matching the existing UI field and reducing that
summary storage by 2,048 bytes; complete captured/logged evidence is unchanged.
Heltec retains its existing summary capacity. A registered public failed-
allocation callback uses an IRAM-checked function and a sticky atomic flag;
status reports registration and whether any allocation failure was seen, not
private allocation details or a count. The HW6 observations below cover the
short Recon/audio test and stationary Wardriver workload; maximum raw evidence
and upload/restore memory behavior still require their own measurements.
Upload interface/state fixes are incorporated and covered by the updated
worker/UI tests; their device and live-service acceptance is still pending.

Upload fixtures cover cancellation, cancellation during result-checkpoint
writes, delayed automatic scheduling after manual cancellation, retry exhaustion
and manual recovery, authentication failures, queued-job polling, malformed
receipts, corrupted/failed checkpoints and chunk idempotency. Cardputer tests
also cover BLE release/restoration, cleanup before restoration, restoration
failure blocking capture, and cancellation paths. These are offline software
checks, not evidence of live TLS handshakes or service acceptance.

## Limited HW3 hardware evidence

The user confirmed Fn-arrow navigation and selection/saving of the US LoRa
region. They also confirmed Shift, punctuation, Backspace, Enter/save and
Escape/cancel while testing credential entry. Device status then reported the
LoRa frontend ready. A roughly
60-second Wardriver capture completed and returned from saving to idle with
**197 Wi-Fi observations, 1,983 BLE observations, zero LoRa packets, zero
reported drops and zero reported errors**. GPS status showed a fix with six
satellites. The matching Wardriver file was subsequently checked by independent SD
readback, as recorded below. This does not establish LoRa reception. No
coordinates or captured identifiers are included in this record.

HW3 did **not** meet the internal-memory acceptance target. Diagnostics reported
a lifetime minimum internal heap of **6,756 bytes**, below the required
**32 KiB (32,768 bytes)**. During the capture samples, current free heap reached
27,672 bytes and the largest free internal block reached 17,396 bytes. The
reported loop-task stack margin fell to 1,596 bytes by Stop. The observed
keyboard dispatch-to-draw metric was 16 ms; it excludes time before keyboard
polling and the physical keyboard FIFO, so it is not an end-to-end latency
measurement.

## Limited HW4 hardware evidence

These historical results belong to the earlier HW4 image, whose factory
write and separate esptool verification matched these hashes:

```text
ef299b6b71f2244619373013a645183f66d3fdecb11e52b8e322e25a740cb4bf  HW4 factory
33831c9fed05c99bb41f00c19f5ce631b6ecf76d0ce5bca1e9d65ed48871b606  HW4 application
```

They do not validate the later HW5 or HW6 images.

The user confirmed HW4 display/navigation operation and that the US region was
retained. The image booted with the keyboard, cap, shared SPI/SD, LoRa frontend,
writer, scanner and memory readiness checks reporting ready. GNSS reported a
fresh fix with seven satellites at the end of the baseline observation.
Steady idle free internal heap was 41,164 bytes. These checks establish startup
and limited operation of this exact candidate; they do not establish LoRa
packet reception or upload acceptance.

| HW4 observation | Lifetime minimum internal heap | Smallest sampled largest internal block | Loop stack margin | Writer stack margin |
| --- | ---: | ---: | ---: | ---: |
| Boot/idle baseline | 40,396 bytes | 31,732 bytes | 5,328 bytes | 6,824 bytes |
| First approximately 60 seconds of Wardriver | 35,192 bytes | 28,660 bytes | 5,024 bytes | 5,144 bytes |
| Short Recon and saving/return to idle | **32,368 bytes — below threshold** | 25,588 bytes | 4,380 bytes | 5,096 bytes |

The first Wardriver minute reported 253 Wi-Fi observations, 1,651 BLE
observations, zero LoRa packets, one reported drop and zero errors. The short
Recon run reported 881 Wi-Fi observations, 1,388 BLE observations and zero LoRa
packets before it stopped and saved. Lifetime drop/error counters at the end
were 12/0; the 12 drops were already present in the preceding Wardriver saving
sample and are not attributed to Recon. Subsequent independent SD readback
found 447 Wi-Fi and 2,708 BLE records in the complete HW4 Wardriver file, with
12 log drops recorded by its markers. The first-minute diagnostic counts above
are a shorter window than that complete file. Counters are diagnostics, not
proof of lossless RF reception.

HW4's strict heap gate failed by 400 bytes. Heap minimum is the
allocator's lifetime low-water mark since boot, largest-block values are point
samples, and stack values are task lifetime margins. Further changes and any
new candidate need their own image hashes and retesting. The 30-minute run,
physical input latency and worst-case raw-evidence/transition paths remain
untested; no whole-run UI percentile or sustained-operation pass is claimed.

HW4's verified linker relocation moves 12,041 linked input-section bytes to
flash. Comparing the linked memory boundaries yields an exact **11,008-byte
build-time heap-boundary gain**; section-byte totals and usable heap-boundary
changes differ because of linker layout and alignment. This build-time gain
is distinct from the runtime measurements above and does not make the heap
gate pass. Retest boot, configuration/region saves, capture and Stop, maximum
raw Recon evidence, BLE/upload transitions, stack margins, and sustained
operation on the candidate proposed for acceptance.

## Independent HW3/HW4 SD readback

The card was read through the PC microSD reader. Private copies matched the
card files by hash, and the strict offline validator passed all three closed
sessions against the production schema: one start/end pair per file,
contiguous record sequence numbers, consistent session/mode/board/version,
valid retained-data lengths, and GNSS freshness rules.

| Earlier session read back | Total records including markers | Wi-Fi records | BLE records | LoRa records | Log-drop delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| Recon evidence | 35 | 0 | 33 | 0 | 0 |
| HW3 Wardriver | 2,182 | 197 | 1,983 | 0 | 0 |
| HW4 Wardriver | 3,157 | 447 | 2,708 | 0 | 12 |

All **5,368 radio records** had schema-valid fresh receiver fixes and fresh
UTC. The readback reported zero validation errors and zero marker deltas for
ingress drops, storage errors and radio errors. The 12 HW4 log drops remain
explicit; structurally valid files do not establish lossless RF reception.
The observed BLE payloads were at most 31 bytes, so this physical readback does
not exercise maximum raw capacities. Recon stores matched evidence rather
than every observation shown by the scanning counters. No coordinates,
identifiers, credential values or capture filenames are included here.

The user returned the card and USB connection for subsequent testing. Real
home-network/service credentials have intentionally not yet been entered;
previous keyboard confirmations used test entry and do not establish live
upload readiness or successful service receipts.

## Limited HW5 installation and runtime evidence

HW5 was previously installed and stopped, and has now been superseded by HW6.
Its historical hashes below are distinct from the installed HW6 application
hash above:

```text
a5f3f0f342141ccf82fff219f6bf826b99d915f8e4c64539a08d81535de1d2df  HW5 factory
1c0aaa3a4df7ba072ea2fc59f10475dabe6bac25b54b0a51faef70e7dc4f0b53  HW5 application
```

Brave's staged ESP Web Tools installer completed the HW5 factory installation
with Erase device unchecked. Independent readback confirmed the exact
then-installed HW5 application SHA-256 in this section. Bootloader/partition bytes, OTA data and application
bytes matched their factory-image ranges exactly. After first boot, **2,306
bytes differed only within the NVS partition** (`0x9000` to `0xe000`); this is
not described as a whole-factory byte-for-byte match after runtime state was
written. The verified application and executable/partition ranges establish
the installed candidate's identity without exposing private flash contents.

Short serial observations covered approximately 120 seconds of Wardriver,
Stop/save, selecting Recon, approximately 120 seconds of Recon, Stop/save and
returning to idle Wardriver. All six command observations completed without
reported serial/diagnostic failure. These used the short, non-strict command
helper; they are not the strict 30-minute acceptance run.

| HW5 short observation | Lifetime minimum internal heap | Smallest sampled largest internal block | Loop stack margin | Writer stack margin |
| --- | ---: | ---: | ---: | ---: |
| Wardriver and Stop/save | 35,496 bytes | 28,660 bytes | 4,496 bytes | 3,084 bytes |
| Recon, Stop/save and return to idle | **25,168 bytes — below threshold** | 27,636 bytes | 4,380 bytes | 3,080 bytes |

After Wardriver stopped and saved, counters reported **633 Wi-Fi observations,
4,176 BLE observations and four LoRa receptions**, with 126 lifetime drops and
zero errors. These ambient LoRa counters are limited receive evidence; there
was no documented known-source/profile comparison or independent HW5 SD
packet readback, so **known-source LoRa acceptance remains pending**. After
Recon stopped and saved, counters reported 1,803 Wi-Fi observations and 3,293
BLE observations. Lifetime drops remained 126 and errors remained zero, so
there was no additional reported drop during the Recon interval. GNSS status
continued to report a fix; the last samples showed four satellites.

The short HW5 observations therefore **fail the 32 KiB lifetime minimum-heap
gate by 7,600 bytes**. Sampled free heap remained at least 37,312 bytes during
Recon, which does not erase the lower allocator lifetime minimum or rule out
between-poll dips. The observed 3,080-byte writer stack margin is specific to
this limited workload and is not maximum-payload/transition proof. UI frame
sample counts stayed zero, so these runs supply no key-dispatch timing or
physical input-latency evidence. There is no new HW5 user confirmation of
display/navigation in this record.

The upload fixes, new alert behavior and memory changes are incorporated in
HW6. HW5 installation and runtime evidence do not validate HW6; its separate
limited device evidence follows. The software counts above remain host/build
evidence only.

## HW6 verified installation and limited audio evidence

HW6's **1,727,152-byte application** was installed at `0x10000` using esptool
4.8.1.1 over USB. Independent application readback matched the exact
`b0c8a2680cef93d70f789488903ab9a6d391f717ea47f449dcabe518ef6e6254`
SHA-256 above. Boot/partition data and NVS were preserved. This verifies the
installed application, not installation of the HW6 merged factory image through
the browser. The corresponding installation evidence is recorded in
`hw6-usb-app-verification.json`; private backup contents are not published.

A short command sequence selected Recon, captured for **45.031 seconds**,
stopped/saved and returned to idle Wardriver. All four short command
observations completed without reported serial/diagnostic failure. Recon
reported 34 matches and five admitted alert identities, with zero reported
drops, errors or latched allocation failures. The lowest lifetime internal-heap
reading was **33,988 bytes**, the smallest sampled largest block was
**28,660 bytes**, and the observed loop/writer stack margins were
**4,652 / 3,044 bytes**. Stop completed with `running=0`, `saving=0` and
`tone_active=0`; no additional alert identity was admitted in the stop or
idle-Wardriver samples. These are limited workload measurements, not
maximum-payload, all-transition or sustained Recon proof.

The user confirmed: **"Custom tones play without repeating"**. This supports
short multi-note sound and no repeating alerts during that Recon check. It
does not establish every category motif, rotating identities, full-history
behavior, long sessions or physical Heltec audio. The bounded diagnostic and
user records are `hw6-audio-smoke.json` and
`hw6-user-audio-confirmation.json`.

## HW6 stationary 30-minute Wardriver evidence

The exact verified HW6 application completed **1,800.125 seconds** of stationary
USB-powered observation with **1,774 valid status samples**. The sample span was
1,799.297 seconds and the largest sample gap was 1.062 seconds. The strict
checker result is **failed only on `ui_samples`**: zero new UI presentation
frames were recorded, rather than the required 30. No keyboard-dispatch timing,
physical input latency or interactive sustained-capture pass is claimed. Zero
reported UI latency with zero frames is not a latency measurement.

The continuity, capture-state, counter, error, memory, monitored-stack and
allocation-reporting checks passed. No crash, watchdog/reset diagnostic or
latched allocation failure was reported during the observation. The public
allocation hook was registered; its scope excludes allocations before hook
registration and private pool exhaustion.

| HW6 stationary observation | Recorded value | Required resource threshold |
| --- | ---: | ---: |
| Lifetime minimum free internal heap | 33,516 bytes | At least 32,768 bytes |
| Smallest sampled largest internal block | 25,588 bytes | At least 16,384 bytes |
| Loop-task lifetime stack margin | 4,496 bytes | At least 1,024 bytes |
| Writer-task lifetime stack margin | 3,044 bytes | At least 1,024 bytes |
| Smallest sampled free heap | 36,020 bytes | Diagnostic point samples |
| UI presentation frames added | **0 — requirement not exercised** | At least 30 |

The lifetime heap minimum cleared the threshold by **748 bytes**. This is a
limited margin for the observed workload, not proof of worst-case headroom.
Free-heap first/last 60-sample window means were **38,112.3 / 38,316.1 bytes**;
the corresponding largest-block means were **29,103.7 / 29,137.9 bytes**. The final
sampled windows were not lower than the first; this comparison does not show a
growing heap loss, but cannot prove the absence of a leak or lower
between-poll values. Heap minima and stack margins
are lifetime measures since boot/task creation, not values reset for this run.

Wi-Fi, BLE and LoRa counters all increased. LoRa remained ready and its receive
counter increased by **34**. The user subsequently confirmed sending a normal
Meshtastic test message during the run, but no exact transmission time or node
identity has yet been matched to a captured packet. Independent SD readback
now confirms 34 saved LoRa records, as detailed below, but does not identify
which packet came from that transmission. **Known-source LoRa acceptance
remains pending.** GNSS reported a fix in **634 of 1,774 samples**
and ended with zero satellites: the fix was intermittent, not continuous
positioning acceptance. Reported drops increased by **851**; errors began and
ended at zero without an intermediate increase. Drops remain an explicit
limitation, and counters do not establish lossless RF reception. Speaker
readiness remained true; all Wardriver samples showed an inactive tone and no
new admitted alert identities.

After Stop/save, status confirmed `running=0`, `saving=0`, SD ready, zero errors,
an inactive tone and no latched allocation failure. Free heap was **39,392
bytes**, the largest block **30,708 bytes**, and the lifetime heap/stack minima
remained **33,516 / 4,496 / 3,044 bytes**. This confirms return to saved/idle
state in diagnostics; subsequent independent SD readback is recorded below.
The sanitized runtime review is `hw6-wardriver-30min-review.json`. No captured
identifiers, coordinates or credential values are included in this record.

## Independent HW6 SD readback and Launcher-file preparation

The microSD was independently read through the PC reader after Stop/save. A
private backup of **1,069 files / 556,802,856 bytes** was verified before further
preparation. The selected HW6 Recon and Wardriver files both passed the strict
offline validator with zero validation errors: one complete matching start/end
pair, contiguous sequences, consistent session/mode/board/version, valid retained
raw lengths and schema-enforced GNSS freshness rules. The sanitized results are
entries 5 and 7 of `sd-readback-hw5-hw6.json`.

| HW6 session read back | Total records including two markers | Wi-Fi records | BLE records | LoRa records | Fresh receiver-fix radio records | Fresh UTC radio records | Log-drop delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Short Recon/audio test | 36 | 1 | 33 | 0 | 0 | 34 | 0 |
| Full stationary Wardriver session | 69,259 | 6,862 | 62,361 | 34 | 24,899 | 69,257 | 851 |

The full Wardriver session spans **1,805.903 seconds** between its markers,
slightly longer than the 1,800.125-second serial observation window. Its
**69,257 saved radio records plus 851 reported log drops equal the 70,108
observed radio-counter total** for the full session. The difference is fully
accounted for by the recorded log-drop counter; this does not establish lossless
RF reception or eliminate the drops. Both HW6 sessions report zero marker deltas
for ingress drops, storage errors and radio errors.

Of the saved Wardriver radio records, **24,899** contain schema-valid fresh
receiver fixes. All **44,358** records with invalid fixes have null latitude
and longitude, so unavailable positions were not written as valid geotags.
All **69,257** saved radio records have fresh UTC. The short Recon file has no
fresh receiver fixes, while all 34 radio records have fresh UTC; its evidence
must not be described as GPS-positioned. These counts do not establish location
accuracy, continuous positioning or eligibility of every record for upload.

All **34 LoRa records** identify the configured **906.875 MHz** receive
frequency. The largest retained LoRa payload was **104 bytes** and the largest
BLE payload **31 bytes**; the Wi-Fi records were AP scan results without raw
frames. This readback therefore does not exercise maximum raw payloads or the
1,024-byte Wi-Fi frame path. The user confirmed transmitting a normal Meshtastic
message but does not know its node identifier or exact time; matching that
transmission to a saved packet remains pending. Aggregate cross-checks are
recorded in `sd-hw6-crosscheck.json` without publishing captures or identifiers.

The **1,727,152-byte HW6 Launcher application BIN** was copied onto the microSD
and independently read back with the exact application SHA-256 listed above.
The previous SD image was preserved in the verified private backup. This is
verified file preparation only: **installation through Launcher and return to
Launcher have not been tested**. `sd-hw6-launcher-copy.json` records that limited
result. Private backup paths, captures, credentials and coordinates are not
included in this document.

The remaining gate includes interactive sustained capture and field testing,
SD fault/recovery cases, maximum raw-evidence and upload/restore resource checks,
known-source LoRa matching, cold-start configuration and battery/power/wake
behavior, physical input latency, the exact HW6 web/factory installation,
Launcher installation/return, and enabled-service live WiGLE/Wardrift/WDGWars
acceptance with cancellation and capture resumption. The
[device test procedure](CARDPUTER_TEST_PROCEDURE.md) and
[publication gate](CARDPUTER_RELEASE.md) define those checks. Build and host-test
success does not authorize enabling the public installer or submitting the
firmware to M5Burner.

## Later HW6 spare-card and region-save observations

A user-provided disposable spare card initially failed to mount. The app,
keyboard, cap interface and speaker remained initialized; Start correctly
remained stopped with storage unavailable. This was not an absent-card boot
or a full-card test. The original filesystem was not observed. After the user
formatted the spare as FAT32, a fresh boot mounted it with zero reported errors.

The user explicitly selected and saved US. LoRa became ready, SD stayed ready,
and no allocation failure was latched. However, the lifetime minimum internal
heap fell from **37,780 to 16,908 bytes**, a **15,860-byte shortfall** against the
32 KiB target. Sampled free heap recovered to **39,180 bytes**, with a largest
block of **29,684 bytes**. Recovery after saving does not erase the failed reserve
measurement. Per-region heap diagnostics were preserved before any further
reboot; they report the same aggregate minimum. Diagnostic output itself can
allocate a small stdio buffer, so its later free-heap sample is a separate value.

Source inspection found a 16,384-byte configuration document overlapping the
pinned Arduino VFS's initial 4,096-byte stdio buffer during save operations.
The NVS region write occurs after the document scope. This allocation overlap
explains the measured dip closely; it is not a traced allocation-by-allocation
attribution. The planned fix must preserve the complete configuration limits
and imported fields, retain capture buffers, and be physically retested rather
than lowering the resource target.

Loading a valid region from SD does not automatically mirror it into NVS.
Explicit region saves write and verify `lt-region:code`; with SD unavailable,
startup uses that device preference or remains UNSET/off if no valid preference
is available. The earlier spare-card LoRa-off status alone does not prove an RF
fault or establish the exact prior NVS contents. Independent device-preference
persistence after this explicit save is still pending.

## HW7 configuration-memory and checked-storage candidate

HW7 uses a bounded **16,896-byte lease** from the existing **32,400-byte RGB332
framebuffer** while a synchronous stopped configuration operation runs: 16,384
bytes retain the full JSON arena and 512 bytes provide caller-owned file
buffering. Acquisition requires the UI/main task, startup or an idle writer,
and no capture, save drain, upload, shutdown or existing lease. It waits for
real display transfers to finish. Input and presentation pause until every file
is closed and the document is destroyed; borrowed pixels are wiped and a full
frame is rebuilt. The previous physical LCD image remains visible meanwhile.
There is no extra JSON heap allocation or heap fallback, and no capture-buffer,
raw-evidence, parser-capacity or radio-lifecycle reduction.

The Cardputer configuration path now uses checked stdio with the caller-owned
512-byte buffer. The shared logger uses unbuffered stdio with its existing
8 KiB staging buffer and 512-byte Cardputer SPI chunks. Both check writes,
stream errors, flush, sync and close. Exclusive session creation avoids
truncating a colliding name. Host fault injection verifies delayed failures,
sticky first errors, handle cleanup and accounting; it is not a physical
full-card or power-loss result. Heltec retains its existing configuration
memory/File path while receiving the shared checked logger.

Software validation for the frozen source passed: **134 native cases**,
**98 Python tests**, **6 Node installer tests**, **3 schemas / 9 examples**,
**214 keyboard assertions**, **149 UI checks / 18 synthetic previews**,
**370 Cardputer / 91 Heltec configuration checks**, **583 Cardputer / 498 Heltec
checked-storage/logger checks**, **127 capture / 278 AP inventory checks**,
**21 logger-capacity checks**, **516 Cardputer / 425 Heltec uploader checks**,
and **94 actual-session/guard checks**. Upload tests use offline fixtures.

Both the Cardputer and Heltec Expansion V2 targets compiled successfully;
Cardputer also passed the generated linker policy. The Heltec build is a
regression check and does not replace its published binary. Exact build inputs are bound by the 76-file source digest
`33ad4e41a1538562f6881077ec814eb177af65a805eece0a395b262c730eb31b`.
The application is **1,732,032 bytes**, leaving **1,610,304 bytes** below its
3,342,336-byte ceiling. The **1,797,568-byte factory** contains that exact same
application. Candidate SHA-256 values are:

| Artifact | SHA-256 |
| --- | --- |
| HW7 Launcher application | `a8fed2ae0eeb06203bba9562d2da8de2d5b9a8c69e5786532ec0cc2c1e017ed7` |
| HW7 factory at 0x0 | `5bdf2707c42cd87706bf8a3d022eb0ba93e879a03893c2d497e2c93ed2349548` |
| HW7 relink companion | `ebe3512f490f9d99bb9a549d6e6627b1549e5ddf0457617bbf84a5b4cc0cc205` |

Before installation, a fresh private 8 MiB flash backup was taken and the
previous HW6 application verified. The new application was written at 0x10000
and independently verified. A separate 64 KiB readback before reset proved
that bootloader, partition table, OTA metadata and namespace-scoped NVS bytes
were unchanged. This is a direct USB application update, not a HW7 factory,
web-installer or Launcher installation result.

Initial HW7 status reported capture and upload stopped; SD, LoRa, keyboard,
cap and audio ready; zero errors and zero allocation failures. Sampled free
heap was **39,600 bytes**, largest block **30,708 bytes**, and startup lifetime
minimum **38,448 bytes**. The later results below cover the explicit save,
navigation and short capture workloads; startup alone did not establish them.
The historical HW6 16,908-byte save minimum remains a failed result.

### HW7 save and keyboard navigation

The user confirmed that US saves and the display redraws normally, then
confirmed Settings opens and Back returns Home. The first save review was
inconclusive because its telemetry had no accepted keyboard samples. Fresh
navigation without an intervening reset produced increasing UI sample counts
(10, 12, 13, 15, 18), resolving that input-binding ambiguity. The qualified
functional save/navigation review recorded **37,780 bytes** minimum internal
heap, **29,684 bytes** minimum sampled largest block and **39,388 bytes**
minimum sampled free heap, with zero errors and allocation-failure flags.
Both dispatch-to-presentation p95 and maximum were **17 ms**.

The save timestamp and save-to-confirmation latency were not instrumented.
Those UI metrics exclude physical keyboard/FIFO delay, and these cumulative
samples do not establish the strict 30-new-sample sustained-run gate. This
result does not independently prove NVS fallback or every credential save.

### HW7 short Wardriver and Recon captures

Both sessions stopped and finished saving. Their native start/end markers
record **60.524 seconds** for Wardriver and **30.450 seconds** for Recon.
Runtime counters at the final Stop state were:

| Mode | Wi-Fi observations | BLE observations | LoRa receptions | Enabled Recon matches | Additional combined drops | Additional errors |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Wardriver | 219 | 2,097 | 4 | 0 | 1 | 0 |
| Recon | 352 | 860 | 0 | 19 | 0 | 0 |

The lowest lifetime internal-heap value was **34,368 bytes**, 1,600 bytes
above the 32 KiB target; it did not decline during the subsequent Recon run.
Across these short checks, the minimum sampled largest block was **27,636
bytes** and the reported lifetime loop/writer stack low-water margins were **4,356 /
3,452 bytes**. No allocation failure, crash diagnostic or readiness failure
was observed. These counters and samples are scoped resource evidence, not a
worst-case memory bound or a 30-minute HW7 result.

Wardriver status sampled no active tone or admitted alert. Recon recorded four
new alert-history admissions; its post-Stop statuses had no active tone.
This is scheduler telemetry, not a count of independently heard performances
or proof that every motif was exercised. No new UI samples were collected
during these captures; their reported 17 ms UI values came from earlier input.
After the final mode change, the device returned to stopped Wardriver.

### Independent HW7 copied-session readback

A verified private file backup preceded offline validation of the two copied
sessions. Both passed the production schema, matching start/end markers,
contiguous sequence numbers, target/version/mode consistency, monotonic record
times and retained raw-length checks. All **2,342 records** were valid: four
markers plus the radio records below.

| Mode | Persisted Wi-Fi | Persisted BLE | Persisted LoRa | Marker ingress-drop delta | Marker log-drop delta | Storage/radio error deltas |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Wardriver | 219 | 2,096 | 4 | 0 | 1 | 0 / 0 |
| Recon | 0 | 19 | 0 | 0 | 0 | 0 / 0 |

The one-record Wardriver BLE difference from its final runtime counter is
consistent with the single log-drop increment; the aggregate evidence cannot
identify the missing observation or its precise cause. Recon counts every
consumed radio observation before classification but saves only enabled
matches. Its 19 retained BLE evidence records therefore match all 19 runtime
matches; the larger observation totals do not imply lost Recon log records.
Neither result establishes lossless RF reception or authenticates a detection.

All **2,338 radio records** have fresh sentence UTC, but **none has a fresh
receiver position**. Invalid latitude, longitude and altitude fields are null,
not fabricated zeroes or retained stale coordinates. These sessions therefore
contain no eligible geolocated rows for the automatic upload services. A local
"No eligible data; nothing uploaded" result would be expected, not a receipt.

The retained BLE payload maximum was **31 bytes** and the LoRa maximum was
**94 bytes**, with no recorded truncation. Wi-Fi records are completed AP scan
results without raw-frame fields; maximum-capacity raw frames were not tested.
The four LoRa records retain the configured **906.875 MHz** frequency and
successful driver-read status, while CRC is explicitly not reported and the
payload is not decoded. Copied records do not establish a known transmitting
source, CRC verification, GNSS accuracy or exact firmware origin from the
shared version string alone. The installed application hash is established
separately by USB readback above.

The sanitized aggregate reviews are retained privately as
`hw7-save-navigation-review.json`, `hw7-writer-smoke-review.json`,
`hw7-recon-smoke-review.json` and `hw7-sd-readback-review.json`; captured values,
credentials, original session filenames and raw payloads are not published.

Further SD-card fault testing was canceled at the user's request. The
preparation activity is not a completed physical full-card firmware test and
is not an upcoming task. Work continues on firmware and release preparation.
The last captures were stopped and saved before the user powered off the
Cardputer and moved the card to the PC reader. HW7 factory/web installation,
Launcher install/return, sustained interactive capture, fresh-fix/known-source
acceptance, power/wake and live upload/restore behavior remain unverified for
these exact binaries. Existing publication checks and pending status are
unchanged; no canceled or unperformed test is marked passed.

## HW8 upload-lifecycle software checkpoint

HW8 compiled successfully for both Cardputer and Heltec and passed **134 native
cases**, the updated actual uploader fixture (**758 Cardputer / 573 Heltec
checks**), **94 actual-session checks**, **42 actual BLE-lifecycle checks**,
and the shared checked-storage/logger fixture. Its local package is
`build-output/cardputer-hw8`; the application is **1,733,264 bytes**. These are
software/build results only. **HW8 was not flashed**; the subsequent installed
HW9 application and its limited boot observation are recorded below.

The Cardputer upload coordinator now reuses the idle writer task to release
BLE before allocating the 16 KiB upload-worker stack. After upload cleanup,
it waits for the worker to suspend and deletes it from the same core before
restoring BLE. Logger maintenance and upload ownership keep capture gated
until that sequence finishes. The host fixture checks actual maintenance and
BLE functions, retained worker-stack lifetime, cancellation and failure paths.
Its deterministic scheduler adapter does not establish real TLS peak heap,
writer-stack demand or hardware transition timing. Heltec retains its existing
BLE lifetime.

Subsequent source review identified unchecked finalization results in uploader
CSV/checkpoint persistence. HW9 addresses that gap as recorded below. HW8 remains
an intermediate software checkpoint, not an installed or accepted release.
No live uploads, device acceptance or public publication are inferred from
these builds. The user's cancellation of further SD-card testing remains in
effect.

## HW9 checked upload persistence: verified USB installation and stopped boot

The frozen HW9 source compiled successfully for **Cardputer in 32.91 seconds**
and **Heltec in 32.58 seconds**. The final actual uploader fixtures passed
**1,009 Cardputer / 824 Heltec checks**, plus **94 actual-session checks** and
**42 actual BLE-lifecycle checks**. The shared checked-storage/logger fixtures
passed **583 Cardputer / 498 Heltec checks**. The **134 native cases** passed
at HW8; their native inputs are unchanged for HW9, so that result is carried
forward rather than described as a new native run. These tests use offline
adapters and make no live service requests.

HW9 retains the idle-writer BLE/worker-stack coordination introduced in HW8.
It additionally uses the real shared checked-file helper for CSV staging and
checkpoint writes on both targets, with bounded caller-owned **512-byte**
buffers. Short writes, full-count writes with a stream error, and failures from
`fflush`, `fsync` or `fclose` are detected. No POST starts before successful
finalization of the staged CSV and its sending checkpoint. Checkpoint readback
also verifies the written generation, state, job and retry metadata. A received
service response followed by a persistence failure reports that failure rather
than claiming durable completed acceptance.

The actual-worker fixtures inject these failures at staging, sending,
accepted-receipt, pending-job and cancellation checkpoints. They preserve the
source session and exercise cleanup and retry/receipt boundaries. Their
results establish software behavior under those injected failures, not
physical full-card, power-loss, TLS peak-memory or live-service acceptance.
Further SD-card testing remains canceled.

All **76 firmware build inputs** match the frozen source digest
`e9f1198d629299d60b22860426ec58986e133c7df21daae644a04d186d14105d`.
The local package is `build-output/cardputer-hw9`. Its application is
**1,731,840 bytes**, leaving **1,610,496 bytes** below the 3,342,336-byte ceiling.
The factory contains the same application. Candidate hashes are:

| Artifact | SHA-256 |
| --- | --- |
| HW9 Launcher application | `180d674e02e4746724ccd7c364ffe1fddf5b7c8ed27173147061a970c81e9847` |
| HW9 factory at 0x0 | `2570e51731eeda465453d1a6e3e8e20cc6f5994b17365765398c8b7338daa6ba` |

The local firmware checkpoint is
`6e28226ec6554106607516e06b262dec9bfcda33`. Before updating the device, a complete
private **8,388,608-byte** flash backup was taken and the installed HW7
application and partition layout were checked. The **1,731,840-byte HW9
application** was written at **0x10000** with esptool and independently verified
against its exact SHA-256 above. A separate **64 KiB readback before first
boot** confirmed that bootloader, partition table, OTA metadata and NVS bytes
were unchanged. This establishes the USB application update; the HW9 factory
image, browser installation and Launcher installation were not exercised.

A subsequent read-only observation collected **10 valid status samples over
10.14 seconds**, with no malformed or oversized status lines. Every sample
reported the expected target, `ready=1`, stopped capture/save/upload, initialized
keyboard and audio, `cap_ok=1` and `lora=1`. The allocation-failure flag stayed
zero. Storage was absent/unmounted (`sd=0`), with the expected missing-storage
error count stable at **one**; this is not a zero-error storage result or a
storage-recovery test. GNSS characters arrived, but no fresh fix or radio
capture was established.

The reported lifetime internal-heap minimum was **56,476 bytes**, the sampled
largest internal block **31,732 bytes**, and the reported loop/writer stack
low-water margins **5,236 / 4,784 bytes**. These values describe stopped boot
without mounted storage. They do not establish loaded capture or TLS memory,
writer-stack demand during uploads, or an improvement over the differently
loaded historical HW7 captures. Input/audio initialization does not establish
physical key operation or audible sound; `ui_samples` remained zero and no
audio audition was performed.

The sanitized installation and boot records are retained privately as
`hw9-usb-app-verification.json` and `hw9-boot-review.json`. No capture, storage
test, credential change, live upload, physical UI action or audio audition was
performed in this HW9 observation. Historical HW7 save, capture and
copied-session results remain attributed to HW7. Full hardware acceptance and
public publication remain pending, with the existing release gates unchanged.
The current-source Heltec build is regression evidence only; its published
0.3.0-beta.1 artifacts remain unchanged. Further SD-card testing remains
canceled.

## HW10/HW11 upload memory optimization and offline hardware checks

HW10 separates CSV parsing and HTTPS into distinct allocation lifetimes. It
releases the 8,193-byte line buffer, 2,048-byte row buffer and 4,096-byte JSON
arena before every POST or job-status GET. The JSON document object is also
released. A row that crosses the 1 MiB chunk boundary is reread from its saved
source offset after the preceding chunk finishes; reopening requires unchanged
source length and successful seek. Original sessions, retained raw-evidence
limits, capture queues and the screen buffer are unchanged. Host tests check
16,000 distinct records across chunk boundaries exactly once and in order,
parser-reallocation failure, source-size change and failed seek. The fake HTTP
adapter rejects any live parser allocation at request entry.

HW10 was installed and passed its first offline cycle. A subsequent 100-cycle
run stopped at its resource subcheck after 51 completed cycles: a sampled
parser-phase largest free block fell to **15,860 bytes**. No allocation failure,
reset or growing idle-heap loss was observed. That failed subcheck remains in
the private HW10 record. It is not a successful 100-cycle run and does not
measure the memory available during HTTPS.

HW11 adds a bounded idle-task cleanup barrier after Cardputer BLE shutdown.
The pinned NimBLE host task deletes itself, so its stack can remain pending
idle-task reclamation after deinitialization returns. The existing writer waits
for two core-0 idle passes before creating the 16 KiB worker. The idle callback
only increments an atomic; it does not block or allocate. Registration failure,
cancellation or a one-second timeout stops admission and restores BLE without
creating the upload worker. The hook is deregistered on every registered path.
Heltec keeps its existing BLE lifetime. Hardware repetition supports the new
sequence; it does not by itself prove the cause of every HW10 transient.

Separate diagnostics now distinguish all upload-phase samples from the
post-parser, pre-HTTP checkpoint. Both are lifetime minima since boot. The
offline self-check exercises actual BLE release/restoration, worker lifetime,
parser allocation/release and cancellation, using no files, credentials,
Wi-Fi association, HTTP requests or preference writes. It is admitted only
while capture and save are stopped and logger maintenance can be reserved.

The final HW11 Cardputer and Heltec builds passed. Actual uploader fixtures
passed **17,127 Cardputer / 16,869 Heltec assertions**, including 16,000 individual
row-order assertions per target; these are not that many separate test cases.
The actual-session and BLE-lifecycle fixtures passed **94 / 42 checks**, checked
storage/logger fixtures passed **583 / 498**, and **99 Python tests** passed.
The package validator, source-input comparison and whitespace checks passed.
All **76 application/build-policy inputs** match digest
`68dbc3065ffbd5afd6b24b07a80bf0bf2ac4027b08db37c3e1058fe6937ed0df`.

The application is **1,734,656 bytes**, **1,607,680 bytes** below its ceiling.
The factory and Launcher packages contain identical application bytes:

| HW11 artifact | SHA-256 |
| --- | --- |
| Launcher application | `d4c086da98beb4a3cdf99457ba0059f71af7abc988344f65b665c10b7b12d58a` |
| Factory at 0x0 | `ae88b656f3d74ead93944c2b98f2b74f72c08fde49f2a78983b1cd04ccff3565` |

Before installation, the entire 8 MiB device flash was backed up privately and
verified against the preceding HW10 app and expected layout. The new app was
written at 0x10000 and verified independently. An exact 64 KiB readback before
boot confirmed unchanged bootloader, partitions, OTA metadata and NVS bytes.
A ten-status stopped boot observation passed, followed by one initial offline
cycle, **100 consecutive offline cycles**, and **30 cancellation-timing checks**.
The latter observed **26 actual cancellations** and four completions before
cancellation; all returned to a ready scanner with stopped capture/upload.

Across the 100-cycle run, sampled idle free heap ranged from **57,184 to 57,232
bytes**. The first/last ten-cycle means were **57,215.2 / 57,214.8 bytes**, with
no growing loss observed over this bounded run. After the cancellation checks:

| Reported measurement | Bytes |
| --- | ---: |
| Lifetime minimum free internal heap | 55,416 |
| Sampled largest internal block | 31,732 |
| Upload-phase sampled minimum free internal heap | 68,412 |
| Offline post-parser sampled minimum free internal heap | 84,512 |
| Offline post-parser smallest sampled largest block | 31,732 |
| Loop / writer stack lifetime low-water margins | 5,168 / 3,232 |
| Offline upload-worker stack lifetime low-water margin | 15,196 |

These are stopped/offline measurements, not capture contention, full TLS stack
or live upload acceptance. No crashes, watchdog reports or allocation failures
were observed, and the absent-storage error count remained one. The passive
GNSS parser reported a fresh fix with five to six satellites; no coordinates
were retained here, and this does not verify saved geotags or GNSS accuracy.
There was no physical UI action, audio audition, new web installation or
Launcher installation in this run. The earlier RTTTL/user-input evidence
remains attributed to its original candidate.

The exact installation, boot and repetition reports are retained privately as
`hw11-usb-app-verification.json`, `hw11-boot-review.json`,
`hw11-upload-offline-100.json` and `hw11-upload-offline-cancel-30.json`.
Cardputer was left with capture, save and uploads stopped. Further SD-card work
remains canceled. Production flasher publication, GitHub/Gitea release assets
and M5Burner submission have not occurred for this candidate. Physical Launcher,
exact factory/web installation, battery/sleep/wake, sustained interactive
capture and live-service receipts remain distinct outstanding validation.
