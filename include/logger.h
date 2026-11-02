#pragma once
#include "app_types.h"
namespace tunnel {
bool mountStorage();
bool storageReady();
// Returns false if the 32-record PSRAM queue or internal writer task cannot be
// allocated; no fallback consumes the enlarged queue from internal RAM.
bool loggerBegin();
bool logObservation(const Observation& observation);
bool loggerIdle();
// Caller owns busMutex. Refuses an active or queued session.
bool unmountStorageForSleep();
}
