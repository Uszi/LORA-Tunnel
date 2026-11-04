# Cardputer 0.4.0-beta.1 software candidate

Updated 2026-11-03 (America/New_York). **Public release remains blocked.**
The newer **HW6 candidate is built and packaged but has not been flashed**.
HW5 remains installed and stopped after its staged Brave installation and
short Wardriver/Recon checks. **HW5 failed the minimum-heap threshold**:
25,168 bytes after Recon, 7,600 bytes below the required 32,768 bytes. Independent
SD readback passed for three earlier HW3/HW4 sessions. HW6's sound, upload and
memory changes require fresh device tests; sustained capture, known-source
LoRa, live uploads, Launcher and overall acceptance remain pending. The
published Heltec 0.3.0-beta.1 artifacts remain unchanged.

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
| Embedded builds | Cardputer HW6 final incremental build and current-source Heltec regression passed; HW6 remains unflashed |
| Cardputer image/package validation | Passed image digests, partition table, target/version, application ceiling, blank factory NVS and factory/application equality |
| HW4/HW5/HW6 linker placement | Post-link verification passed; 12,041 linked input-section bytes placed in flash under the pinned official non-ISR policy, producing an 11,008-byte build-time heap-boundary gain; required ISR/cache-disabled code remains in IRAM |

The factory and Launcher files contain the same application. These hashes
identify the **built, unflashed HW6 candidate**, not a fully accepted public release:

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
main-loop admission paths; no HW6 sound has been physically accepted yet.

Cardputer now initializes its audio task/I2S resources before capture and BLE,
so first sound does not trigger late initialization. Its 32 summary records
store 64 evidence bytes each, matching the existing UI field and reducing that
summary storage by 2,048 bytes; complete captured/logged evidence is unchanged.
Heltec retains its existing summary capacity. A registered public failed-
allocation callback uses an IRAM-checked function and a sticky atomic flag;
status reports registration and whether any allocation failure was seen, not
private allocation details or a count. These changes need new heap/stack and
allocation-failure observations on HW6 rather than extrapolated acceptance.
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

They do not validate the newer HW5 image.

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

HW5 remains installed and stopped at this checkpoint. Its historical hashes
below are distinct from the unflashed HW6 hashes above:

```text
a5f3f0f342141ccf82fff219f6bf826b99d915f8e4c64539a08d81535de1d2df  HW5 factory
1c0aaa3a4df7ba072ea2fc59f10475dabe6bac25b54b0a51faef70e7dc4f0b53  HW5 application
```

Brave's staged ESP Web Tools installer completed the HW5 factory installation
with Erase device unchecked. Independent readback confirmed the exact current
HW5 application SHA-256 in this section. Bootloader/partition bytes, OTA data and application
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
the built HW6 candidate. Repeat affected hardware checks against its exact
hashes before the sustained test; HW5 installation and runtime evidence do not
validate HW6. The current software counts above are host/build evidence only.

The remaining gate includes resolving the runtime heap shortfall, sustained
capture, final-candidate SD readback and storage recovery, known-source LoRa
reception, cold-start configuration behavior, power/wake, physical input
latency, Launcher installation/return, and enabled-service live WiGLE/Wardrift/
WDGWars acceptance with cancellation and capture resumption. The
[device test procedure](CARDPUTER_TEST_PROCEDURE.md) and
[publication gate](CARDPUTER_RELEASE.md) define those checks. Build and host-test
success does not authorize enabling the public installer or submitting the
firmware to M5Burner.
