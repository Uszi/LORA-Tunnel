#pragma once
#include "app_types.h"
namespace tunnel {
bool mountStorage();
bool storageReady();
uint32_t loggerStackMargin();
// Returns false if the board-profile queue or writer task cannot be allocated.
bool loggerBegin();
bool logObservation(const Observation& observation);
bool loggerIdle();
// Caller owns busMutex. Refuses an active or queued session.
bool unmountStorageForSleep();
}
