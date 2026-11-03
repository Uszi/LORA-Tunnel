#pragma once
#include "board_config.h"
#include <esp_heap_caps.h>

namespace tunnel {
// Large, persistent payload buffers use PSRAM only on boards that have it.
// A missing allocation never silently changes the resource profile.
inline void* allocatePayload(size_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT |
      (board::PSRAM_BYTES ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL));
}
}
