#pragma once

// Cardputer-only forced include: apply after SDK defaults, before any NimBLE
// header, in every source and library translation unit. No SDK files are edited.
#if defined(LORA_TUNNEL_CARDPUTER_ADV) && defined(ESP_PLATFORM)
#include "sdkconfig.h"

// WiFiGeneric passes this through WIFI_INIT_CONFIG_DEFAULT to csi_enable.
// Passive AP/probe capture does not use channel-state (CSI) samples. The SDK
// defaults enable their reservation; keep the public init ABI and disable only
// that unused reservation. Runtime savings require a matching device check.
#undef CONFIG_ESP_WIFI_CSI_ENABLED
#define CONFIG_ESP_WIFI_CSI_ENABLED 0

// Keep SDK role definitions: this pinned host has unguarded GAP/GATT references
// that pull a second precompiled NimBLE host from libbt.a if connecting roles
// are removed. The application still uses passive scanning only.
// One connection capacity reduces reservations without changing linked roles.
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
#undef CONFIG_NIMBLE_MAX_CONNECTIONS
#define CONFIG_NIMBLE_MAX_CONNECTIONS 1

// Passive advertising reports use HCI event buffers, not connection ACL data.
// Keep every block/payload size, event count and task stack at its SDK default;
// reduce unused connection-oriented mbuf reservations conservatively.
#undef CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT
#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT 4
#undef CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT
#define CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT 4
#undef CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT 4

// Source-calculated MSYS savings: (12-4)*256 + (24-4)*320 = 8,448 bytes.
// ACL saves another 20 aligned 255-byte payload blocks plus their headers.
// Runtime heap/controller savings and reception still require device checks.
#endif
