# Prepare the microSD card

Use a FAT32 card for the initial hardware checks. Copy the `lora-tunnel` directory to the card root, then copy `config.example.json` to `config.json` on the card. The firmware creates `/sessions` and a new `LT-xxxxxxxx.jsonl` file for each session. Keep the original JSONL files when exporting.

The example starts with `lora.region:"UNSET"`, `enabled:false` and frequency zero. Choose a region during first setup or in Settings before LoRa reception. This build supports the 863-928 MHz RF variant and standard Meshtastic LongFast profiles; 433/470 MHz choices require different hardware. Selection remains receive-only and starts with LNA bypass. See `docs/REGIONS.md` for the complete source-backed table and migration behavior. Reboot after manually editing radio or detection settings on the card.

When neither `config.json` nor `config.bak` exists, firmware can restore a previously selected standard region from device NVS. Without a saved choice it stays UNSET. A valid explicit SD file takes precedence, including an explicit UNSET file. If existing configuration cannot be read/validated, firmware first tries `config.bak`; without a valid backup, LoRa is disabled and NVS does not override the error. Existing schema-1 US configurations retain their settings and credentials. SD storage is still required to start a recording session.

`config.json` accepts at most 16 KiB, with strings bounded to 127 characters for credentials. Settings > a credential opens the keyboard only when needed. Stop capture before saving. A temporary file is written, checked, and promoted; a previous valid `config.bak` is retained. When creating a configuration through the keyboard, the current radio settings are retained. FAT filesystem updates cannot guarantee atomicity against every card/power failure. Full interruption tests remain required.

The WiGLE API name/token, WDGWars token, Wi-Fi SSID and Wi-Fi password can be entered in this file or on the touchscreen. **This milestone stores them but does not connect or upload.** An automatic-upload policy and uploader will be added after offline capture/export acceptance. Credentials are plaintext on removable storage as requested: treat the card as containing those credentials. Firmware never prints credential values or includes them in session logs. Do not add your real config to source control.

Recon has a built-in passive signature engine and an embedded vendor table. It works when `detection_rules.json` has an empty `rules` array. Optional SD rules add your own evidence; they are not required to enable the built-in detections. `detection_rules.json` accepts up to 64 valid rules and 16 KiB. Add entries in `rules`, for example:

```json
{"type":"name","match":"My test beacon","label":"Owned test beacon","alert":true}
```

An OUI entry uses `type:"oui"` with a verified colon-separated 24-bit prefix, plus a `label` and `alert`. Vendor prefixes are used only for globally assigned unicast Wi-Fi addresses and public/public-identity BLE addresses. Randomized or locally administered addresses are never mapped to an OUI vendor. A `name` rule is an exact case-insensitive name match. Additional types are `ssid_prefix` (Wi-Fi only), `service16` (BLE service list or service data), and `manufacturer` (BLE company ID). BLE identifiers use four hex digits, optionally prefixed by `0x`.

Rules indicate **a match to the supplied evidence**, not verified device identity or purpose. Broadcast names and manufacturer fields can be copied or spoofed. `category` selects one of the documented Recon categories; `confidence` is `low`, `medium` or `high`. A legacy rule without `category` is always a custom low-confidence match. Rules only replace a result with a stronger configured grade; equal-grade built-in evidence wins. `alert:false` suppresses the alert only when that rule supplies the winning classification. It is not a global mute for a device already identified by stronger evidence.

`detection_rules.example.json` contains a few optional examples. Keep the loaded `detection_rules.json` empty unless you want custom rules. `vendor-prefixes.csv` is the editable 201-row evidence reference with attribution in `vendor-prefixes.README.md`; the firmware uses its compiled table and does not automatically import this CSV. To add or change a runtime OUI rule, use `detection_rules.json`. Full evidence and format notes are in `docs/RECON.md` and `schemas/detection-rules-v1.schema.json` in the source tree.

In `config.json`, `recon.display_min` defaults to `low`, `recon.alert_min` defaults to `medium`, and `recon.buzzer` defaults to `true`. `recon.categories` optionally lists enabled category keys; omission enables all and an empty array disables all. `ui.buttons_enabled` defaults to `true` for the physical-button shortcuts. Optional fields with invalid types or values retain their defaults. Reboot after editing the card. Recon filtering is independent of ordinary Wardriver capture and logging.

If the card is missing at boot, the screen/GNSS remain available. Insert a card, then Setup > Retry SD. Reboot to load its configuration. Start refuses a missing card; a card failure during capture is visible and increments errors/drops. Stop must finish before removing the card. Power loss can lose the current RAM buffer; complete prior JSONL lines remain independently readable.
