// Copyright (c) 2026 Valleytech Solutions. SPDX-License-Identifier: MIT
// Original bounded wire-format reader; no upstream implementation/generated
// protobufs incorporated. Numeric protocol facts inspected 2026-11-05:
// https://github.com/meshtastic/protobufs/tree/85f3f4357946a52daa5310285476d2dffb137c7b/meshtastic
// (mesh.proto, telemetry.proto, portnums.proto)
// https://github.com/meshtastic/firmware/tree/9bcf0f776edabb3b2447db323323cf2c49835452/src/mesh
// (Channels.h, CryptoEngine.cpp). The well-known default key is public data.
#include "mesh_decode.h"
#include "mesh_header.h"
#include "text_sanitize.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace tunnel { namespace mesh { namespace {
constexpr std::uint8_t publicKey[16]={
  0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01
};
struct Field {
  std::uint32_t number=0;
  std::uint8_t wire=0;
  std::uint64_t value=0;
  const std::uint8_t* bytes=nullptr;
  std::size_t length=0;
};
// Three explicitly chosen levels (Data -> telemetry -> device metrics), no
// recursive parser, allocation, casts through unaligned pointers, or strlen on
// packet bytes. The complete radio frame is at most 255 bytes.
class Reader {
  const std::uint8_t* bytes_;
  std::size_t length_, offset_=0;
  bool ok_=true;
  bool integer(std::uint64_t& value) {
    value=0;
    for(unsigned n=0;n<10;++n) {
      if(offset_==length_) return false;
      const std::uint8_t b=bytes_[offset_++];
      if(n==9 && b>1) return false;
      value|=std::uint64_t(b&0x7f)<<(7*n);
      if(!(b&0x80)) return true;
    }
    return false;
  }
 public:
  Reader(const std::uint8_t* bytes,std::size_t length):bytes_(bytes),length_(length) {}
  bool next(Field& field) {
    if(!ok_ || offset_==length_) return false;
    field=Field{};
    std::uint64_t tag;
    if(!integer(tag) || tag>0xffffffffULL || (tag>>3)==0 || (tag>>3)>0x1fffffff) return ok_=false;
    field.number=std::uint32_t(tag>>3); field.wire=std::uint8_t(tag&7);
    if(field.wire==0) return ok_=integer(field.value);
    if(field.wire==1 || field.wire==5) {
      const unsigned count=field.wire==1?8:4;
      if(count>length_-offset_) return ok_=false;
      for(unsigned n=0;n<count;++n)field.value|=std::uint64_t(bytes_[offset_++])<<(8*n);
      return true;
    }
    if(field.wire==2) {
      std::uint64_t count;
      if(!integer(count) || count>length_-offset_)return ok_=false;
      field.bytes=bytes_+offset_;field.length=std::size_t(count);offset_+=field.length;
      return true;
    }
    // Groups are not part of the supported proto3 wire messages.
    return ok_=false;
  }
  bool complete() const {return ok_ && offset_==length_;}
};
bool once(std::uint64_t& mask,std::uint32_t field) {
  if(field>=64)return true;
  const auto bit=std::uint64_t(1)<<field;
  if(mask&bit)return false;
  mask|=bit;return true;
}
std::int32_t signed32(std::uint32_t value) {
  // Avoid implementation-defined narrowing of unsigned values above INT32_MAX.
  return value<=0x7fffffffU?std::int32_t(value):
      std::int32_t(-1-std::int64_t(0xffffffffU-value));
}
bool int32Value(std::uint64_t value,std::int32_t& output) {
  // Protobuf int32 uses sign-extended ten-byte varints for negative values.
  if(value>0xffffffffULL && value<0xffffffff80000000ULL)return false;
  output=signed32(std::uint32_t(value));return true;
}
bool parseUser(const std::uint8_t* bytes,std::size_t length,Result& result) {
  Reader reader(bytes,length);Field field;std::uint64_t seen=0;
  while(reader.next(field)) {
    if(field.number<=9 && !once(seen,field.number))return false;
    switch(field.number) {
      case 1: // User-supplied textual ID is not substituted for radio sender ID.
        if(field.wire!=2)return false;
        break;
      case 2:
        if(field.wire!=2)return false;
        result.sanitized|=copyUtf8Bounded(result.name,sizeof(result.name),field.bytes,field.length);
        break;
      case 3:
        if(field.wire!=2)return false;
        result.sanitized|=copyUtf8Bounded(result.short_name,sizeof(result.short_name),field.bytes,field.length);
        break;
      case 4: if(field.wire!=2)return false;break;
      case 5:
        if(field.wire!=0 || field.value>UINT32_MAX)return false;
        result.hardware=std::uint32_t(field.value);result.has_hardware=true;break;
      case 6: case 9:
        if(field.wire!=0 || field.value>1)return false;break;
      case 7:
        if(field.wire!=0 || field.value>UINT32_MAX)return false;
        result.role=std::uint32_t(field.value);result.has_role=true;break;
      case 8:
        if(field.wire!=2 || (field.length!=0 && field.length!=32))return false;
        if(field.length)std::memcpy(result.public_key,field.bytes,32);
        break;
      default:break;
    }
  }
  if(!reader.complete())return false;
  result.has_node=(seen&0x1fe)!=0; // An empty node-info request reports no node fields.
  return true;
}
bool parsePosition(const std::uint8_t* bytes,std::size_t length,Result& result) {
  Reader reader(bytes,length);Field field;std::uint64_t seen=0;
  while(reader.next(field)) {
    if(field.number<=23 && !once(seen,field.number))return false;
    switch(field.number) {
      case 1: case 2:
        if(field.wire!=5)return false;
        if(field.number==1)result.latitude_i=signed32(std::uint32_t(field.value));
        else result.longitude_i=signed32(std::uint32_t(field.value));
        break;
      case 3:
        if(field.wire!=0 || !int32Value(field.value,result.altitude))return false;
        result.has_altitude=true;break;
      case 4:
        if(field.wire!=5)return false;
        result.timestamp=std::uint32_t(field.value);result.has_time=result.timestamp!=0;break;
      case 7: if(field.wire!=5)return false;break;
      case 23:
        if(field.wire!=0 || field.value>32)return false;
        result.precision_bits=std::uint8_t(field.value);break;
      default:
        // The remaining current Position fields use varints. Unknown future
        // fields still pass the generic wire/length validation.
        if(field.number>=5 && field.number<=22 && field.wire!=0)return false;
        break;
    }
  }
  if(!reader.complete())return false;
  if(result.latitude_i < -900000000 || result.latitude_i > 900000000 ||
     result.longitude_i < -1800000000 || result.longitude_i > 1800000000)return false;
  // Presence rather than value: (0,0) is a valid advertised coordinate pair.
  result.has_position=(seen&6)==6;
  return true;
}
bool parseDeviceMetrics(const std::uint8_t* bytes,std::size_t length,Result& result) {
  Reader reader(bytes,length);Field field;std::uint64_t seen=0;
  while(reader.next(field)) {
    if(field.number<=5 && !once(seen,field.number))return false;
    switch(field.number) {
      case 1:
        // 101 is the protocol's externally-powered value, not 101 percent.
        if(field.wire!=0 || field.value>101)return false;
        result.battery=std::uint32_t(field.value);result.has_battery=true;break;
      case 2: {
        if(field.wire!=5)return false;
        const std::uint32_t bits=std::uint32_t(field.value);
        static_assert(sizeof(float)==sizeof(bits),"32-bit IEEE float required");
        std::memcpy(&result.voltage,&bits,sizeof(bits));
        if(!std::isfinite(result.voltage) || result.voltage<0)return false;
        result.has_voltage=true;break;
      }
      case 3:case 4:if(field.wire!=5)return false;break;
      case 5:if(field.wire!=0 || field.value>UINT32_MAX)return false;break;
      default:break;
    }
  }
  return reader.complete();
}
bool parseTelemetry(const std::uint8_t* bytes,std::size_t length,Result& result) {
  Reader reader(bytes,length);Field field;std::uint64_t seen=0;bool variant=false;
  while(reader.next(field)) {
    if(field.number<=11 && !once(seen,field.number))return false;
    if(field.number==1) {
      if(field.wire!=5)return false;
      result.timestamp=std::uint32_t(field.value);result.has_time=result.timestamp!=0;
    } else if(field.number>=2 && field.number<=11 && field.number!=10) {
      if(field.wire!=2 || variant)return false;
      variant=true;
      if(field.number==2) {
        if(!parseDeviceMetrics(field.bytes,field.length,result))return false;
      } else result.status=Status::Unsupported;
    }
  }
  return reader.complete();
}
bool parseData(const std::uint8_t* bytes,std::size_t length,Result& result) {
  Reader reader(bytes,length);Field field;std::uint64_t seen=0;
  const std::uint8_t* payload=nullptr;std::size_t payloadLength=0;
  while(reader.next(field)) {
    if(field.number<=10 && !once(seen,field.number))return false;
    switch(field.number) {
      case 1:
        if(field.wire!=0 || !field.value || field.value>511)return false;
        result.port=std::uint16_t(field.value);break;
      case 2:
        if(field.wire!=2)return false;
        payload=field.bytes;payloadLength=field.length;break;
      case 3:if(field.wire!=0 || field.value>1)return false;break;
      case 4:case 5:case 6:case 7:case 8:if(field.wire!=5)return false;break;
      case 9:if(field.wire!=0 || field.value>UINT32_MAX)return false;break;
      case 10:
        if(field.wire!=2 || (field.length!=0 && field.length!=64))return false;
        result.signature_present=field.length==64;break;
      default:break;
    }
  }
  if(!reader.complete() || !(seen&2))return false;
  result.authentication=Authentication::Unverified;
  // protobuf omits an empty bytes field. Requests with an empty payload still
  // have a valid envelope, but do not invent names/coordinates/telemetry.
  if(!payload)payload=bytes+length;
  bool valid=true;
  switch(result.port) {
    case 1:case 10:case 11:
      result.has_text=payloadLength!=0;
      result.sanitized|=copyUtf8Bounded(result.text,sizeof(result.text),payload,payloadLength);break;
    case 3:valid=parsePosition(payload,payloadLength,result);break;
    case 4:valid=parseUser(payload,payloadLength,result);break;
    case 67:valid=parseTelemetry(payload,payloadLength,result);break;
    default:result.status=Status::Unsupported;return true;
  }
  if(!valid)return false;
  if(result.status!=Status::Unsupported)result.status=Status::Decoded;
  // Ordinary channel AES-CTR supplies no cryptographic integrity; parsing,
  // channel-hash matches and radio CRC never authenticate the original sender.
  result.authentication=Authentication::Unverified;
  return true;
}
void put32(std::uint8_t* output,std::uint32_t value) {
  for(unsigned n=0;n<4;++n)output[n]=std::uint8_t(value>>(8*n));
}
void clearScratch(std::uint8_t* bytes,std::size_t length) {
  volatile std::uint8_t* p=bytes;while(length--)*p++=0;
}
} // namespace

