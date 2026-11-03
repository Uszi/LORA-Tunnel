# Cardputer 0.4.0-beta.1 software candidate

Local preparation on 2026-11-02 (America/New_York). **Physical acceptance,
live-service uploads and publication are pending.** No Cardputer was available
for these checks. The published Heltec 0.3.0-beta.1 artifacts remain unchanged.

The final application was compiled for both pinned targets with Arduino-ESP32
3.3.12. The Heltec build is a regression check only. The Cardputer application
image is **2,896,512 bytes**, leaving **445,824 bytes** below the 3,342,336-byte
Launcher/standalone application ceiling. Linker-reported static RAM is
121,048 bytes; this is not a measurement of runtime free heap or stack margin.

| Check | Result |
| --- | --- |
| Native parser/configuration/input/receipt suites | 120 cases passed in 15 suites |
| Python exporter, serial, UI and release/package tests | 50 passed |
| Installer selection/failure tests in Node | 6 passed |
| Public schemas and shipped example records | 3 schemas, 9 documents/records valid |
| Real Cardputer UI compiled with host drawing adapter | 119 checks passed; 17 synthetic screen previews |
| Production key mapper with pinned M5Cardputer SDK | 214 assertions passed; all 95 printable ASCII characters reachable |
| Real upload worker with in-memory SD and scripted HTTPS | 83 checks passed; no network requests |
| Both embedded targets | Compiled successfully |
| Actual Cardputer image/package validation | Passed image digests, partition table, target/version, ceiling, blank factory NVS and factory/application equality |

The factory and Launcher files contain the same application. Candidate hashes:

```text
132d3965bd70e2c5590cf8f9fa7ce9108a950f3216c23fe8380dff4b426dc65b  FACTORY-0x0.bin
63f15594c3fd544758a659768f97bca2b790a531442a278d1c392afc2fe6ab8b  LAUNCHER-APP.bin
```

Package and target-catalog metadata record the full filenames, source commit,
archives and hashes. The dependency companion contains the pinned library
sources/notices and hashes tied to the build. Browser layout checks and the
generated artifact inventory are recorded separately with the local candidate.

Review fixes include paged menu indicators, typed/read-back upload preference
saves, rejection of malformed imported credentials without silent truncation,
deferred radio start/standby during SD bus contention, and bounded Wi-Fi result
delivery for the reduced ingress queue. Upload host fixtures cover interruption,
retry exhaustion/manual recovery, persisted authentication failures, queued job
polling, malformed receipts, corrupted/failed checkpoints and chunk idempotency.

These results do not prove RF reception, physical keyboard timing, battery/GNSS
behavior, SD durability, live TLS/service compatibility or Launcher installation.
The [device test procedure](CARDPUTER_TEST_PROCEDURE.md) and
[publication gate](CARDPUTER_RELEASE.md) define the remaining work.
