#pragma once
#include <cstddef>
#include <cstdint>

namespace tunnel {
struct BleAdSummary {
  bool malformed=false, has_manufacturer=false;
  std::uint16_t manufacturer_id=0;
};
// Inspect bounded AD structures, respecting NimBLE's ADV/SCAN_RSP boundary.
// A zero-length element pads only its own segment. Do not derive an ID from a
// structure that consumes bytes from the other segment or a truncated record.
inline BleAdSummary summarizeBleAd(const std::uint8_t* bytes,std::size_t length,
                                   std::size_t advertisingLength=0) {
  BleAdSummary result;
  if((length && !bytes) || length>512 || advertisingLength>length) {
    result.malformed=true; return result;
  }
  for(std::size_t at=0;at<length;) {
    const std::size_t end=advertisingLength && at<advertisingLength?advertisingLength:length;
    const std::size_t size=bytes[at++];
    if(!size) { at=end; continue; }
    if(size>end-at) { result.malformed=true; result.has_manufacturer=false; return result; }
    if(size>=3 && bytes[at]==0xff) {
      result.has_manufacturer=true;
      result.manufacturer_id=std::uint16_t(bytes[at+1])|(std::uint16_t(bytes[at+2])<<8);
    }
    at+=size;
  }
  return result;
}
}
