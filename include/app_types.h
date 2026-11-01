#pragma once
#include <Arduino.h>
#include <atomic>
#include "recon.h"

namespace tunnel {
constexpr size_t RAW_MAX = 1024;
constexpr uint32_t GPS_STALE_MS = 10000;
enum class Kind : uint8_t { Wifi, Ble, Lora, Start, Stop };
enum class CaptureMode : uint8_t { Wardriver, Recon };
struct Fix {
  bool valid = false;
  uint32_t age_ms = UINT32_MAX;
  uint32_t utc_age_ms = UINT32_MAX;
  double lat = 0, lon = 0;
  float altitude = NAN, hdop = NAN;
  uint16_t satellites = 0;
  char utc[24]{};
};
// Fixed-size queue payload. No pointers, heap strings, or allocation in callbacks.
struct Observation {
  Kind kind = Kind::Wifi;
  CaptureMode mode = CaptureMode::Wardriver;
  uint32_t session = 0;
  uint64_t mono_ms = 0;
  Fix gps;
  char address[24]{}, name[40]{}, security[48]{};
  int16_t rssi = 0;
  float snr = 0;
  uint16_t channel = 0, length = 0;
  uint8_t address_type = 255;
  bool truncated = false;
  bool name_sanitized = false;
  bool wifi_frame = false;
  uint16_t original_length = 0;
  uint16_t ble_adv_length = 0;
  uint8_t ssid_length = 0, ssid_raw[32]{};
  recon::Result detection;
  uint8_t raw[RAW_MAX]{};
};
struct Counters {
  std::atomic<uint32_t> ingress_drops{0}, log_drops{0}, storage_errors{0};
  std::atomic<uint32_t> malformed{0}, radio_errors{0}, ui_skips{0};
  std::atomic<uint32_t> written{0}, flush_max_ms{0}, scan_cycles{0};
};
extern Counters counters;
extern QueueHandle_t captureQueue;
extern SemaphoreHandle_t busMutex;
extern std::atomic<bool> capturing;
extern std::atomic<uint32_t> sessionId;
extern std::atomic<CaptureMode> captureMode;
Fix currentFix();
// Session changes and admission share a short critical section. A callback can
// tag its generation at entry without being relabelled by a later Start press.
void startCapture(uint32_t id, CaptureMode mode);
void endCapture();
bool beginObservation(Observation& observation);
void submit(Observation& observation);
}
