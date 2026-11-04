#pragma once
#include <cstddef>
#include <cstdint>
#include "recon.h"

namespace tunnel { namespace alerts {
// Fixed, original short riffs. These name cues, not verified device identities.
enum class PatternId : uint8_t {
  None, Alpr, Flock, Axon, BodyCamera, IBeacon, Ring, Camera, Drone,
  AirTag, Tracker, SmartTag, Tile, FindHub, Glasses, Raven, Skimmer,
  Hacker, Flipper, Pwnagotchi, Biscuit, Custom, Count
};
struct Pattern { const char* text; uint16_t length; uint8_t priority; };
struct Note { uint16_t frequency_hz=0, duration_ms=0; };
constexpr uint16_t MAX_PATTERN_BYTES=192;
constexpr uint8_t MAX_PATTERN_NOTES=24;
constexpr uint16_t MAX_PATTERN_MS=1000;
constexpr uint16_t MAX_PLAYBACK_MS=1200;
const Pattern& pattern(PatternId id);
PatternId patternFor(const recon::Result& result);

// Bounded RTTTL subset: name:d=1/2/4/8/16/32,o=4..7,b=60..240:notes.
// Notes support an optional denominator, c..b/p, sharp, octave and one dot.
// Only complete <=1 second patterns with <=24 notes are admitted. The supplied
// text must remain alive while read; production patterns are immutable flash data.
class Reader {
public:
  bool begin(const char* text, size_t length);
  bool next(Note& note);
  uint16_t durationMs() const { return duration_; }
  uint8_t noteCount() const { return notes_; }
private:
  bool parse(Note& note);
  const char* text_=nullptr;
  uint16_t length_=0, cursor_=0, duration_=0, bpm_=0;
  uint8_t denominator_=0, octave_=0, notes_=0;
};

// Main-loop-only sink. hz=0 means immediate silence; each nonzero tone also
// has a finite hardware duration, so a stalled loop cannot leave a held tone.
using ToneOutput = void (*)(uint16_t hz, uint16_t durationMs);
class Scheduler {
public:
  explicit Scheduler(ToneOutput output) : output_(output) {}
  void setEnabled(bool enabled);
  bool request(const recon::Result& result, uint32_t now);
  bool request(PatternId id, uint32_t now);
  void tick(uint32_t now);
  void cancel();
  bool active() const { return active_; }
  PatternId current() const { return current_; }
private:
  void startNote(const Note& note,uint32_t now);
  ToneOutput output_;
  Reader reader_;
  uint32_t noteEnd_=0, gateEnd_=0, hardEnd_=0;
  PatternId current_=PatternId::None;
  bool enabled_=false, active_=false, sounding_=false;
};

// History is independent of the evicting display summary and mute state.
// Check before request; remember ONLY when a motif actually starts. No backlog.
// Full history suppresses new sound, never evicts/replays an older identity.
// Reset only on an explicit new Recon session. All calls belong to the main loop.
enum class HistoryStatus : uint8_t { Unseen, Seen, Full, Invalid };
class AlertHistory {
public:
  static constexpr size_t CAPACITY=128;
  HistoryStatus check(uint8_t radioKind,uint8_t addressType,const char* mac) const;
  bool remember(uint8_t radioKind,uint8_t addressType,const char* mac);
  void reset();
  bool full() const { return fingerprints_[CAPACITY-1]!=0; }
  size_t size() const;
private:
  static uint64_t fingerprint(uint8_t radioKind,uint8_t addressType,const char* mac);
  uint64_t fingerprints_[CAPACITY]{};
};
static_assert(sizeof(AlertHistory)==1024,"Alert history must remain a fixed 1 KiB");
} }
