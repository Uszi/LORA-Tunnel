# LoRa region selection

New installations start with `region: "UNSET"`, frequency zero and LoRa reception disabled. Choose a region during first setup or later from Settings while capture is stopped and saved. Selecting a region chooses its standard Meshtastic **LongFast** receive profile. It does not transmit, join a mesh, acknowledge packets or change the Wi-Fi country/channel policy.

**Skip LoRa** is a persistent choice: it saves UNSET with `selection_complete:true`, completes first setup and leaves LoRa disabled. The blank example has `selection_complete:false`, so it still opens setup. A selected supported region, including a valid legacy US configuration, also completes setup. Internally `regionSelectionMade()` tracks this choice separately from `regionConfigured()`, which stays false for UNSET. A corrupt existing SD configuration clears the completed-choice state and prompts recovery instead of borrowing NVS state.

This build supports the **863-928 MHz Heltec Expansion Kit V2 variant**, which includes units sold for 868 or 915 MHz operation. Heltec also lists separate 433 MHz and 470-510 MHz products. Those variants need their own verified RF/antenna configuration; selecting a software profile cannot change the fitted RF hardware. The seven lower-band profiles below are reference entries and cannot be enabled on this build. SX1262 cannot receive the 2.4 GHz LoRa region. Use an antenna appropriate for the selected frequency. [Official kit variants](https://heltec.org/project/v4-r8-ex/)

## Persistence and migration

The device stores only the selected region code in its dedicated `lt-region` NVS namespace. Credentials remain in the SD configuration. With a mounted card, saving first writes and validates `config.tmp`, rotates only a valid primary into `config.bak`, promotes the candidate and then mirrors the region code to NVS. An SD save failure leaves the active region unchanged; it does not leave a newer NVS selection hidden behind the old SD file. Without a card, an NVS write and readback must succeed before the choice becomes active. SD remains required for recording sessions.

Any valid saved NVS code, including UNSET for Skip, completes setup when used without an SD configuration. The SD `selection_complete` marker also preserves Skip if the SD save succeeded but the NVS mirror failed; the UI can report that partial persistence result. An explicit blank SD example still takes precedence over an older NVS choice.

A valid explicit SD configuration takes precedence over NVS. A valid backup can recover a damaged primary. If both existing SD files are unusable, LoRa stays disabled and NVS does not override the error. Only when the files are absent, or the SD card is unavailable, can NVS restore the last selected **standard** profile. Custom SD frequencies and modem settings are not copied to NVS. An explicit SD `UNSET` configuration also takes precedence: do not copy the blank example over a configured card unless you intend to reset its choice.

Older schema-1 configurations remain compatible. An existing configuration without `region` that enabled LoRa or supplied a valid 902-928 MHz frequency retains its old US settings, including custom frequency, modem values and disabled state. A bare disabled configuration stays UNSET. Saving another credential does not retune the radio or discard the other credentials. Choosing a new region deliberately replaces the radio settings with that region's standard LongFast values and returns the LNA to bypass; unrelated configuration and unknown future fields survive.

Example explicit selection:

```json
{"schema":1,"lora":{"enabled":true,"region":"EU_868"}}
```

Omitted modem values use the region's standard profile. To retain a selected region while turning reception off, use `enabled:false` with that region. For a fresh/unconfigured state use `{"schema":1,"lora":{"enabled":false,"region":"UNSET"}}`. Keep credentials and other settings from your existing file when editing it. The parser checks that a modern profile's complete occupied bandwidth fits its selected region.

## Verified default LongFast profiles

All entries use **250 kHz bandwidth, SF11, coding rate 4/5, 16-symbol preamble, sync word 0x2B and CRC reception**. The table is derived from Meshtastic revision `6d41e279f1f51bd59f687b9d441c1bf47b1594fc`, inspected 2026-11-01. Its region intervals and default frequency-slot calculation are in [RadioInterface.cpp](https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/src/mesh/RadioInterface.cpp), modem values in [MeshRadio.h](https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/src/mesh/MeshRadio.h), and preamble in [RadioInterface.h](https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/src/mesh/RadioInterface.h).

| Region code | Center MHz | Frequency slot (1-based) | This high-band build |
| --- | ---: | ---: | --- |
| US | 906.875 | 20 | Available |
| EU_868 | 869.525 | 1 | Available |
| ANZ | 919.875 | 20 | Available |
| JP | 923.375 | 12 | Available |
| KR | 922.875 | 12 | Available |
| TW | 923.875 | 16 | Available |
| RU | 869.075 | 2 | Available |
| IN | 865.875 | 4 | Available |
| NZ_865 | 864.875 | 4 | Available |
| TH | 923.875 | 16 | Available |
| UA_868 | 868.375 | 2 | Available |
| MY_919 | 922.875 | 16 | Available |
| SG_923 | 917.875 | 4 | Available |
| PH_868 | 868.125 | 1 | Available |
| PH_915 | 917.875 | 12 | Available |
| KZ_863 | 866.875 | 16 | Available |
| NP_865 | 867.875 | 12 | Available |
| BR_902 | 903.875 | 8 | Available |
| EU_433 | 433.875 | 4 | Requires 433 MHz hardware |
| ANZ_433 | 434.425 | 6 | Requires 433 MHz hardware |
| UA_433 | 434.375 | 6 | Requires 433 MHz hardware |
| MY_433 | 433.875 | 4 | Requires 433 MHz hardware |
| PH_433 | 434.375 | 6 | Requires 433 MHz hardware |
| KZ_433 | 434.450 | 6 | Requires 433 MHz hardware |
| CN | 478.875 | 36 | Requires 470-510 MHz hardware |

These are Meshtastic region codes and default-network settings, not a promise that every network in a country uses them. Meshtastic's [country mapping](https://meshtastic.org/docs/configuration/region-by-country/) provides a starting point. A custom channel name, manual slot or frequency override can move a network away from the table. The SX1262 still listens to only one frequency and modem configuration at a time; this selector does not implement hopping or discover every nearby LoRa device.

The table and validation tests are software evidence. RF reception, antenna performance, persistence across interrupted SD/NVS writes and the complete touchscreen setup flow require physical acceptance for each supported configuration.
