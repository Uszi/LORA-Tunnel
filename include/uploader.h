#pragma once
#include <cstddef>
#include <cstdint>
namespace tunnel {
// Initialize scheduling only; network work starts from an idle main-loop tick.
bool uploaderBegin();
// Sampled upload-only heap/block minima, and lifetime worker stack low-water.
// Zero means no worker sample. Self-check:0 unrun,1 running,2 passed,3 failed;
// a result is final only when uploaderBusy() is false. No credentials included.
struct UploadMetrics { uint32_t runs,min_free,smallest_block,stack_margin,self_check,ready_free,ready_block; };
UploadMetrics uploaderMetrics();
// Cardputer USB bench check: idle BLE release/restore and parser allocation only.
// No SD/files, Wi-Fi association, TLS, uploads, credentials or preference writes.
bool uploaderSelfCheck();
// Job-only continuation is explicit; it may GET a persisted job but never POST.
void uploaderTick(bool idle,bool allowAutomatic=true,bool allowJobPolling=false);
// 0 WiGLE, 1 Wardrift, 2 WDGWars, -1 all enabled services. Manual retry clears
// the selected service's authentication/ambiguous-request pause for this boot.
bool uploaderRequest(int service);
bool uploaderBusy();
void uploaderCancel();
void uploaderStatus(char* destination,std::size_t capacity);
void uploaderConfigChanged();
}
