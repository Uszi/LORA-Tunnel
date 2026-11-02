#pragma once

#include <cstdint>

namespace tunnel {

enum class ShutdownDecision : std::uint8_t { Wait, Ready, StorageError, Timeout };

// Called only after explicit user confirmation. Compare cumulative counters with
// their values at confirmation: older errors are not new shutdown failures.
// No storage is required when no capture session needs to be finalized.
inline ShutdownDecision shutdownDecision(
    bool captureActive, bool saving, bool loggerIdle, bool hadSession,
    bool storageReady, std::uint32_t errorsBefore, std::uint32_t errorsNow,
    std::uint32_t dropsBefore, std::uint32_t dropsNow, std::uint32_t elapsedMs) {
  if(hadSession && (!storageReady || errorsNow!=errorsBefore || dropsNow!=dropsBefore))
    return ShutdownDecision::StorageError;
  if(!captureActive && !saving && loggerIdle) return ShutdownDecision::Ready;
  return elapsedMs>=15000 ? ShutdownDecision::Timeout : ShutdownDecision::Wait;
}

} // namespace tunnel
