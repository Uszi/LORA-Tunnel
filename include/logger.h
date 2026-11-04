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
// Cardputer upload maintenance reuses the existing writer stack. The request
// must be made while capture and the writer are idle. One pending callback is
// allowed; it runs on writer core 0 without busMutex, outside all file work.
// complete runs after maintenance ownership is released. No heap is allocated.
bool loggerRequestMaintenance(void (*run)(void*),void* context,void (*complete)());
// True only inside that writer-owned callback with no session/queue/file work.
// BLE transitions use this instead of exposing a false idle state to callers.
bool loggerMaintenanceQuiescent();
// Caller owns busMutex. Refuses an active or queued session.
bool unmountStorageForSleep();
}
