#pragma once

// M5GFX carries the same lgfx types, but must never be linked alongside the
// standalone LovyanGFX implementation in one embedded build.
#if defined(LORA_TUNNEL_CARDPUTER_ADV) && !defined(LORA_TUNNEL_HOST_UI)
#include <M5GFX.h>
#else
#include <LovyanGFX.hpp>
#endif
