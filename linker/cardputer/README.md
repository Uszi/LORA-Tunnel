# Cardputer non-ISR linker placement

The Cardputer target uses the official ESP-IDF 5.5.5 FreeRTOS and ring-buffer
non-ISR flash-placement rules against the pinned, precompiled SDK. This changes
code placement only. It does not change SDK headers, library ABI, task stacks,
radio buffers, the RGB332 framebuffer, capture/writer capacity, or log evidence.
Heltec does not load this hook.

`esp-idf-v5.5.5/freertos.lf` and `esp_ringbuf.lf` are unchanged upstream files
from Espressif ESP-IDF, used under Apache-2.0; the upstream license is included.
`provenance.json` records exact upstream URLs and hashes, SDK package identity,
SDK linker/config/archive hashes, and the complete parsed archive inventories.

`tools/cardputer_linker.py` checks those inputs before producing a project-owned
`sections.ld`. It evaluates only the official non-ISR options. It replaces the
original FreeRTOS/ring-buffer IRAM selectors with exact retained archive/object/
section selectors and explicitly places only the approved sections in flash.
Compiler clone suffixes are recognized explicitly; no function-prefix wildcard
can accidentally select a `FromISR` variant. All other SDK script content stays
unchanged. The SDK directory is never written.

The post-framework PlatformIO hook replaces the single `-T sections.ld` argument
only in `cardputer-adv-lora1262`. It also makes the generated script and policy
inputs link dependencies. After linking, the map verifier requires selected
sections to be in flash and retained sections to remain in IRAM, including
scheduler-state/tick access, context switch, ISR helpers, and cache-off code.
Link-time garbage collection is expected; only linked sections count as actual
movement. A successful build reports section bytes moved, not a measured heap
increase or a hardware acceptance result.

Each Cardputer build generates these files under its build directory:

- `cardputer_linker/sections.ld`: generated linker script.
- `cardputer_linker/report.json`: SDK, upstream, generator, hook and policy
  identities plus exact moved/retained section inventories.
- `cardputer_linker/verification.json`: verified section addresses and byte
  counts, bound to the report, linker script, ELF, and map SHA-256 values.

No network access is needed to generate or verify these materials. An SDK update
must be reviewed and repinned; hash/section drift stops the build. Re-run
`python -m unittest discover -s tests -p test_cardputer_linker.py -v` after the
pinned SDK/toolchain are installed to include real archive-generation tests.

The original HW3 map predicts 146 linked sections / 11,151 bytes moved. Treat that
as a baseline comparison only. Device acceptance must measure internal free,
minimum and largest-block heap during passive capture, SD writes, NVS/config
writes, BLE upload pause/restore, and shutdown. Retain the 12 KiB writer stack
until separate worst-case stack measurements justify any change.
