# Cardputer 0.4.0-beta.1 software candidate

Updated 2026-11-03 (America/New_York). **Public release remains blocked.**
The newer **HW5 candidate is built and packaged but has not been flashed**.
HW4 remains installed, with verified flash and limited boot/Wardriver/Recon
checks. **HW4 failed the minimum-heap threshold**: 32,368 bytes after Recon,
400 bytes below the required 32,768 bytes. HW5 needs fresh physical testing;
sustained capture, SD readback, live-service uploads, Launcher installation and
overall acceptance remain pending. The published Heltec 0.3.0-beta.1 artifacts
remain unchanged.

The HW5 application was compiled for `cardputer-adv-lora1262` with the pinned
Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 toolchain. Its application image is
**1,721,424 bytes**, leaving **1,620,912 bytes** below the
3,342,336-byte Launcher/standalone application ceiling. The merged factory
image is 1,786,960 bytes. Linker-reported static RAM is 89,392 bytes; this is not
a measurement of runtime free heap or task stack margin. The final Heltec
HW5-source regression build passed in 34.92 seconds, following the earlier
297.50-second regression with the session-marker fix. It does not replace the
published 0.3.0-beta.1 binary or constitute new physical Heltec acceptance.

| Check | Result |
| --- | --- |
| Native parser/configuration/input/receipt suites | 120 cases passed in 15 suites |
| Python exporter, serial, UI, linker, readback and release/package tests | 96 passed in the HW5 full-suite run, including the 25 benchmark and 12 readback tests listed below |
| Focused serial/Cardputer benchmark fixtures | 25 passed, independently rerun with fake serial and clocks; no device port |
| Strict offline SD-session readback fixtures | 12 passed against the production schema; synthetic copied-file fixtures only |
| Installer selection/failure tests in Node | 6 passed |
| Public schemas and shipped example records | 3 schemas, 9 documents/records valid |
| Real Cardputer UI compiled with host drawing adapter | 121 checks passed; 17 synthetic screen previews |
| Production key mapper with pinned M5Cardputer SDK | 214 assertions passed; all 95 printable ASCII characters reachable |
| Actual ingress drain with complete raw-record copies | 127 checks passed |
| Actual compact AP inventory and bounded result delivery | 278 checks passed; all 64 inventory slots retained |
| Actual configuration allocation/persistence handling | 96 Cardputer checks and 71 Heltec checks passed |
| Complete logger evidence within the Cardputer JSON arena | 21 checks passed |
| Real upload worker with in-memory SD and scripted HTTPS | 343 Cardputer checks and 251 Heltec checks passed; no network requests |
| Actual session request/marker and stopped-session handling | 16 checks passed |
| Embedded builds | Cardputer HW5 and the separately recorded Heltec regression build compiled successfully; HW5 is unflashed |
| Cardputer image/package validation | Passed image digests, partition table, target/version, application ceiling, blank factory NVS and factory/application equality |
| HW4/HW5 linker placement | Post-link verification passed; 12,041 linked input-section bytes placed in flash under the pinned official non-ISR policy, producing an 11,008-byte build-time heap-boundary gain; required ISR/cache-disabled code remains in IRAM |

The factory and Launcher files contain the same application. These hashes
identify the **local, unflashed HW5 candidate**, not a physically accepted public release:

```text
a5f3f0f342141ccf82fff219f6bf826b99d915f8e4c64539a08d81535de1d2df  LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-FACTORY-0x0.bin
1c0aaa3a4df7ba072ea2fc59f10475dabe6bac25b54b0a51faef70e7dc4f0b53  LORA-Tunnel-0.4.0-beta.1-Cardputer-ADV-LoRa1262-LAUNCHER-APP.bin
```

The HW5 package records `hardware_validation: pending` and
`public_enabled: false`. Local package `build-output/cardputer-hw5` and matching
build snapshot `build-output/cardputer-hw5-build` retain the verified binaries,
ELF, map, generated linker script and placement reports. Release source and
relink companions must be generated from the matching final build and source;
the companion validator rejects stale linker output or mismatched ELF/map
verification. Browser layout evidence and earlier staged inventories are
separate from HW5 device acceptance.

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
satellites. These are diagnostic counters; they do not establish SD-file
readback integrity or LoRa reception. No coordinates or captured identifiers
are included in this record.

HW3 did **not** meet the internal-memory acceptance target. Diagnostics reported
a lifetime minimum internal heap of **6,756 bytes**, below the required
**32 KiB (32,768 bytes)**. During the capture samples, current free heap reached
27,672 bytes and the largest free internal block reached 17,396 bytes. The
reported loop-task stack margin fell to 1,596 bytes by Stop. The observed
keyboard dispatch-to-draw metric was 16 ms; it excludes time before keyboard
polling and the physical keyboard FIFO, so it is not an end-to-end latency
measurement.

## Limited HW4 hardware evidence

These historical results belong to the installed HW4 image, whose factory
write and separate esptool verification matched these hashes:

```text
ef299b6b71f2244619373013a645183f66d3fdecb11e52b8e322e25a740cb4bf  HW4 factory
33831c9fed05c99bb41f00c19f5ce631b6ecf76d0ce5bca1e9d65ed48871b606  HW4 application
```

They do not validate the newer, unflashed HW5 image.

The installed image booted with the keyboard, cap, shared SPI/SD, LoRa frontend,
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
sample and are not attributed to Recon. No SD-file readback has yet established
that these observation counts match complete stored records. Counters are
reported as diagnostics, not proof of lossless RF reception.

The strict heap gate remains failed by 400 bytes. Heap minimum is the
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

The remaining gate includes sustained capture, SD readback and recovery,
LoRa reception, cold-start configuration behavior, power/wake, physical input
latency, Launcher installation/return, and enabled-service live WiGLE/Wardrift/
WDGWars acceptance with cancellation and capture resumption. The
[device test procedure](CARDPUTER_TEST_PROCEDURE.md) and
[publication gate](CARDPUTER_RELEASE.md) define those checks. Build and host-test
success does not authorize enabling the public installer or submitting the
firmware to M5Burner.
