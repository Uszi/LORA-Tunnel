#pragma once
#include <cstddef>
#include <cstdint>

namespace tunnel { namespace recon {
// Confidence describes the evidence, not a measured probability or intent.
enum class Category : uint8_t {
  None, Alpr, Flock, Axon, IBeacon, Ring, Camera, Drone, AirTag,
  Tracker, Glasses, Raven, Skimmer, Hacker, Custom
};
enum class Confidence : uint8_t { None, Low, Medium, High };
constexpr size_t kMaxBleBytes = 512;
constexpr size_t kMaxWifiBytes = 1024;

struct RemoteId {
  bool decoded = false;
  bool has_id = false, has_location = false, has_operator = false;
  bool has_altitude = false, has_speed = false;
  uint8_t id_type = 0, aircraft_type = 0, operator_location_type = 255;
  char id[41] = {};
  double latitude = 0, longitude = 0;
  double operator_latitude = 0, operator_longitude = 0;
  float altitude_m = 0, speed_mps = 0;
};
struct Beacon {
  bool valid = false;
  uint8_t uuid[16] = {};
  uint16_t major = 0, minor = 0;
  int8_t measured_power = 0;
};
struct Result {
  Category category = Category::None;
  Confidence confidence = Confidence::None;
  bool payload_valid = false;
  char label[32] = {}, vendor[32] = {}, evidence[128] = {};
  RemoteId remote_id;
  Beacon beacon;
  bool matched() const { return category != Category::None; }
};

const char* categoryName(Category category);
const char* confidenceName(Confidence confidence);
bool parseAddress(const char* text, uint8_t address[6]);

// fullFrame, when supplied, is an entire 802.11 frame WITHOUT its trailing FCS.
// Its transmitter and validated information elements supersede address/ssid.
// Without a frame, address must be the observed AP's BSSID, not a destination.
Result classifyWifi(const uint8_t address[6], const char* ssid,
                    const uint8_t* fullFrame = nullptr, size_t frameLen = 0);
Result classifyWifi(const char* address, const char* ssid,
                    const uint8_t* fullFrame = nullptr, size_t frameLen = 0);

// addressType uses NimBLE values 0 public, 1 random, 2 public identity,
// 3 random identity. Random/unknown addresses never supply OUI evidence.
// advLength optionally separates concatenated advertisement and scan response.
Result classifyBle(const uint8_t address[6], uint8_t addressType,
                   const uint8_t* ad, size_t length, size_t advLength = 0);
Result classifyBle(const char* address, uint8_t addressType,
                   const uint8_t* ad, size_t length, size_t advLength = 0);
} }
