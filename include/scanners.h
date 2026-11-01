#pragma once
#include <stdint.h>
#include "gnss_protocol.h"
namespace tunnel {
using GnssSkyView=gnss_protocol::SkyView;
// Coordinate-free diagnostics. Read on the main loop that calls gnssTick().
// Ages are UINT32_MAX before a matching checksum-valid sentence. GSV visible
// count belongs to the latest talker/signal sentence, not a constellation sum.
struct GnssDiagnostics {
  uint32_t baud=0, bytes=0, checksum_ok=0, checksum_failed=0, sentences_with_fix=0;
  uint32_t gga=0, rmc=0, gsa=0, gsv=0, txt=0, other=0;
  // Historical exact documented TXT warnings, not an inferred antenna-OK bit.
  uint32_t antenna_open_reports=0, antenna_open_age_ms=UINT32_MAX;
  uint32_t antenna_ok_reports=0, antenna_status_age_ms=UINT32_MAX;
  // Exact receiver text only: -1 unknown, 0 OK, 1 OPEN. Not an RF-health test.
  int8_t antenna_status=-1;
  GnssSkyView sky[gnss_protocol::kSkyTalkers];
  uint32_t last_sentence_age_ms=UINT32_MAX, location_age_ms=UINT32_MAX;
  uint32_t gga_age_ms=UINT32_MAX, rmc_age_ms=UINT32_MAX;
  uint32_t gsa_age_ms=UINT32_MAX, gsv_age_ms=UINT32_MAX;
  int16_t fix_quality=-1, fix_type=-1, satellites_visible=-1;
  char rmc_status='?';
  bool parser_location_valid=false, date_valid=false, time_valid=false;
  // UART write completion does not imply a receiver acknowledgment.
  bool output_config_attempted=false, output_config_sent=false;
};
GnssDiagnostics gnssDiagnostics();
bool scannersBegin();
void scannersStart();
void scannersStop();
// Manual bench-only terminal state: refuses active capture, stops controllers,
// and leaves GNSS untouched. Reboot is required even if shutdown returns false.
// Caller must gate pending logger work and arm an independent restart timer:
// the upstream NimBLE shutdown waits have no finite timeout.
bool scannersEnterQuiet();
void scannersTick();
const char* scanPhase();
bool loraReady();
void gnssBegin();
void gnssTick();
unsigned long gnssCharacters();
}
