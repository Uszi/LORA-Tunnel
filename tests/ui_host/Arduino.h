#pragma once
#include <cstdint>
#include <cstddef>
extern uint32_t hostMillis;
inline uint32_t millis() { return hostMillis; }

using TaskHandle_t=void*;
static uintptr_t hostTaskIdentity=1;
inline TaskHandle_t xTaskGetCurrentTaskHandle() {return reinterpret_cast<TaskHandle_t>(hostTaskIdentity);}