void decodeMeshtastic(const std::uint8_t* data,std::size_t length,const Crypto& crypto,
                      const Key* keys,std::size_t keyCount,bool publicDefault,Result& out) {
  out=Result{};out.protocol=Protocol::Meshtastic;
  lt::MeshHeader header;
  if(!lt::parseMeshHeader(data,length,header) || length==lt::kMeshHeaderBytes) {
    out.status=Status::Malformed;return;
  }
  out.node_id=header.from;
  if(keyCount>MaxKeys || (keyCount && !keys)) {out.status=Status::Unsupported;return;}
  std::uint8_t nonce[16]{};
  put32(nonce,header.id);put32(nonce+8,header.from);
  std::uint8_t scratch[239]{};
  const std::size_t payloadLength=length-lt::kMeshHeaderBytes;
  bool attempted=false, hadPlaintext=false;
  // Try exact configured hashes first. The default key may be used on a renamed
  // channel, so its final bounded attempt is not restricted to hash 0x08.
  for(std::size_t index=0;index<keyCount+(publicDefault?1u:0u);++index) {
    const bool defaultAttempt=index==keyCount;
    const Key* key=defaultAttempt?nullptr:&keys[index];
    if(key && (key->channel_hash!=header.channel || (key->length!=0 && key->length!=16 && key->length!=32)))continue;
    // 0xA6 is the ordinary LongFast/default hash XOR the AEAD discriminator.
    // It is only a hint (hashes collide), so explicit configured CTR keys win.
    if(defaultAttempt && header.channel==0xa6)continue;
    const std::uint8_t* keyBytes=defaultAttempt?publicKey:key->bytes;
    const std::size_t keyLength=defaultAttempt?sizeof(publicKey):key->length;
    attempted=true;
    if(keyLength) {
      if(!crypto.ctr || !crypto.ctr(keyBytes,keyLength,nonce,data+16,payloadLength,scratch))continue;
    } else {
      hadPlaintext=true;std::memcpy(scratch,data+16,payloadLength);
    }
    Result candidate;candidate.protocol=Protocol::Meshtastic;candidate.node_id=header.from;
    if(parseData(scratch,payloadLength,candidate)) {
      out=candidate;clearScratch(scratch,sizeof(scratch));return;
    }
    clearScratch(scratch,sizeof(scratch));
  }
  clearScratch(scratch,sizeof(scratch));
  out.status=hadPlaintext?Status::Malformed:attempted?Status::KeyMismatch:
      publicDefault && header.channel==0xa6?Status::Unsupported:Status::Encrypted;
}
} } // namespace tunnel::mesh
