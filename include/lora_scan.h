#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lora_profiles.h"

namespace tunnel { namespace lora_scan {
enum class Mode : std::uint8_t { Meshtastic, MeshCore, Both };
// Stored with every packet, before rearming or changing the radio. Never infer
// a queued packet's settings from the device's subsequently selected profile.
struct Snapshot {
  float frequency_mhz=0, bandwidth_khz=0;
  std::uint8_t sf=0, cr=0, sync_word=0, protocol=0; // 1 Meshtastic / 2 MeshCore
  std::uint16_t preamble_symbols=0;
  bool lna=false;
  char name[32]{};
};
struct Status {
  Snapshot active;
  std::uint32_t listen_ms[2]{}, received[2]{};
  std::uint32_t switches=0, hop_deferrals=0, hop_timeouts=0, hop_interruptions=0;
  std::uint32_t remaining_ms=0;
  bool combined=false, meshcore_available=false;
};
struct MeshCoreProfile {
  const char* region;
  const char* name;
  double frequency_mhz, bandwidth_khz;
  std::uint8_t sf, cr;
};
// Numeric settings only, no upstream implementation imported. Sources:
// https://www.kismetwireless.net/docs/readme/datasources/lora-lorapipe/#channel-reference
// https://github.com/meshcore-dev/MeshCore/blob/81dbae592ffad40b4212bc0b37e82b38da08f4bc/docs/faq.md
// Preamble: src/helpers/radiolib/RadioLibWrappers.h at that same revision,
// 32 symbols for SF7/SF8 and 16 for SF9+. Sync word 0x12, explicit header.
// AUTO is a receive starting point, not a promise about the local community.
constexpr MeshCoreProfile meshCoreProfiles[] = {
  {"US","US-NARROW",910.525,62.5,7,5},
  {"US","US-SOCAL",927.875,62.5,7,5},
  {"EU_868","EU_868-NARROW",869.618,62.5,8,8},
  {"EU_868","EU_868-SF7",869.618,62.5,7,5},
  {"EU_868","EU_868-LEGACY",869.525,250,11,5},
  {"ANZ","ANZ-DEFAULT",915.800,250,10,5},
  {"ANZ","ANZ-NARROW",916.575,62.5,7,7},
  {"ANZ","ANZ-MID",915.075,125,9,5},
  {"ANZ","ANZ-NZ_NARROW",917.375,62.5,7,5}
};
constexpr std::size_t meshCoreProfileCount=sizeof(meshCoreProfiles)/sizeof(meshCoreProfiles[0]);
inline const MeshCoreProfile* findMeshCoreProfile(const char* region,const char* selection="AUTO") {
  if(!region || !selection) return nullptr;
  for(const auto& p:meshCoreProfiles) {
    if(std::strcmp(region,p.region) || (std::strcmp(selection,"AUTO") && std::strcmp(selection,p.name))) continue;
    return &p;
  }
  return nullptr;
}
inline const MeshCoreProfile* meshCoreProfile(const char* region,const char* selection="AUTO") {
  const auto* p=findMeshCoreProfile(region,selection);
  if(!p || !lora_profiles::fitsHardware(p->frequency_mhz,p->bandwidth_khz,
      lora_profiles::hardwareMinimum,lora_profiles::hardwareMaximum)) return nullptr;
  return p;
}
inline Snapshot snapshot(const MeshCoreProfile& p,bool lna=false) {
  Snapshot s;
  s.frequency_mhz=static_cast<float>(p.frequency_mhz);s.bandwidth_khz=static_cast<float>(p.bandwidth_khz);
  s.sf=p.sf;s.cr=p.cr;s.sync_word=0x12;s.protocol=2;s.preamble_symbols=p.sf<=8?32:16;s.lna=lna;
  std::strncpy(s.name,p.name,sizeof(s.name)-1);
  return s;
}
constexpr std::uint32_t minimumDwellMs=30000, maximumDwellMs=600000;
inline bool validDwell(std::uint32_t value) {return value>=minimumDwellMs && value<=maximumDwellMs;}
// Main-loop scheduler. Deadlines use unsigned subtraction and survive millis()
// wrap. A false preamble cannot pin a profile indefinitely; its grace period is
// bounded by the slowest complete packet airtime plus a driver margin.
class Dwell {
  std::uint32_t since_=0, dwell_=minimumDwellMs, busy_since_=0;
  bool waiting_=false;
public:
  void reset(std::uint32_t now,std::uint32_t duration) {
    since_=now;dwell_=validDwell(duration)?duration:minimumDwellMs;waiting_=false;
  }
  bool due(std::uint32_t now) const {return std::uint32_t(now-since_)>=dwell_;}
  std::uint32_t remaining(std::uint32_t now) const {
    const auto elapsed=std::uint32_t(now-since_);return elapsed>=dwell_?0:dwell_-elapsed;
  }
  enum class Decision { Wait, Switch, TimeoutSwitch };
  Decision consider(std::uint32_t now,bool inFlight,bool complete,std::uint32_t maximumPacketMs) {
    if(!due(now) || complete) return Decision::Wait;
    if(!inFlight) {waiting_=false;return Decision::Switch;}
    if(!waiting_) {waiting_=true;busy_since_=now;}
    if(std::uint32_t(now-busy_since_)>=maximumPacketMs) return Decision::TimeoutSwitch;
    return Decision::Wait;
  }
};
} }
