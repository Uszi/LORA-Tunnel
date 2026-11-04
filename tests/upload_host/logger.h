#pragma once
namespace tunnel {inline bool storageReady(){return true;}
bool loggerRequestMaintenance(void (*run)(void*),void* context,void (*complete)());
bool loggerMaintenanceQuiescent();
bool loggerIdle();}
