#pragma once
#include <cstddef>
namespace tunnel {
// Initialize scheduling only; network work starts from an idle main-loop tick.
bool uploaderBegin();
void uploaderTick(bool idle,bool allowAutomatic=true);
// 0 WiGLE, 1 Wardrift, 2 WDGWars, -1 all enabled services. Manual retry clears
// the selected service's authentication/ambiguous-request pause for this boot.
bool uploaderRequest(int service);
bool uploaderBusy();
void uploaderCancel();
void uploaderStatus(char* destination,std::size_t capacity);
void uploaderConfigChanged();
}
