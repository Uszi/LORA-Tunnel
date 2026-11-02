#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tunnel { namespace gnss_protocol {
// Quectel L76K protocol V1.1 section 2.3.3. These are output divisors,
// not a change to the positioning interval. Preserve the reserved fields.
constexpr char kDiagnosticBody[]="PCAS03,1,0,1,2,1,0,0,1,0,0,,,0,0";
constexpr std::size_t kDiagnosticCommandSize=sizeof(kDiagnosticBody)-1+6;
// Quectel L76K GNSS Protocol V1.1 sections 2.3.4/2.3.5:
// https://files.waveshare.com/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf
// Restart flags are 0=hot, 1=warm, 2=cold, 3=factory. No factory helper is exposed.
// PCAS input commands have no specified ACK; construction does not prove execution.
constexpr char kWarmRestartBody[]="PCAS10,1";
constexpr char kColdRestartBody[]="PCAS10,2";
constexpr char kAllConstellationsBody[]="PCAS04,7";
// Read-only software-version probe from Meshtastic's L76K (GNSS_MODEL_MTK) path:
// https://github.com/meshtastic/firmware/blob/6d41e279f1f51bd59f687b9d441c1bf47b1594fc/src/gps/GPS.cpp#L1360
// It expects TXT SW= information; this query is not listed in Quectel V1.1.
constexpr char kVersionQueryBody[]="PCAS06,0";

inline std::uint8_t nmeaChecksum(const char* body,std::size_t length) {
  std::uint8_t value=0;
  for(std::size_t i=0;i<length;++i) value^=static_cast<std::uint8_t>(body[i]);
  return value;
}

namespace detail {
template<std::size_t N>
inline std::size_t frameCommand(const char (&body)[N],char* output,std::size_t capacity) {
  // Literal-only commands, bounded to a 64-byte destination including the NUL.
  static_assert(N>1 && N<=58,"GNSS command body exceeds bounded frame size");
  constexpr std::size_t bodyLength=N-1, commandSize=bodyLength+6;
  if(!output || capacity<=commandSize) return 0;
  constexpr char hex[]="0123456789ABCDEF";
  const std::uint8_t checksum=nmeaChecksum(body,bodyLength);
  output[0]='$';
  std::memcpy(output+1,body,bodyLength);
  output[bodyLength+1]='*';
  output[bodyLength+2]=hex[checksum>>4];
  output[bodyLength+3]=hex[checksum&15];
  output[bodyLength+4]='\r'; output[bodyLength+5]='\n';
  output[commandSize]=0;
  return commandSize;
}
}
// Pure construction only: no UART, reset, delay or configuration side effects.
// All or nothing; zero leaves output untouched. Success includes CR/LF but not
// the NUL in its return count; capacity must also allow that terminating NUL.
inline std::size_t diagnosticCommand(char* output,std::size_t capacity) {
  return detail::frameCommand(kDiagnosticBody,output,capacity);
}
inline std::size_t warmRestartCommand(char* output,std::size_t capacity) {
  return detail::frameCommand(kWarmRestartBody,output,capacity);
}
inline std::size_t coldRestartCommand(char* output,std::size_t capacity) {
  return detail::frameCommand(kColdRestartBody,output,capacity);
}
inline std::size_t allConstellationsCommand(char* output,std::size_t capacity) {
  return detail::frameCommand(kAllConstellationsBody,output,capacity);
}
inline std::size_t versionQueryCommand(char* output,std::size_t capacity) {
  return detail::frameCommand(kVersionQueryBody,output,capacity);
}

// L76K software TXT responses can split the description and version at a comma:
// $GPTXT,01,01,02,SW=URANUS5,V5.3.0.0*checksum. Preserve both text fields.
// This only collects bounded fields. The caller must reset it per sentence and
// require a valid whole-sentence checksum before publishing recognized results.
class SoftwareVersionFields {
public:
  char value[64]{};
  bool recognized=false, invalid=false;
  void field(std::uint16_t term,const char* text,std::size_t length) {
    if(invalid) return;
    if(term==4) {
      if(sawText_) { reject(); return; }
      sawText_=true;
      if(!text || length<3 || std::memcmp(text,"SW=",3)) return;
      recognized=true;
      if(length==3 || !append(text+3,length-3,false)) reject();
    } else if(term==5 && recognized) {
      if(sawVersion_) { reject(); return; }
      sawVersion_=true;
      if(length && !append(text,length,true)) reject();
    }
  }
private:
  std::size_t size_=0;
  bool sawText_=false, sawVersion_=false;
  void reject() { invalid=true; size_=0; value[0]=0; }
  bool append(const char* text,std::size_t length,bool comma) {
    // Check size before addition/iteration, including untrusted oversized lengths.
    if(!text || length>=sizeof(value) || size_+length+(comma?1:0)>=sizeof(value)) return false;
    for(std::size_t i=0;i<length;++i) {
      const unsigned char c=static_cast<unsigned char>(text[i]);
      if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') ||
           c=='.' || c=='_' || c=='=' || c=='-' || c==' ')) return false;
    }
    if(comma) value[size_++]=',';
    std::memcpy(value+size_,text,length); size_+=length; value[size_]=0;
    return true;
  }
};

