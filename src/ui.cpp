#include "ui.h"
#include "hardware.h"
#include "recon.h"
#include "version.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern const uint8_t logoStart[] asm("_binary_assets_lora_tunnel_radio_logo_png_start");
extern const uint8_t logoEnd[] asm("_binary_assets_lora_tunnel_radio_logo_png_end");

namespace {
constexpr uint16_t BG = 0x0021, PANEL = 0x0843, GREEN = 0x77e0;
constexpr uint16_t CYAN = 0x07ff, WHITE = 0xef7d, MUTED = 0x7c50;
constexpr uint16_t AMBER = 0xfd20, RED = 0xf9aa, BORDER = 0x1368;
constexpr const char* fields[] = {"WiGLE API name", "WiGLE API token",
  "WDGWars token", "Wi-Fi SSID", "Wi-Fi password"};

enum class Page { Home, Wardriver, Recon, ReconDetail, Settings, Keyboard };
Page page = Page::Home, settingsParent = Page::Home;
ui::Handler onAction = nullptr;
int tab = 0, field = 0, keyPage = 0;
uint8_t reconFilter = 0, reconPage = 0;
int focusIndex = -1;
bool focusVisible = false;
struct Rect { uint16_t x, y, w, h; };
Rect previousFocus{};
bool previousFocusDrawn = false;
constexpr size_t MAX_TARGETS = 56;
struct Detail {
  char title[48]{}, evidence[64]{};
  uint8_t category = 0, confidence = 0;
} detail;
char input[128]{};
size_t inputSize = 0;
bool dirty = true, wasPressed = false, splashShown = false;
uint32_t bootAt = 0, paintedAt = 0;

constexpr int FRAME_WIDTH = 240, FRAME_HEIGHT = 320;
constexpr size_t FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * sizeof(uint16_t);
lgfx::LGFX_Sprite frame;
void* frameBuffer = nullptr;
bool buffered = false, fullClearNeeded = true, pagePainted = false;
Page paintedPage = Page::Home;

lgfx::LovyanGFX& gfx() {
  if (buffered) return frame;
  return hw::display();
}

void present() {
  if (!buffered) return;
  // The caller already owns the shared display/SD mutex. A complete image is
  // transferred once; background clearing and individual primitives stay in RAM.
  hw::display().startWrite();
  frame.pushSprite(&hw::display(), 0, 0);
  hw::display().endWrite();
}

void eraseInput() {
  volatile char* secret = input;
  for (size_t i = 0; i < sizeof(input); ++i) secret[i] = 0;
  inputSize = 0;
}

void label(const char* value, int x, int y, uint16_t color = WHITE,
           int scale = 1, size_t maxCharacters = 38) {
  char bounded[80];
  const size_t n = std::min(maxCharacters, sizeof(bounded) - 1);
  snprintf(bounded, sizeof(bounded), "%.*s", static_cast<int>(n), value);
  gfx().setTextSize(scale);
  gfx().setTextColor(color);
  gfx().setTextDatum(lgfx::textdatum_t::top_left);
  gfx().drawString(bounded, x, y);
}

void button(int x, int y, int width, int height, const char* name,
            uint16_t accent = CYAN, bool selected = false) {
  gfx().fillRoundRect(x, y, width, height, 4, selected ? accent : PANEL);
  gfx().drawRoundRect(x, y, width, height, 4, accent);
  gfx().setTextSize(1);
  gfx().setTextColor(selected ? BG : accent);
  gfx().setTextDatum(lgfx::textdatum_t::middle_center);
  gfx().drawString(name, x + width / 2, y + height / 2);
  gfx().setTextDatum(lgfx::textdatum_t::top_left);
}

bool inside(uint16_t x, uint16_t y, int left, int top, int width, int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

void heading(const char* subtitle) {
  // Allocation failure keeps the direct UI usable without periodically blanking
  // the whole LCD. Only page transitions clear it; dynamic regions clear below.
  if (buffered || fullClearNeeded) gfx().fillScreen(BG);
  else gfx().fillRect(0, 30, FRAME_WIDTH, 16, BG);
  fullClearNeeded = false;
  // Small radio silhouette echoes the splash without decoding PNG each frame.
  gfx().fillRect(12, 5, 2, 8, GREEN);
  gfx().drawRoundRect(8, 12, 17, 16, 2, GREEN);
  gfx().drawRect(12, 15, 9, 5, CYAN);
  gfx().fillRect(12, 23, 9, 1, GREEN);
  label("LORA TUNNEL", 33, 9, GREEN, 2, 15);
  label(subtitle, 9, 32, MUTED, 1, 36);
  if (!buffered) label("UI BUFFER LOW: DIRECT DISPLAY", 3, 0, AMBER, 1, 38);
}

void splash() {
  heading("OFFLINE FIELD TERMINAL / " TUNNEL_VERSION);
  const size_t size = logoEnd - logoStart;
  if (size > 24) {
    const uint32_t width = (uint32_t{logoStart[16]} << 24) |
                          (uint32_t{logoStart[17]} << 16) |
                          (uint32_t{logoStart[18]} << 8) | logoStart[19];
    const uint32_t height = (uint32_t{logoStart[20]} << 24) |
                           (uint32_t{logoStart[21]} << 16) |
                           (uint32_t{logoStart[22]} << 8) | logoStart[23];
    if (width && height && width <= 4096 && height <= 4096) {
      const float scale = 180.0f / std::max(width, height);
      gfx().drawPng(logoStart, size, 30, 48, 180, 180, 0, 0, scale);
    }
  }
  label("WIFI + BLE + LORA", 18, 234, CYAN, 2, 18);
  label("DRIVE / RECON / EXPLORE", 48, 267, WHITE, 1, 30);
  label("Wardriver first. Built for the field.", 12, 292, MUTED, 1, 38);
}

void statusLines(const ui::Model& m) {
  gfx().fillRect(8, 48, 224, 24, BG);
  char status[40];
  if (std::isfinite(m.battery) && m.battery > 0) {
    snprintf(status, sizeof(status), "%s %s  %.2fV", m.sd ? "SD OK" : "SD !",
             m.gps ? "FIX" : "NO FIX", m.battery);
  } else {
    snprintf(status, sizeof(status), "%s %s  BAT --", m.sd ? "SD OK" : "SD !",
             m.gps ? "FIX" : "NO FIX");
  }
  label(status, 9, 48, m.sd ? CYAN : AMBER, 1, 37);
  label(m.gps_detail[0] ? m.gps_detail : (m.gps ? "GNSS FIX READY" : "GNSS: WAITING FOR FIX"),
        9, 61, m.gps ? GREEN : MUTED, 1, 37);
}

void setPage(Page next) {
  if (page != next) { page = next; focusIndex = -1; fullClearNeeded = true; }
  dirty = true;
}

bool modeMatches(const ui::Model& m) {
  return m.recon == (page == Page::Recon);
}

void sessionControls(const ui::Model& m) {
  const char* name = !modeMatches(m) ? "SWITCHING..." : m.running ? "STOP + SAVE" :
                     page == Page::Recon ? "START RECON" : "START DRIVE";
  button(8, 280, 48, 32, "HOME", CYAN);
  button(64, 280, 108, 32, name, m.running ? AMBER : GREEN, true);
  button(180, 280, 52, 32, "SETUP", CYAN);
}

void modeCard(int top, const char* title, const char* subtitle, uint16_t accent) {
  gfx().fillRoundRect(8, top, 224, 52, 5, PANEL);
  gfx().drawRoundRect(8, top, 224, 52, 5, accent);
  gfx().fillRect(8, top + 9, 4, 34, accent);
  label(title, 22, top + 9, accent, 2, 16);
  label(subtitle, 22, top + 35, MUTED, 1, 33);
}

void home(const ui::Model& m) {
  heading("YOUR OFFLINE FIELD TERMINAL");
  statusLines(m);
  modeCard(84, "WARDRIVER", m.running && !m.recon ? "SESSION RUNNING / TAP TO RETURN" :
           "Wi-Fi / BLE / LoRa session logs", GREEN);
  modeCard(146, "RECON", m.running && m.recon ? "SESSION RUNNING / TAP TO RETURN" :
           "Indicators / evidence / alerts", CYAN);
  button(8, 208, 224, 34, "SETUP / CONNECTION KEYS", CYAN);
  gfx().fillRect(8, 250, 224, 66, BG);
  if (m.notice[0]) label(m.notice, 9, 252, AMBER, 1, 37);
  else label(m.running ? "Capture continues while on Home." : "Choose a mode, then start a session.",
             9, 252, MUTED, 1, 37);
  label(m.buttons_enabled ? "IO: MOVE  USER: SELECT" : "TOUCH CONTROL / BUTTONS OFF", 9, 283, MUTED, 1, 37);
  if (m.buttons_enabled) label("Hold USER for Back", 9, 297, MUTED, 1, 37);
}

void wardriver(const ui::Model& m) {
  heading("WARDRIVER / RADIO OBSERVATIONS");
  statusLines(m);
  const uint32_t totals[] = {m.wifi, m.ble, m.lora};
  const char* names[] = {"WiFi", "BLE", "LoRa"};
  for (int i = 0; i < 3; ++i) {
    char count[12];
    snprintf(count, sizeof(count), "%lu", static_cast<unsigned long>(totals[i]));
    const uint16_t accent = i == 2 ? GREEN : CYAN;
    const uint16_t ink = i == tab ? BG : accent;
    const int left = 8 + i * 76;
    button(left, 78, 72, 26, "", accent, i == tab);
    label(names[i], left + (72 - strlen(names[i]) * 6) / 2, 81, ink, 1, 10);
    label(count, left + (72 - strlen(count) * 6) / 2, 93, ink, 1, 10);
  }
  gfx().fillRect(8, 110, 224, 22, BG);
  label(m.running && !m.recon ? "CAPTURING" : "READY", 9, 112, m.running && !m.recon ? GREEN : MUTED, 2, 10);
  label(m.phase[0] ? m.phase : "IDLE", 143, 114, CYAN, 1, 13);
  gfx().fillCircle(225, 119, 4, m.running && !m.recon ? GREEN : MUTED);

  bool any = false;
  for (int i = 0; i < 4; ++i) {
    const char* row = m.recent[tab][i];
    gfx().fillRoundRect(8, 136 + i * 26, 224, 24, 3, PANEL);
    if (!row[0]) continue;
    any = true;
    gfx().fillRect(8, 140 + i * 26, 3, 16, GREEN);
    label(row, 16, 140 + i * 26, WHITE, 1, 35);
    if (strnlen(row, 48) > 35) label(row + 35, 16, 150 + i * 26, MUTED, 1, 12);
  }
  gfx().fillRect(8, 246, 224, 30, BG);
  if (!any) {
    label("NO OBSERVATIONS YET", 44, 178, MUTED, 1, 30);
    label(tab == 2 && !m.lora_ready ? "Check LoRa profile / region" :
          "Start a session to populate this view", 14, 194, MUTED, 1, 36);
  }
  char detail[56];
  if (tab == 2) snprintf(detail, sizeof(detail), "RX %.39s", m.profile[0] ? m.profile : "not configured");
  else snprintf(detail, sizeof(detail), "DROP %lu  ERR %lu  GPS %s",
                static_cast<unsigned long>(m.drops), static_cast<unsigned long>(m.errors),
                m.nmea_chars ? "DATA" : "WAIT");
  label(detail, 9, 248, (m.drops || m.errors) ? AMBER : MUTED, 1, 37);
  if (tab == 2 && (m.drops || m.errors)) {
    snprintf(detail, sizeof(detail), "DROP %lu  ERR %lu", static_cast<unsigned long>(m.drops),
             static_cast<unsigned long>(m.errors));
    label(detail, 9, 262, AMBER, 1, 37);
  }
  else if (m.notice[0]) label(m.notice, 9, 262, AMBER, 1, 37);
  else label(m.utc[0] ? m.utc : "Waiting for GNSS time", 9, 262, MUTED, 1, 37);
  sessionControls(m);
}

constexpr uint8_t LAST_CATEGORY = static_cast<uint8_t>(tunnel::recon::Category::Custom);
const char* categoryShort(uint8_t category) {
  static const char* names[] = {"ALL", "ALPR", "FLOCK", "BODYCAM", "IBEACON", "RING",
    "CAMERAS", "DRONES", "FIND MY", "TRACKERS", "GLASSES", "ACOUSTIC", "SERIAL", "RADIO TOOLS", "CUSTOM"};
  static_assert(sizeof(names) / sizeof(names[0]) == LAST_CATEGORY + 1, "Update UI category labels");
  return category <= LAST_CATEGORY ? names[category] : "UNKNOWN";
}

const char* confidenceText(uint8_t confidence) {
  using tunnel::recon::Confidence;
  if (confidence == static_cast<uint8_t>(Confidence::High)) return "STRONG SIGNATURE";
  if (confidence == static_cast<uint8_t>(Confidence::Medium)) return "MEDIUM EVIDENCE";
  if (confidence == static_cast<uint8_t>(Confidence::Low)) return "WEAK INDICATOR";
  return "UNRATED EVIDENCE";
}

uint16_t confidenceColor(uint8_t confidence) {
  using tunnel::recon::Confidence;
  return confidence == static_cast<uint8_t>(Confidence::High) ? GREEN :
         confidence == static_cast<uint8_t>(Confidence::Medium) ? CYAN : AMBER;
}

unsigned filteredMatches(const ui::Model& m, uint8_t indices[8]) {
  unsigned count = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t category = m.recon_categories[i];
    if (m.recon_recent[i][0] && category > 0 && category <= LAST_CATEGORY &&
        (reconFilter == 0 || category == reconFilter)) indices[count++] = i;
  }
  return count;
}

void recon(const ui::Model& m) {
  heading("RECON / MATCHED INDICATORS");
  statusLines(m);
  char text[48];
  snprintf(text, sizeof(text), "FILTER: %s >", categoryShort(reconFilter));
  button(8, 78, 168, 26, text, CYAN);
  button(184, 78, 48, 26, m.recon_alerts ? "MUTE" : "SOUND", m.recon_alerts ? GREEN : MUTED);
  gfx().fillRect(8, 110, 224, 22, BG);
  snprintf(text, sizeof(text), "MATCHES %lu / BUZZER %s", static_cast<unsigned long>(m.recon_total),
           m.recon_alerts ? "ON" : "OFF");
  label(text, 9, 112, CYAN, 1, 35);
  label(m.running && m.recon ? "RECON RUNNING" : "RECON READY", 9, 123, m.running && m.recon ? GREEN : MUTED, 1, 25);
  uint8_t indices[8];
  const unsigned count = filteredMatches(m, indices);
  const unsigned pages = std::max(1U, (count + 1) / 2);
  if (reconPage >= pages) reconPage = pages - 1;
  for (unsigned slot = 0; slot < 2; ++slot) {
    const int top = 136 + slot * 52;
    gfx().fillRoundRect(8, top, 224, 48, 3, PANEL);
    const unsigned offset = reconPage * 2 + slot;
    if (offset >= count) continue;
    const uint8_t i = indices[offset];
    const uint16_t color = confidenceColor(m.recon_confidence[i]);
    gfx().fillRect(8, top + 6, 3, 36, color);
    label(m.recon_recent[i], 16, top + 4, WHITE, 1, 35);
    snprintf(text, sizeof(text), "%s / %s", categoryShort(m.recon_categories[i]), confidenceText(m.recon_confidence[i]));
    label(text, 16, top + 15, color, 1, 35);
    label(m.recon_evidence[i][0] ? m.recon_evidence[i] : "Tap to inspect available evidence", 16, top + 27, MUTED, 1, 35);
    if (strnlen(m.recon_evidence[i], 64) > 35) label(m.recon_evidence[i] + 35, 16, top + 37, MUTED, 1, 28);
  }
  if (!count) {
    label("NO MATCHES IN RECENT WINDOW", 27, 151, MUTED, 1, 34);
    label(m.running ? "Listening for selected indicators" : "Start Recon to listen for indicators", 14, 165, MUTED, 1, 36);
    label("Ordinary scan results stay in Drive.", 14, 202, MUTED, 1, 36);
    label("A match is evidence, not identity.", 14, 216, MUTED, 1, 36);
  }
  button(8, 242, 40, 26, "<", CYAN);
  gfx().fillRect(50, 242, 140, 26, BG);
  snprintf(text, sizeof(text), "%u/%u  %u RECENT", static_cast<unsigned>(reconPage) + 1, pages, count);
  label(text, 57, 251, MUTED, 1, 22);
  button(192, 242, 40, 26, ">", CYAN);
  gfx().fillRect(8, 270, 224, 8, BG);
  if (m.notice[0]) label(m.notice, 9, 270, AMBER, 1, 37);
  else {
    snprintf(text, sizeof(text), "DROP %lu  ERR %lu", static_cast<unsigned long>(m.drops), static_cast<unsigned long>(m.errors));
    label(text, 9, 270, (m.drops || m.errors) ? AMBER : MUTED, 1, 37);
  }
  sessionControls(m);
}

void wrapped(const char* value, int x, int y, size_t maximum, uint16_t color) {
  // Source fields have fixed bounds. Two lines cover the complete evidence field.
  const size_t length = strnlen(value, maximum);
  for (size_t offset = 0; offset < length; offset += 36)
    label(value + offset, x, y + (offset / 36) * 14, color, 1, std::min(size_t{36}, length - offset));
}

void reconDetail() {
  heading("RECON / OBSERVATION EVIDENCE");
  gfx().fillRoundRect(8, 52, 224, 56, 4, PANEL);
  wrapped(tunnel::recon::categoryName(static_cast<tunnel::recon::Category>(detail.category)), 14, 61, 64, CYAN);
  label(confidenceText(detail.confidence), 14, 93, confidenceColor(detail.confidence), 1, 36);
  gfx().fillRoundRect(8, 118, 224, 56, 4, PANEL);
  label("OBSERVED SUMMARY", 14, 126, GREEN, 1, 36);
  wrapped(detail.title, 14, 143, sizeof(detail.title), WHITE);
  gfx().fillRoundRect(8, 184, 224, 64, 4, PANEL);
  label("MATCH EVIDENCE", 14, 192, GREEN, 1, 36);
  wrapped(detail.evidence[0] ? detail.evidence : "No further evidence reported", 14, 210, sizeof(detail.evidence), WHITE);
  label("Evidence does not confirm identity.", 14, 259, MUTED, 1, 36);
  button(8, 280, 224, 32, "< BACK TO MATCHES", CYAN);
}

void settings(const ui::Model& m) {
  heading("SD CONFIG / CONNECTION KEYS");
  if (!buffered) gfx().fillRect(0, 250, FRAME_WIDTH, 24, BG);
  for (int i = 0; i < 5; ++i) button(8, 48 + i * 34, 224, 29, fields[i], i < 3 ? GREEN : CYAN);
  label("Tap a field to enter a replacement.", 12, 224, MUTED, 1, 36);
  label("Secrets are stored on the SD card.", 12, 237, MUTED, 1, 36);
  if (m.notice[0]) label(m.notice, 12, 253, AMBER, 1, 36);
  button(8, 280, 108, 32, "< BACK", CYAN);
  button(124, 280, 108, 32, "RETRY SD", GREEN);
}

const char* keyRow(int row) {
  static const char* lower[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890", ""};
  static const char* upper[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", "1234567890", ""};
  static const char* symbols[] = {"1234567890", "!@#$%^&*()", "-_=+[]{}\\", ";:'\",.<>/?", "`~|"};
  return keyPage == 2 ? symbols[row] : keyPage == 1 ? upper[row] : lower[row];
}

void keyboard() {
  heading("ENTER VALUE / LOCAL SD STORAGE");
  if (!buffered) gfx().fillRect(0, 110, FRAME_WIDTH, 134, BG);
  label(fields[field], 9, 50, GREEN, 1, 36);
  gfx().fillRoundRect(8, 66, 224, 36, 3, PANEL);
  gfx().drawRoundRect(8, 66, 224, 36, 3, BORDER);
  char view[35]{};
  const size_t visible = std::min(inputSize, size_t{33});
  const bool secret = field == 1 || field == 2 || field == 4;
  if (secret) memset(view, '*', visible);
  else memcpy(view, input + inputSize - visible, visible);
  label(inputSize ? view : "Tap keys below...", 14, 74, inputSize ? WHITE : MUTED, 1, 34);
  char length[24];
  snprintf(length, sizeof(length), "%u / 127 characters", static_cast<unsigned>(inputSize));
  label(length, 14, 89, MUTED, 1, 34);
  for (int row = 0; row < 5; ++row) {
    const char* keys = keyRow(row);
    for (int col = 0; keys[col]; ++col) {
      char key[2] = {keys[col], 0};
      button(6 + col * 23, 110 + row * 27, 21, 24, key, CYAN);
    }
  }
  // Row five also contains a space key without covering punctuation keys.
  button(101, 218, 130, 24, "SPACE", CYAN);
  button(8, 247, 68, 26, keyPage == 1 ? "abc" : "ABC", CYAN);
  button(86, 247, 68, 26, keyPage == 2 ? "letters" : "#+=", CYAN);
  button(164, 247, 68, 26, "< DEL", AMBER);
  button(8, 282, 108, 30, "CANCEL", CYAN);
  button(124, 282, 108, 30, "SAVE TO SD", GREEN);
}

void append(char value) {
  if (inputSize < sizeof(input) - 1) {
    input[inputSize++] = value;
    input[inputSize] = 0;
    dirty = true;
  }
}

void touchAction(uint16_t x, uint16_t y, const ui::Model& m) {
  if (page == Page::Home) {
    const bool drive = inside(x, y, 8, 84, 224, 52);
    const bool reconChoice = inside(x, y, 8, 146, 224, 52);
    if (drive || reconChoice) {
      if (m.recon != reconChoice && onAction) onAction(ui::Action::ToggleMode, reconChoice ? 1 : 0, "");
      setPage(reconChoice ? Page::Recon : Page::Wardriver);
    } else if (inside(x, y, 8, 208, 224, 34)) {
      settingsParent = page; setPage(Page::Settings);
    }
  } else if (page == Page::Wardriver || page == Page::Recon) {
    if (page == Page::Wardriver) {
      for (int i = 0; i < 3; ++i) if (inside(x, y, 8 + i * 76, 78, 72, 26)) tab = i;
    } else {
      if (inside(x, y, 8, 78, 168, 26)) {
        reconFilter = (reconFilter + 1) % (LAST_CATEGORY + 1); reconPage = 0;
      }
      if (inside(x, y, 184, 78, 48, 26) && onAction)
        onAction(ui::Action::ToggleAlerts, m.recon_alerts ? 0 : 1, "");
      uint8_t indices[8];
      const unsigned count = filteredMatches(m, indices);
      const unsigned pages = std::max(1U, (count + 1) / 2);
      if (reconPage >= pages) reconPage = pages - 1;
      if (inside(x, y, 8, 242, 40, 26)) reconPage = (reconPage + pages - 1) % pages;
      if (inside(x, y, 192, 242, 40, 26)) reconPage = (reconPage + 1) % pages;
      for (unsigned slot = 0; slot < 2; ++slot) {
        const unsigned offset = reconPage * 2 + slot;
        if (offset < count && inside(x, y, 8, 136 + slot * 52, 224, 48)) {
          const uint8_t i = indices[offset];
          memcpy(detail.title, m.recon_recent[i], sizeof(detail.title));
          memcpy(detail.evidence, m.recon_evidence[i], sizeof(detail.evidence));
          detail.title[sizeof(detail.title) - 1] = 0;
          detail.evidence[sizeof(detail.evidence) - 1] = 0;
          detail.category = m.recon_categories[i]; detail.confidence = m.recon_confidence[i];
          setPage(Page::ReconDetail);
        }
      }
    }
    if (inside(x, y, 8, 280, 48, 32)) setPage(Page::Home);
    else if (inside(x, y, 64, 280, 108, 32) && modeMatches(m) && onAction)
      onAction(ui::Action::ToggleSession, 0, "");
    else if (inside(x, y, 180, 280, 52, 32)) { settingsParent = page; setPage(Page::Settings); }
  } else if (page == Page::ReconDetail) {
    if (inside(x, y, 8, 280, 224, 32)) setPage(Page::Recon);
  } else if (page == Page::Settings) {
    for (int i = 0; i < 5; ++i) if (inside(x, y, 8, 48 + i * 34, 224, 29)) {
      eraseInput(); field = i; keyPage = 0; setPage(Page::Keyboard);
    }
    if (inside(x, y, 8, 280, 108, 32)) setPage(settingsParent);
    if (inside(x, y, 124, 280, 108, 32) && onAction) onAction(ui::Action::RetryStorage, 0, "");
  } else {
    for (int row = 0; row < 5; ++row) {
      const char* keys = keyRow(row);
      for (int col = 0; keys[col]; ++col)
        if (inside(x, y, 6 + col * 23, 110 + row * 27, 21, 24)) append(keys[col]);
    }
    if (inside(x, y, 101, 218, 130, 24)) append(' ');
    if (inside(x, y, 8, 247, 68, 26)) { keyPage = keyPage == 1 ? 0 : 1; focusIndex = -1; }
    if (inside(x, y, 86, 247, 68, 26)) { keyPage = keyPage == 2 ? 0 : 2; focusIndex = -1; }
    if (inside(x, y, 164, 247, 68, 26) && inputSize) input[--inputSize] = 0;
    if (inside(x, y, 8, 282, 108, 30)) { eraseInput(); setPage(Page::Settings); }
    if (inside(x, y, 124, 282, 108, 30)) {
      if (onAction) onAction(ui::Action::SaveCredential, field, input);
      eraseInput(); setPage(Page::Settings);
    }
  }
  dirty = true;
}

void back() {
  if (page == Page::Keyboard) { eraseInput(); setPage(Page::Settings); }
  else if (page == Page::Settings) setPage(settingsParent);
  else if (page == Page::ReconDetail) setPage(Page::Recon);
  else if (page != Page::Home) setPage(Page::Home);
}

size_t focusTargets(const ui::Model& m, Rect targets[MAX_TARGETS]) {
  size_t count = 0;
  const auto add = [&](int x, int y, int width, int height) {
    if (count < MAX_TARGETS) targets[count++] = Rect{static_cast<uint16_t>(x), static_cast<uint16_t>(y),
        static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
  };
  if (page == Page::Home) {
    add(8, 84, 224, 52); add(8, 146, 224, 52); add(8, 208, 224, 34);
  } else if (page == Page::Wardriver || page == Page::Recon) {
    if (page == Page::Wardriver) {
      for (int i = 0; i < 3; ++i) add(8 + i * 76, 78, 72, 26);
    } else {
      add(8, 78, 168, 26); add(184, 78, 48, 26);
      uint8_t indices[8];
      const unsigned countMatches = filteredMatches(m, indices);
      const unsigned pages = std::max(1U, (countMatches + 1) / 2);
      if (reconPage >= pages) reconPage = pages - 1;
      for (unsigned slot = 0; slot < 2; ++slot)
        if (reconPage * 2 + slot < countMatches) add(8, 136 + slot * 52, 224, 48);
      add(8, 242, 40, 26); add(192, 242, 40, 26);
    }
    add(8, 280, 48, 32); add(64, 280, 108, 32); add(180, 280, 52, 32);
  } else if (page == Page::ReconDetail) {
    add(8, 280, 224, 32);
  } else if (page == Page::Settings) {
    for (int i = 0; i < 5; ++i) add(8, 48 + i * 34, 224, 29);
    add(8, 280, 108, 32); add(124, 280, 108, 32);
  } else {
    for (int row = 0; row < 5; ++row) {
      const char* keys = keyRow(row);
      for (int col = 0; keys[col]; ++col) add(6 + col * 23, 110 + row * 27, 21, 24);
    }
    add(101, 218, 130, 24);
    add(8, 247, 68, 26); add(86, 247, 68, 26); add(164, 247, 68, 26);
    add(8, 282, 108, 30); add(124, 282, 108, 30);
  }
  return count;
}

void drawFocus(const ui::Model& m) {
  previousFocusDrawn = false;
  if (!focusVisible || !m.buttons_enabled) return;
  Rect targets[MAX_TARGETS];
  const size_t count = focusTargets(m, targets);
  if (!count) return;
  if (focusIndex < 0 || static_cast<size_t>(focusIndex) >= count) focusIndex = 0;
  const Rect& r = targets[focusIndex];
  gfx().drawRoundRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 5, WHITE);
  gfx().drawRoundRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, 4, WHITE);
  previousFocus = r;
  previousFocusDrawn = true;
}
}

namespace ui {
void begin(Handler handler) {
  onAction = handler;
  bootAt = 0;
  splashShown = false;
  wasPressed = false;
  page = Page::Home; settingsParent = Page::Home;
  tab = 0; reconFilter = 0; reconPage = 0;
  focusIndex = -1; focusVisible = false;
  previousFocusDrawn = false;
  eraseInput();
  dirty = true;
  pagePainted = false;
  fullClearNeeded = true;
  // Fixed RGB565 storage is PSRAM-only, with no large stack/internal-RAM fallback.
  // LovyanGFX setBuffer attaches this application-owned allocation without a copy.
  if (!frameBuffer) frameBuffer = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buffered = frameBuffer != nullptr;
  if (buffered) {
    frame.setColorDepth(16);
    frame.setBuffer(frameBuffer, FRAME_WIDTH, FRAME_HEIGHT, 16);
  } else {
    Serial.println("UI framebuffer allocation failed; direct drawing fallback");
  }
  gfx().setTextWrap(false, false);
  gfx().setFont(&lgfx::fonts::Font0);
}

void navigation(uint8_t events, const Model& model) {
  if (!model.buttons_enabled) {
    if (focusVisible) { focusVisible = false; focusIndex = -1; dirty = true; }
    return;
  }
  if (!events || !splashShown || millis() - bootAt < 1400) return;
  focusVisible = true;
  dirty = true;
  // Back wins over a simultaneous short action; navigation never draws.
  if (events & hw::Back) { back(); return; }
  Rect targets[MAX_TARGETS];
  const size_t count = focusTargets(model, targets);
  if (!count) return;
  if (events & hw::Next) {
    focusIndex = focusIndex < 0 ? 0 : (focusIndex + 1) % count;
    return; // Move wins over simultaneous Select, avoiding an accidental click.
  }
  if (events & hw::Select) {
    if (focusIndex < 0 || static_cast<size_t>(focusIndex) >= count) focusIndex = 0;
    const Rect& r = targets[focusIndex];
    touchAction(r.x + r.w / 2, r.y + r.h / 2, model);
  }
}

void tick(const Model& model) {
  if (!splashShown) {
    splash();
    present();
    splashShown = true;
    // Count visible time after PNG decoding, independent of scanner startup.
    bootAt = millis();
    return;
  }
  const uint32_t now = millis();
  if (now - bootAt < 1400) return;
  uint16_t x, y;
  const bool pressed = hw::touch(x, y);
  if (pressed && !wasPressed) {
    focusVisible = false; focusIndex = -1;
    touchAction(x, y, model);
  }
  wasPressed = pressed;
  // Credential screens redraw only on input; capture status refreshes at 2 Hz.
  if (!dirty && (page == Page::Keyboard || page == Page::ReconDetail || now - paintedAt < 500)) return;
  if (!pagePainted || paintedPage != page) fullClearNeeded = true;
  gfx().startWrite();
  // In the low-memory direct path, remove the previous focus outline without
  // clearing the whole display. Buffered rendering clears only offscreen RAM.
  if (!buffered && !fullClearNeeded && previousFocusDrawn) {
    const Rect& r = previousFocus;
    gfx().drawRoundRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 5, BG);
    gfx().drawRoundRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, 4, BG);
  }
  if (page == Page::Home) home(model);
  else if (page == Page::Wardriver) wardriver(model);
  else if (page == Page::Recon) recon(model);
  else if (page == Page::ReconDetail) reconDetail();
  else if (page == Page::Settings) settings(model);
  else keyboard();
  drawFocus(model);
  gfx().endWrite();
  present();
  paintedPage = page;
  pagePainted = true;
  dirty = false;
  paintedAt = now;
}
}
