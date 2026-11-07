#pragma once
#include <cstddef>
#include <cstdint>

// Dual boot with the application in the other OTA slot, e.g. the Cardputer
// firmware that installed LORA Tunnel over Wi-Fi. A pending OTA image is
// confirmed only after display and input have worked for 30 s, so a failed
// start returns to the other firmware on the next reset.
namespace dual_boot {
// "project version" of the other slot; false and empty when it holds no app.
bool describeOther(char* out, size_t size);
// True once, when this call marks a pending image valid.
bool confirmIfStable(uint32_t uptimeMs, bool healthy);
// Selects the other slot and restarts. Returns only on failure.
bool startOther();
}