// Exact strings documented by Quectel's protocol/QuecPython examples. Unknown
// free-form messages, including unverified short-circuit spellings, stay unknown.
enum class AntennaStatus : std::int8_t { Unknown=-1, Ok=0, Open=1 };
inline AntennaStatus antennaStatus(const char* field,std::size_t length) {
  if(!field) return AntennaStatus::Unknown;
  if(length==12 && !std::memcmp(field,"ANTENNA OPEN",12)) return AntennaStatus::Open;
  // Quectel's L76K QuecPython documentation supplies this exact TXT example.
  if(length==10 && !std::memcmp(field,"ANTENNA OK",10)) return AntennaStatus::Ok;
  return AntennaStatus::Unknown;
}
inline bool antennaOpen(const char* field,std::size_t length) {
  return antennaStatus(field,length)==AntennaStatus::Open;
}

constexpr std::size_t kSkyTalkers=5;
struct SkyView {
  char talker[3]{};
  std::uint32_t sentences=0, complete_groups=0, age_ms=UINT32_MAX;
  std::int16_t visible=-1, max_cn0=-1;
  std::uint8_t cn0_count=0, positive_cn0_count=0, signal_id=255;
};
// GSV contains only satellite metadata, not receiver coordinates. Capture at
// most 20 numeric terms and reject oversized/malformed fields before assembly.
struct GsvFields {
  std::int16_t values[21];
  std::uint8_t count=0;
  bool invalid=false;
  GsvFields() { for(auto& value:values) value=-1; }
  void field(std::uint16_t term,const char* text,std::size_t length) {
    if(term==0 || term>20) { invalid=true; return; }
    count=static_cast<std::uint8_t>(term);
    if(!length) return;
    if(length>3) { invalid=true; return; }
    std::int16_t value=0;
    for(std::size_t i=0;i<length;++i) {
      if(text[i]<'0' || text[i]>'9') { invalid=true; return; }
      value=value*10+(text[i]-'0');
    }
    values[term]=value;
  }
};
class SkyTracker {
  struct Slot {
    SkyView view;
    std::uint32_t completedAt=0, startedAt=0;
    std::int16_t pendingVisible=-1, maximum=-1;
    std::uint8_t expected=0, total=0, signal=255, count=0, positive=0;
    bool active=false;
  } slots_[kSkyTalkers];
  static int talkerIndex(const char* id) {
    if(!id) return -1;
    constexpr const char* names[]={"GP","GB","BD","GL","GN"};
    for(std::size_t i=0;i<kSkyTalkers;++i)
      if(id[0]==names[i][0] && id[1]==names[i][1]) return static_cast<int>(i);
    return -1;
  }
 public:
  SkyTracker() {
    constexpr const char* names[]={"GP","GB","BD","GL","GN"};
    for(std::size_t i=0;i<kSkyTalkers;++i) std::memcpy(slots_[i].view.talker,names[i],3);
  }
  // Call only after the complete parent sentence has passed its checksum.
  void accept(const char* talker,const GsvFields& fields,std::uint32_t now) {
    const int index=talkerIndex(talker);
    if(index<0) return;
    auto& slot=slots_[index];
    ++slot.view.sentences;
    const auto& f=fields.values;
    if(fields.invalid || fields.count<3 || fields.count>20 || f[1]<1 || f[1]>9 ||
       f[2]<1 || f[2]>f[1] || f[3]<0 || f[3]>99) { slot.active=false; return; }
    const unsigned extra=(fields.count-3)%4;
    if(extra>1) { slot.active=false; return; }
    const unsigned blocks=(fields.count-3-extra)/4;
    const int signal=extra?f[fields.count]:-1;
    if(blocks>4 || signal>15) { slot.active=false; return; }
    const std::uint8_t signalId=signal<0?255:static_cast<std::uint8_t>(signal);
    if(f[2]==1) {
      slot.active=true; slot.startedAt=now; slot.expected=1;
      slot.total=static_cast<std::uint8_t>(f[1]); slot.signal=signalId;
      slot.pendingVisible=f[3]; slot.maximum=-1; slot.count=slot.positive=0;
    }
    if(!slot.active || now-slot.startedAt>5000 || f[2]!=slot.expected ||
       f[1]!=slot.total || f[3]!=slot.pendingVisible || signalId!=slot.signal) {
      slot.active=false; return;
    }
    for(unsigned i=0;i<blocks;++i) {
      const int id=f[4+4*i], cn0=f[7+4*i];
      if(cn0>99 || (cn0>=0 && id<=0)) { slot.active=false; return; }
      if(cn0>=0) {
        ++slot.count; if(cn0>0) ++slot.positive;
        if(cn0>slot.maximum) slot.maximum=static_cast<std::int16_t>(cn0);
      }
    }
    ++slot.expected;
    if(f[2]==slot.total) {
      slot.view.visible=slot.pendingVisible; slot.view.max_cn0=slot.maximum;
      slot.view.cn0_count=slot.count; slot.view.positive_cn0_count=slot.positive;
      slot.view.signal_id=slot.signal; ++slot.view.complete_groups;
      slot.completedAt=now; slot.active=false;
    }
  }
  void snapshot(SkyView (&result)[kSkyTalkers],std::uint32_t now) const {
    for(std::size_t i=0;i<kSkyTalkers;++i) {
      result[i]=slots_[i].view;
      if(result[i].complete_groups) result[i].age_ms=now-slots_[i].completedAt;
    }
  }
};
} }
