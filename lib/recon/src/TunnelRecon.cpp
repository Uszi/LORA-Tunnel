// LORA Tunnel adapter, 2026-11-01. MIT; Dragon Detector notices retained locally.
#include "TunnelRecon.h"
#include "dragon/axon.h"

namespace tunnel { namespace recon {
namespace {
void copy(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  size_t i = 0;
  if (src) for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
  dst[i] = 0;
}
Category category(dragon::Family f, const char* evidence) {
  using F = dragon::Family;
  switch (f) {
    case F::Flock: return Category::Flock;
    case F::Alpr: return Category::Alpr;
    case F::Axon: return Category::Axon;
    case F::IBeacon: return Category::IBeacon;
    case F::Ring: return Category::Ring;
    case F::Camera: return Category::Camera;
    case F::Drone: return Category::Drone;
    case F::Tracker:
      return evidence && (strstr(evidence,"Find My") || strstr(evidence,"Apple") || strstr(evidence,"AirTag"))
        ? Category::AirTag : Category::Tracker;
    case F::SmartTag: case F::GoogleTag: case F::Tile: return Category::Tracker;
    case F::Meta: return Category::Glasses;
    case F::Raven: return Category::Raven;
    case F::Skimmer: return Category::Skimmer;
    case F::Pineapple: case F::Pwnagotchi: case F::Nyanbox:
    case F::Halehound: case F::Flipper: case F::Biscuit: return Category::Hacker;
    default: return Category::None;
  }
}
void apply(Result& out, dragon::Match match) {
  if (match.confidence == dragon::Confidence::None) return;
  out.category = category(match.family, match.evidence);
  if (out.category == Category::None) return;
  out.confidence = static_cast<Confidence>(match.confidence);
  copy(out.label,sizeof(out.label),categoryName(out.category));
  copy(out.evidence,sizeof(out.evidence),match.evidence);
  // These labels name the matched manufacturer/protocol, never its operator.
  const char* vendor = "";
  if (out.category == Category::AirTag || out.category == Category::IBeacon) vendor="Apple format";
  else if (strstr(out.evidence,"Xuntong")) vendor="Xuntong";
  else if (out.category == Category::Flock && out.confidence >= Confidence::Medium) vendor="Flock accessory";
  else if (out.category == Category::Axon && strstr(out.evidence,"Axon")) vendor="Axon / TASER candidate";
  else if (out.remote_id.decoded) vendor="Remote ID broadcast";
  else if (out.category == Category::Ring && strstr(out.evidence,"Ring")) vendor="Ring";
  else if (strstr(out.evidence,"Samsung")) vendor="Samsung";
  else if (strstr(out.evidence,"Tile")) vendor="Tile";
  else if (strstr(out.evidence,"Find Hub")) vendor="Find Hub format";
  else if (strstr(out.evidence,"OUI") || strstr(out.evidence,"product unverified")) {
    copy(out.vendor,sizeof(out.vendor),out.evidence);
    char* stop = strstr(out.vendor," OUI");
    if (!stop) stop = strchr(out.vendor,';');
    if (stop) *stop=0;
    return;
  }
  copy(out.vendor,sizeof(out.vendor),vendor);
}
void rid(Result& out, const dragon::DroneInfo& in) {
  auto& d=out.remote_id;
  d.decoded=in.valid; d.has_id=in.hasId; d.has_location=in.hasLocation;
  d.has_operator=in.hasOperator; d.has_altitude=in.hasAltitude; d.has_speed=in.hasSpeed;
  d.id_type=in.idType; d.aircraft_type=in.uaType; d.operator_location_type=in.operatorLocationType;
  copy(d.id,sizeof(d.id),in.id);
  if(in.hasLocation){d.latitude=in.latitude;d.longitude=in.longitude;}
  if(in.hasOperator){d.operator_latitude=in.operatorLatitude;d.operator_longitude=in.operatorLongitude;}
  if(in.hasAltitude)d.altitude_m=in.altitude;
  if(in.hasSpeed)d.speed_mps=in.speed;
}
void beacon(Result& out,const uint8_t* ad,size_t n,size_t advLength) {
  // Called only after the complete AD stream has passed the bounded parser.
  for(size_t at=0;at<n;) {
    const size_t end=advLength && at<advLength?advLength:n;
    const uint8_t len=ad[at++];
    if(!len){at=end;continue;}
    if(len>end-at)return;
    const uint8_t type=ad[at];const uint8_t* p=ad+at+1;const size_t z=len-1;
    if(type==0xff && z==25 && p[0]==0x4c && p[1]==0 && p[2]==2 && p[3]==0x15){
      out.beacon.valid=true;memcpy(out.beacon.uuid,p+4,16);
      out.beacon.major=uint16_t(p[20])<<8|p[21];
      out.beacon.minor=uint16_t(p[22])<<8|p[23];
      out.beacon.measured_power=static_cast<int8_t>(p[24]);return;
    }
    at+=len;
  }
}
int hex(char c) {
  return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
}
}

const char* categoryName(Category c) {
  switch(c){
    case Category::Alpr:return "ALPR vendor candidate";
    case Category::Flock:return "Flock / accessory candidate";
    case Category::Axon:return "Body-camera candidate";
    case Category::IBeacon:return "iBeacon";
    case Category::Ring:return "Ring vendor candidate";
    case Category::Camera:return "Camera vendor candidate";
    case Category::Drone:return "Drone / Remote ID";
    case Category::AirTag:return "Find My / AirTag candidate";
    case Category::Tracker:return "Tracker protocol candidate";
    case Category::Glasses:return "Camera-glasses candidate";
    case Category::Raven:return "Acoustic-sensor candidate";
    case Category::Skimmer:return "Serial-module candidate";
    case Category::Hacker:return "Radio-tool candidate";
    case Category::Custom:return "Custom rule candidate";
    default:return "Unclassified";
  }
}
const char* confidenceName(Confidence c) {
  return c==Confidence::High?"Strong signature":c==Confidence::Medium?"Medium evidence":c==Confidence::Low?"Weak indicator":"Unclassified";
}
bool parseAddress(const char* text,uint8_t address[6]) {
  if(!text||!address)return false;
  uint8_t parsed[6]{};
  for(size_t i=0;i<6;++i){
    if(!text[0]||!text[1])return false;
    const int a=hex(text[0]),b=hex(text[1]);if(a<0||b<0)return false;
    parsed[i]=uint8_t(a*16+b);text+=2;
    if(i<5){if(*text++!=':')return false;}
  }
  if(*text)return false;memcpy(address,parsed,6);return true;
}
Result classifyWifi(const uint8_t address[6],const char* ssid,const uint8_t* frame,size_t n) {
  Result out;
  if(n>kMaxWifiBytes || (n && !frame))return out;
  if(frame){
    const auto parsed=dragon::parseWifi(frame,n);
    if(!parsed.valid)return out;
    out.payload_valid=true;rid(out,parsed.drone);
    auto match=dragon::withAxon(parsed.match,dragon::inspectAxonWifi(frame,n));
    apply(out,match);return out;
  }
  if(!address)return out;
  char name[33]{};size_t size=0;
  if(ssid)while(size<32&&ssid[size])++size;
  if(size)dragon::safeText(name,sizeof(name),reinterpret_cast<const uint8_t*>(ssid),size);
  auto match=dragon::named(name,dragon::Family::Wifi);
  match=dragon::addressHint(address,true,match);
  // Shared/local vendor addresses never prove a manufacturer or product.
  if(!(address[0]&3)){
    dragon::AxonEvidence axon;
    if(dragon::axonPrefix(address))axon.signals=dragon::AxonOui;
    else if(dragon::bodyCameraPrefix(address))axon.signals=dragon::BodyCameraVendor;
    else if(dragon::axonResearchPrefix(address))axon.signals=dragon::AxonResearch;
    match=dragon::withAxon(match,axon);
  }
  out.payload_valid=true;apply(out,match);return out;
}
Result classifyWifi(const char* address,const char* ssid,const uint8_t* frame,size_t n) {
  uint8_t mac[6]{};
  if(!parseAddress(address,mac) && !frame)return {};
  return classifyWifi(mac,ssid,frame,n);
}
Result classifyBle(const uint8_t address[6],uint8_t type,const uint8_t* ad,size_t n,size_t advLength) {
  Result out;
  if(!ad||!n||n>kMaxBleBytes||advLength>n)return out;
  const auto parsed=dragon::parseBle(ad,n,advLength);
  if(!parsed.valid)return out;
  out.payload_valid=true;rid(out,parsed.drone);beacon(out,ad,n,advLength);
  // Never use a random-address prefix, even as a weak compatibility match.
  const uint8_t noAddress[6]{};
  const bool publicAddress=address&&(type==0||type==2)&&!(address[0]&3);
  const uint8_t* mac=publicAddress?address:noAddress;
  auto match=dragon::withAxon(parsed.match,dragon::inspectAxonBle(ad,n,advLength,mac,0));
  if(publicAddress)match=dragon::addressHint(address,true,match);
  apply(out,match);return out;
}
Result classifyBle(const char* address,uint8_t type,const uint8_t* ad,size_t n,size_t advLength) {
  uint8_t mac[6]{};
  const bool valid=parseAddress(address,mac);
  return classifyBle(valid?mac:nullptr,valid?type:255,ad,n,advLength);
}
} }
