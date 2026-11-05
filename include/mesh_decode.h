#pragma once
// Original MIT, bounded receive-only protocol interface. No network stack.
#include <cstddef>
#include <cstdint>

namespace tunnel { namespace mesh {
enum class Protocol : std::uint8_t { Unknown, Meshtastic, MeshCore };
enum class Status : std::uint8_t { Raw, Decoded, Encrypted, Unsupported, Malformed, KeyMismatch };
enum class Authentication : std::uint8_t { None, Unverified, Verified, Failed };
struct Result {
  Protocol protocol=Protocol::Unknown;
  Status status=Status::Raw;
  Authentication authentication=Authentication::None;
  bool has_node=false, has_position=false, has_altitude=false, has_time=false;
  bool has_battery=false, has_voltage=false, has_text=false, sanitized=false;
  bool signature_present=false;
  bool has_hardware=false, has_role=false;
  std::uint8_t packet_type=0, path_count=0, path_hash_size=0, precision_bits=0;
  std::uint16_t port=0;
  std::uint32_t node_id=0, timestamp=0, hardware=0, role=0, battery=0;
  std::int32_t latitude_i=0, longitude_i=0, altitude=0; // degrees * 1e7; metres
  float voltage=0;
  std::uint8_t public_key[32]{};
  char name[40]{}, short_name[5]{}, text[160]{};
};
// Crypto adapters have no ownership and never print key material. Test adapters
// use independent vectors; production uses the pinned ESP32 SDK.
struct Crypto {
  bool (*ctr)(const std::uint8_t*,std::size_t,const std::uint8_t[16],const std::uint8_t*,std::size_t,std::uint8_t*)=nullptr;
  bool (*verify)(const std::uint8_t[32],const std::uint8_t[64],const std::uint8_t*,std::size_t)=nullptr;
};
struct Key {
  std::uint8_t bytes[32]{};
  std::uint8_t length=0, channel_hash=0;
};
constexpr std::size_t MaxKeys=4;
// Known key list belongs to configuration, never to observations or log output.
void decodeMeshtastic(const std::uint8_t* data,std::size_t length,const Crypto& crypto,
                      const Key* keys,std::size_t keyCount,bool publicDefault,Result& out);
void decodeMeshCore(const std::uint8_t* data,std::size_t length,const Crypto& crypto,Result& out);
const Crypto& platformCrypto();
inline const char* protocolName(Protocol p) {
  return p==Protocol::Meshtastic?"meshtastic":p==Protocol::MeshCore?"meshcore":"unknown";
}
inline const char* statusName(Status s) {
  switch(s) {
    case Status::Decoded:return "decoded"; case Status::Encrypted:return "encrypted_or_unknown";
    case Status::Unsupported:return "unsupported"; case Status::Malformed:return "malformed";
    case Status::KeyMismatch:return "key_mismatch_or_unsupported"; default:return "not_decoded";
  }
}
inline const char* authenticationName(Authentication a) {
  switch(a) {case Authentication::Verified:return "verified";case Authentication::Failed:return "failed";
    case Authentication::Unverified:return "unverified";default:return "none";}
}
} }
