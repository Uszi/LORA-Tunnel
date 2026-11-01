#pragma once
#include <cstdint>

namespace ui {
struct Model {
  bool running = false, recon = false, sd = false, gps = false, lora_ready = false;
  uint32_t wifi = 0, ble = 0, lora = 0, drops = 0, errors = 0, nmea_chars = 0;
  bool buttons_enabled = true, recon_alerts = true;
  uint32_t recon_total = 0;
  float battery = 0;
  char utc[24]{}, profile[40]{}, phase[20]{}, notice[64]{};
  char recent[3][4][48]{};
  char gps_detail[48]{};
  // Latest matched summaries only; Category/Confidence use tunnel::recon enums.
  char recon_recent[8][48]{}, recon_evidence[8][64]{};
  uint8_t recon_categories[8]{}, recon_confidence[8]{};
};
enum class Action { ToggleSession, ToggleMode, SaveCredential, RetryStorage, ToggleAlerts };
// Handler executes synchronously, sometimes while UI owns SPI. Copy text to a
// bounded action queue immediately; do not access SD, radio, or draw here.
using Handler = void (*)(Action, int, const char*);
void begin(Handler handler);
// No display/SPI access. Call from the same task as tick(), outside the bus lock.
// Bitmask follows hw::Next=1, Select=2, Back=4. Disabled input is discarded.
void navigation(uint8_t events, const Model& model);
void tick(const Model& model);
}
