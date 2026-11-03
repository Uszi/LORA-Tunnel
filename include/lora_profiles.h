#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tunnel { namespace lora_profiles {
// Original receive-profile data derived from Meshtastic's region definitions,
// LongFast parameters and default-name frequency calculation, inspected 2026-11-01:
// https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/src/mesh/RadioInterface.cpp
// https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/src/mesh/MeshRadio.h
// Slot = uint32 DJB2("LongFast") % floor((end-start)/0.250); center = start+.125+slot*.250.
// No upstream implementation is included. A renamed/custom mesh channel may use another slot.
// The target is Heltec's 863-928 MHz variant; 433 and 470-510 MHz are separate products:
// https://heltec.org/project/v4-r8-ex/ . No transmit settings or paths are introduced.
struct Profile {
  const char* code;
  const char* label;
  double frequency_mhz, bandwidth_khz, minimum_mhz, maximum_mhz;
  std::uint8_t sf, cr;
  std::uint16_t preamble, frequency_slot;
  bool high_band_compatible;
};
constexpr Profile profiles[] = {
  {"US", "US / Canada", 906.875,250,902,928,11,5,16,20,true},
  {"EU_868", "Europe 868",869.525,250,869.4,869.65,11,5,16,1,true},
  {"ANZ", "Australia / NZ 915",919.875,250,915,928,11,5,16,20,true},
  {"CN", "China 470",478.875,250,470,510,11,5,16,36,false},
  {"JP", "Japan",923.375,250,920.5,923.5,11,5,16,12,true},
  {"KR", "Korea",922.875,250,920,923,11,5,16,12,true},
  {"TW", "Taiwan",923.875,250,920,925,11,5,16,16,true},
  {"RU", "Russia",869.075,250,868.7,869.2,11,5,16,2,true},
  {"IN", "India",865.875,250,865,867,11,5,16,4,true},
  {"NZ_865", "New Zealand 865",864.875,250,864,868,11,5,16,4,true},
  {"TH", "Thailand",923.875,250,920,925,11,5,16,16,true},
  {"UA_868", "Ukraine 868",868.375,250,868,868.6,11,5,16,2,true},
  {"MY_919", "Malaysia 919",922.875,250,919,924,11,5,16,16,true},
  {"SG_923", "Singapore 923",917.875,250,917,925,11,5,16,4,true},
  {"PH_868", "Philippines 868",868.125,250,868,869.4,11,5,16,1,true},
  {"PH_915", "Philippines 915",917.875,250,915,918,11,5,16,12,true},
  {"KZ_863", "Kazakhstan 863",866.875,250,863,868,11,5,16,16,true},
  {"NP_865", "Nepal 865",867.875,250,865,868,11,5,16,12,true},
  {"BR_902", "Brazil 902",903.875,250,902,907.5,11,5,16,8,true},
  {"EU_433", "Europe 433",433.875,250,433,434,11,5,16,4,false},
  {"ANZ_433", "Australia / NZ 433",434.425,250,433.05,434.79,11,5,16,6,false},
  {"UA_433", "Ukraine 433",434.375,250,433,434.7,11,5,16,6,false},
  {"MY_433", "Malaysia 433",433.875,250,433,435,11,5,16,4,false},
  {"PH_433", "Philippines 433",434.375,250,433,434.7,11,5,16,6,false},
  {"KZ_433", "Kazakhstan 433",434.450,250,433.075,434.775,11,5,16,6,false}
};
constexpr std::size_t profileCount=sizeof(profiles)/sizeof(profiles[0]);
inline const Profile* find(const char* code) {
  if(code) for(const auto& profile:profiles) if(!std::strcmp(code,profile.code)) return &profile;
  return nullptr;
}
inline bool fitsHardware(double frequency, double bandwidth,
                         double minimum, double maximum) {
  const double halfWidth=bandwidth/2000.0;
  return frequency-halfWidth>=minimum-0.000001 && frequency+halfWidth<=maximum+0.000001;
}
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
constexpr double hardwareMinimum=868.0, hardwareMaximum=923.0;
#else
constexpr double hardwareMinimum=863.0, hardwareMaximum=928.0;
#endif
inline bool selectable(const Profile& profile) {
  return profile.high_band_compatible &&
      fitsHardware(profile.frequency_mhz,profile.bandwidth_khz,hardwareMinimum,hardwareMaximum);
}
inline bool isUnset(const char* code) { return code && !std::strcmp(code,"UNSET"); }
} }
